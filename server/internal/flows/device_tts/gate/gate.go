// Workflow device_tts, node gate (imported by tools/importer).
package gate

import (
	"espos/internal/wm"
	"time"
)

// The dog's voice is the paid layer (business model): a phrase is spoken only with the device's AI licence ("on") or
// an active trial ("trial"; not counted as a turn: no model call), else 402 {"error": "lic"} and the device shows the
// words or rings instead. A server that does not apply licences (self-hosted) lets it through. At most 60 a day per
// device (UTC day), on top of the route's 10 a minute: 429 {"error": "daily_cap"}.

const perDay = 60

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	dev := wm.Int(in["device_id"])
	lic, _ := in["lic"].(map[string]any)
	if wm.Bool(in["applies"]) {
		if ai := wm.Str(lic["ai"]); ai != "on" && ai != "trial" {
			locked := wm.Str(in["locked"])
			if locked == "" {
				locked = "no_ai"
			}
			return map[string]any{"ok": false, "status": 402, "body": map[string]any{"error": "lic", "locked": locked, "buy_url": wm.Str(in["buy_url"])}}
		}
	}
	day := time.Now().UTC().Format("2006-01-02")
	rows, err := wm.QueryOpts("dev_quota", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": dev}, {"column": "what", "op": "eq", "value": "tts"}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	n := 0
	if len(rows) == 1 && wm.Str(rows[0]["day"]) == day {
		n = wm.Int(rows[0]["n"])
	}
	if n >= perDay {
		return map[string]any{"ok": false, "status": 429, "body": map[string]any{"error": "daily_cap", "per_day": perDay}}
	}
	if _, uerr := wm.Upsert("dev_quota", []string{"device_id", "what"}, map[string]any{"device_id": dev, "what": "tts", "day": day, "n": n + 1}, nil); uerr != nil {
		return wm.Fail(uerr)
	}
	return map[string]any{"ok": true, "status": 200, "body": nil}
}
