// Workflow require_device, node chk (imported by tools/importer).
package chk

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"strings"
	"time"
)

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	headers, _ := in["headers"].(map[string]any)
	auth := strings.TrimSpace(wm.Str(headers["authorization"]))
	if !strings.HasPrefix(auth, "Device ") {
		return map[string]any{"ok": false, "err": "unauthorized"}
	}
	token := strings.ToLower(strings.TrimSpace(strings.TrimPrefix(auth, "Device ")))
	if len(token) != 64 {
		return map[string]any{"ok": false, "err": "unauthorized"}
	}
	if _, err := hex.DecodeString(token); err != nil {
		return map[string]any{"ok": false, "err": "unauthorized"}
	}
	sum := sha256.Sum256([]byte(token))
	hash := hex.EncodeToString(sum[:])

	rows, err := wm.QueryOpts("devices", map[string]any{
		"filters": []map[string]any{{"column": "token_hash", "op": "eq", "value": hash}},
		"limit":   1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) == 1 && !wm.Bool(rows[0]["revoked"]) {
		d := rows[0]
		return map[string]any{
			"ok": true, "device_id": wm.Int(d["id"]), "user_id": wm.Int(d["user_id"]),
			"name": wm.Str(d["name"]), "email": wm.Str(d["user_email"]), "err": "",
		}
	}
	if len(rows) == 1 {
		return map[string]any{"ok": false, "err": "unauthorized"}
	}
	// Not a device yet: a pairing still waiting for the user's confirmation?
	pend, err := wm.QueryOpts("pairings", map[string]any{
		"filters": []map[string]any{{"column": "token_hash", "op": "eq", "value": hash}},
		"limit":   1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(pend) == 1 && wm.Str(pend[0]["status"]) == "pending" && int64(wm.Num(pend[0]["expires_ms"])) > time.Now().UnixMilli() {
		return map[string]any{"ok": false, "err": "not_linked"}
	}
	return map[string]any{"ok": false, "err": "unauthorized"}
}
