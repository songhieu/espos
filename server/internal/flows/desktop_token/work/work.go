// Workflow desktop_token, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"fmt"
	"regexp"
	"strings"
	"time"
)

// POST /v1/desktop/token (s3desk/docs/desktop-protocol.md §3): the channel the server pokes ({"t":"sync"}) when a
// command for this Mac is queued, waits for a yes or is cancelled. Named by the desktop's own row (never by the
// request): rt:desk:<device id>:<chan_key>, the key made at the first sync (or here) and kept in desk_state. The
// token (node tok) subscribes only; the channel carries no command data.
var hostRe = regexp.MustCompile(`^[A-Za-z0-9.\-]{1,200}(:[0-9]{1,5})?$`)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	no := func(status int, code string) map[string]any {
		return map[string]any{"ok": false, "status": status, "body": map[string]any{"error": code}}
	}
	if wm.Str(in["on"]) != "1" {
		return no(503, "desktop_disabled")
	}
	dev, _ := in["device"].(map[string]any)
	deviceID := wm.Int(dev["device_id"])
	userID := wm.Int(dev["user_id"])
	if deviceID == 0 || userID == 0 {
		return no(401, "unauthorized")
	}
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return no(500, "server_error")
	}
	first := func(i int) map[string]any {
		if i >= len(res) {
			return nil
		}
		switch r := res[i]["rows"].(type) {
		case []map[string]any:
			if len(r) > 0 {
				return r[0]
			}
		case []any:
			if len(r) > 0 {
				m, _ := r[0].(map[string]any)
				return m
			}
		}
		return nil
	}
	d := first(0)
	if d == nil {
		return no(401, "unauthorized")
	}
	if wm.Str(d["kind"]) != "desktop" {
		return no(403, "not_desktop")
	}
	key := wm.Str(first(1)["chan_key"])
	if key == "" {
		sum := sha256.Sum256([]byte(fmt.Sprint(deviceID, wm.Str(d["token_hash"]), time.Now().UnixNano(), wmCtx["run_id"])))
		key = hex.EncodeToString(sum[:16])
		if _, uerr := wm.Upsert("desk_state", []string{"device_id"}, map[string]any{"device_id": deviceID, "user_id": userID, "chan_key": key}, nil); uerr != nil {
			wm.Log("save: ", uerr.Error())
			return no(500, "server_error")
		}
	}
	api := wm.PublicBase(strings.TrimSpace(wm.Str(in["host"]))) // this server's own address
	return map[string]any{"ok": true, "chan": fmt.Sprintf("%d:%s", deviceID, key), "channel": fmt.Sprintf("rt:desk:%d:%s", deviceID, key), "api": api,
		"identity": fmt.Sprintf("desk:%d", deviceID)}
}
