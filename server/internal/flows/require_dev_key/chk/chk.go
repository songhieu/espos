// Workflow require_dev_key, node chk (imported by tools/importer).
package chk

import (
	"crypto/sha256"
	"encoding/hex"
	"espos/internal/wm"
	"strings"
	"time"
)

// The espos CLI's developer key (Tài khoản › Khoá nhà phát triển on the web): "espos_dev_" and 64 lowercase hex
// digits, made in the browser and shown once. Only its sha256 is kept (dev_keys.key_hash), as the notification key.
// The key is the route's own Authorization header (public routes only), which the platform scrubs from the run log.
// A deleted key is simply not found. last_used_ms is written at most every 10 minutes.
const prefix = "espos_dev_"

func Run(wmIn, wmCtx map[string]any) map[string]any {
	headers, _ := wmIn["headers"].(map[string]any)
	auth := strings.TrimSpace(wm.Str(headers["authorization"]))
	if !strings.HasPrefix(auth, "Bearer ") {
		return map[string]any{"ok": false}
	}
	key := strings.TrimSpace(strings.TrimPrefix(auth, "Bearer "))
	if len(key) != len(prefix)+64 || !strings.HasPrefix(key, prefix) {
		return map[string]any{"ok": false}
	}
	if _, err := hex.DecodeString(key[len(prefix):]); err != nil || strings.ToLower(key) != key {
		return map[string]any{"ok": false}
	}
	sum := sha256.Sum256([]byte(key))
	rows, err := wm.QueryOpts("dev_keys", map[string]any{
		"filters": []map[string]any{{"column": "key_hash", "op": "eq", "value": hex.EncodeToString(sum[:])}},
		"limit":   1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) != 1 || wm.Int(rows[0]["user_id"]) <= 0 {
		return map[string]any{"ok": false}
	}
	r := rows[0]
	now := time.Now().UnixMilli()
	if now-int64(wm.Num(r["last_used_ms"])) > 10*60*1000 {
		if _, uerr := wm.Update("dev_keys", r["id"], map[string]any{"last_used_ms": now}); uerr != nil {
			wm.Log("last_used_ms not written: ", uerr.Error())
		}
	}
	return map[string]any{"ok": true, "user_id": wm.Int(r["user_id"]), "key_id": wm.Int(r["id"]), "label": wm.Str(r["label"])}
}
