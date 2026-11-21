// Workflow chat_device_dog, node own (imported by tools/importer).
package own

import (
	"espos/internal/wm"
	"strings"
)

// POST /v1/devices/:id/assist {"text": "…", "cid": "…", "dog": "bao"}: the device must be the signed-in person's.
// The answer is assist_reply's, as the device gets it (the web page then plays GET …/assist/say).

func clean(s string, n int) string {
	s = strings.ToLower(strings.TrimSpace(s))
	if len(s) > n {
		return ""
	}
	for _, r := range s {
		if !(r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r == '_') {
			return ""
		}
	}
	return s
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	params, _ := in["params"].(map[string]any)
	body, _ := in["body"].(map[string]any)
	id := wm.Int(params["id"])
	no := func(status int, err string) map[string]any {
		return map[string]any{"ok": false, "status": status, "err": err, "device_id": 0, "text": "", "cid": "", "dog": ""}
	}
	if uid == 0 {
		return no(401, "unauthorized")
	}
	text := strings.TrimSpace(wm.Str(body["text"]))
	if text == "" {
		return no(422, "empty")
	}
	if len([]rune(text)) > 300 {
		return no(422, "text_too_long")
	}
	rows, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{{"column": "id", "op": "eq", "value": id}}, "limit": 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return no(500, "server_error")
	}
	if len(rows) != 1 || wm.Int(rows[0]["user_id"]) != uid || wm.Bool(rows[0]["revoked"]) {
		return no(404, "not_found")
	}
	return map[string]any{"ok": true, "status": 200, "err": "", "device_id": id, "text": text,
		"cid": clean(wm.Str(body["cid"]), 16), "dog": clean(wm.Str(body["dog"]), 16)}
}
