// Workflow device_sync, node upk (imported by tools/importer).
package upk

import (
	"encoding/json"
	"espos/internal/wm"
	"strings"
	"time"
)

// The owner's own Java games (user_pkgs, uploaded on the web's "Game Java" tab), for a colour desk only: the rows
// for the answer's user_pkgs, and the ones whose signed download URLs are missing or end within 3 days (the next
// nodes sign those again, for 7 days, and keep them on the row). An X4 gets nothing. A failed read leaves
// "user_pkgs" out of this answer rather than failing the sync.
const renewMs = 3 * 24 * 3600 * 1000

func kindOf(row map[string]any, fw string) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(fw, "espos-s3-") {
		return "s3"
	}
	if strings.HasPrefix(fw, "espos-bot-") {
		return "bot"
	}
	return "x4"
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	deviceID := wm.Int(in["device_id"])
	userID := wm.Int(in["user_id"])
	none := map[string]any{"s3": false, "rows": []any{}, "todo": []any{}}
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1})
	if err != nil {
		wm.Log("devices: ", err.Error())
		return none
	}
	dev := map[string]any{}
	if len(devs) == 1 {
		dev = devs[0]
	}
	if userID == 0 || kindOf(dev, wm.Str(in["fw"])) != "s3" {
		return none
	}
	rows, qerr := wm.QueryOpts("user_pkgs", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": userID}},
		"order_by": "id", "limit": 30,
	})
	if qerr != nil {
		wm.Log("user_pkgs: ", qerr.Error())
		return none // no "user_pkgs" in this answer (unknown), rather than an empty list
	}
	now := time.Now().UnixMilli()
	out := []any{}
	todo := []any{}
	for _, r := range rows {
		meta, _ := r["meta"].(map[string]any)
		if s, isStr := r["meta"].(string); isStr && s != "" {
			_ = json.Unmarshal([]byte(s), &meta)
		}
		id := wm.Int(r["id"])
		key := wm.Str(r["file_key"])
		iconKey := wm.Str(r["icon_key"])
		url := wm.Str(r["url"])
		if url == "" || int64(wm.Num(r["url_exp_ms"])) < now+renewMs {
			url = ""
			sign := iconKey
			if sign == "" {
				sign = key // no icon: its URL is signed for nothing and dropped
			}
			todo = append(todo, map[string]any{"id": id, "key": key, "icon_key": sign, "has_icon": iconKey != ""})
		}
		out = append(out, map[string]any{
			"id": id, "pkg_id": wm.Str(r["pkg_id"]), "name": wm.Str(r["name"]), "version": wm.Str(r["version"]),
			"code": wm.Int(r["code"]), "size": wm.Int(r["size"]), "sha256": wm.Str(r["sha256"]), "url": url,
			"icon_url": wm.Str(r["icon_url"]), "has_icon": iconKey != "", "meta": meta,
		})
	}
	return map[string]any{"s3": true, "rows": out, "todo": todo}
}
