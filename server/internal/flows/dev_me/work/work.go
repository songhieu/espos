// Workflow dev_me, node work (imported by tools/importer).
package work

import (
	"espos/internal/wm"
	"strings"
)

// The account behind the developer key the guard checked: its email (as its devices know it), the key's name, its S3
// devices (what `espos upload --device` takes) and the limits of a private upload.
func kindOf(row map[string]any) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(wm.Str(row["fw"]), "espos-s3-") {
		return "s3"
	}
	return "x4"
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	d, _ := wmIn["dev"].(map[string]any)
	uid := wm.Int(d["user_id"])
	if uid <= 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "Khoá nhà phát triển không đúng hoặc đã bị thu hồi", "code": "bad_key"}}
	}
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": uid}, {"column": "revoked", "op": "eq", "value": false}}, "order_by": "id", "limit": 50})
	if err != nil {
		return wm.Fail(err)
	}
	email := ""
	list := []any{}
	for _, r := range devs {
		if email == "" {
			email = wm.Str(r["user_email"])
		}
		if kindOf(r) != "s3" {
			continue
		}
		list = append(list, map[string]any{"id": wm.Int(r["id"]), "name": wm.Str(r["name"]), "fw": wm.Str(r["fw"]), "last_sync": wm.Str(r["last_sync"])})
	}
	n, cerr := wm.QueryOpts("user_pkgs", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}}, "limit": 31})
	if cerr != nil {
		return wm.Fail(cerr)
	}
	return map[string]any{"status": 200, "body": map[string]any{
		"user_id": uid, "email": email, "key": map[string]any{"id": wm.Int(d["key_id"]), "label": wm.Str(d["label"])},
		"devices": list, "pkgs": len(n), "limits": map[string]any{"pkgs": 30, "bytes": 4194304, "jar_bytes": 2097152, "keys": 5},
	}}
}
