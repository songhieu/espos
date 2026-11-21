// Workflow desktop_rotate, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"regexp"
	"strings"
)

// POST /v1/desktop/rotate {"token_hash": sha256(new token)} with the old token (s3desk/docs/desktop-protocol.md §1):
// the device row takes the new hash, so the old token stops working at once.
var hashRe = regexp.MustCompile(`^[0-9a-f]{64}$`)

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return answer(503, map[string]any{"error": "desktop_disabled"})
	}
	dev, _ := in["device"].(map[string]any)
	deviceID := wm.Int(dev["device_id"])
	if deviceID == 0 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	body, _ := in["body"].(map[string]any)
	hash := strings.ToLower(strings.TrimSpace(wm.Str(body["token_hash"])))
	if !hashRe.MatchString(hash) {
		return answer(422, map[string]any{"error": "invalid"})
	}
	rows, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	if len(rows) != 1 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	if wm.Str(rows[0]["kind"]) != "desktop" {
		return answer(403, map[string]any{"error": "not_desktop"})
	}
	if wm.Str(rows[0]["token_hash"]) == hash {
		return answer(200, map[string]any{"ok": true}) // a retry of the same rotation
	}
	taken, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{{"column": "token_hash", "op": "eq", "value": hash}}, "limit": 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	if len(taken) > 0 {
		return answer(409, map[string]any{"error": "token_taken"})
	}
	if _, uerr := wm.Update("devices", deviceID, map[string]any{"token_hash": hash}); uerr != nil {
		wm.Log("save: ", uerr.Error())
		return answer(500, map[string]any{"error": "server_error"})
	}
	return answer(200, map[string]any{"ok": true})
}
