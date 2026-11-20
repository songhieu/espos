// Workflow device_ping, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"strconv"
	"strings"
	"time"
)

// GET /v1/device/ping (2026-11-17): a linked device asks every minute whether the web changed something since its
// last sync, so a change made on the web reaches it within about a minute instead of at its next sync (15 min).
// Read only, one round trip. The answer:
//   rev: the settings_rev device_sync would answer now (the device syncs when it is above the one it holds)
//   fw:  true when the web asked for an install (or withdrew one), or a newer release for its board came out, after
//        the device's last sync
//
// rev MUST be computed exactly as device_sync/nodes/work.go computes settings_rev (its "answer: merged settings"
// part), or a device would sync for nothing or miss a change: the same keys (deviceDefaults and s3Shared there),
// the same kind rules, news_prefs and the devices row's name_ms and fw_auto_ms for a colour desk. Change both.
// scripts/check-ping-keys.py checks the two key lists against each other.

// device_sync's deviceDefaults keys.
var syncKeys = []string{
	"sys.lang", "sys.sleep_min",
	"read.font", "read.size", "read.spacing",
	"dash.enabled", "dash.layout", "dash.interval_min", "dash.clock",
	"dash.charging_min", "dash.save_pct", "dash.quiet_from", "dash.quiet_to",
	"sleep.mode", "sleep.rotate", "sleep.order", "sleep.clock",
	"read.align", "read.margin", "read.indent", "read.para", "read.hyphen",
	"read.aa", "read.refresh", "read.status", "read.images",
	"sys.pwr_short", "sys.side", "sys.front", "sys.orient", "sys.clock_fmt",
	"read.goal_min",
	"home.w", "sleep.wl", "sleep.wp",
	"s3.p1", "s3.p2", "s3.l1", "s3.l2",
	"s3.bright", "s3.volume", "s3.rot", "s3.saver", "s3.click", "s3.dnd",
	"s3.saver_mode",
	"s3.home", "s3.mywidgets", "s3.notes", "s3.alarms", "s3.todo", "s3.apps",
	"s3.theme", "s3.theme_auto", "s3.wall", "s3.cities",
	"s3.photos",
	"s3.pkgs", "s3.notif",
	"s3.notif_preview",
	"s3.saver_pages",
	"s3.markets",
	"s3.pet_dog", "s3.assist",
	"s3.home_l",
	"x4.pkgs",
}

// device_sync's s3Shared keys: the web's own settings a colour desk shows and changes too.
var sharedKeys = []string{"dash.city", "dash.lat", "dash.lon", "dash.ics_url", "sys.tz_min"}

// The EspOS Bot's keys: device_sync's botDefaults and botShared (scripts/check-bot-keys.py checks the two match).
var botPingKeys = []string{"bot.style", "bot.screens", "bot.bright", "bot.night", "bot.dog", "bot.wake", "bot.follow", "bot.voice", "bot.volume",
	"bot.alarms", "bot.ota", "sys.lang", "dash.city", "dash.lat", "dash.lon", "sys.tz_min"}

// kindOf: the board stored on the row, else told by the firmware it last reported (device_sync's kindOf, which
// reads the firmware name from the request; a device that synced once has its kind stored).
func kindOf(row map[string]any) string {
	if k := wm.Str(row["kind"]); k != "" {
		return k
	}
	if strings.HasPrefix(wm.Str(row["fw"]), "espos-s3-") {
		return "s3"
	}
	if strings.HasPrefix(wm.Str(row["fw"]), "espos-bot-") {
		return "bot"
	}
	return "x4"
}

// "espos-s3-1.2.3" -> 10203 (0 when unreadable), as device_sync and the device compute it.
func versionCode(v string) int {
	if i := strings.LastIndex(v, "-"); i >= 0 {
		v = v[i+1:]
	}
	p := strings.Split(v, ".")
	if len(p) < 2 {
		return 0
	}
	n := 0
	for i := 0; i < 3; i++ {
		x := 0
		if i < len(p) {
			x, _ = strconv.Atoi(p[i])
		}
		n = n*100 + x
	}
	return n
}

// ms reads an RFC3339 time (devices.last_sync, ota_state.at, a row's created_at); 0 when empty or unreadable.
func ms(v any) int64 {
	s := strings.TrimSpace(wm.Str(v))
	if s == "" {
		return 0
	}
	t, err := time.Parse(time.RFC3339Nano, s)
	if err != nil {
		return 0
	}
	return t.UnixMilli()
}

// rowsOf takes one answer of wm.QueryMany ({"rows", "total", "next"}).
func rowsOf(res []map[string]any, i int) []map[string]any {
	if i >= len(res) {
		return nil
	}
	switch r := res[i]["rows"].(type) {
	case []map[string]any:
		return r
	case []any:
		out := []map[string]any{}
		for _, x := range r {
			if m, ok := x.(map[string]any); ok {
				out = append(out, m)
			}
		}
		return out
	}
	return nil
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	dev, _ := wmIn["device"].(map[string]any)
	deviceID := wm.Int(dev["device_id"])
	userID := wm.Int(dev["user_id"])
	if deviceID == 0 || userID == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	// One round trip: the device's row, its settings, the user's news settings, the releases.
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1, "with_total": false},
		{"table": "device_settings", "filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 200, "with_total": false},
		{"table": "news_prefs", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}}, "limit": 1, "with_total": false},
		{"table": "firmware", "order_by": "code", "desc": true, "limit": 50, "with_total": false},
	})
	if err != nil {
		return wm.Fail(err)
	}
	devRows := rowsOf(res, 0)
	if len(devRows) != 1 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	row := devRows[0]
	kind := kindOf(row)

	// ---- rev: as device_sync's settings_rev ----
	at := map[string]int64{}
	for _, r := range rowsOf(res, 1) {
		at[wm.Str(r["skey"])] = int64(wm.Num(r["updated_ms"]))
	}
	var rev int64
	take := func(v int64) {
		if v > rev {
			rev = v
		}
	}
	if kind == "bot" {
		// The EspOS Bot's keys only (device_sync answers a bot exactly these): scripts/check-bot-keys.py.
		for _, k := range botPingKeys {
			take(at[k])
		}
	}
	for _, k := range syncKeys {
		if kind == "bot" {
			break
		}
		if strings.HasPrefix(k, "s3.") && kind != "s3" {
			continue // the colour desk's keys go to the colour desk only
		}
		if strings.HasPrefix(k, "x4.") && kind == "s3" {
			continue // and the X4's to the X4 only
		}
		take(at[k])
	}
	if kind == "s3" {
		for _, k := range sharedKeys {
			take(at[k])
		}
		if np := rowsOf(res, 2); len(np) == 1 {
			take(int64(wm.Num(np[0]["updated_ms"])))
		}
		take(int64(wm.Num(row["name_ms"])))
		take(int64(wm.Num(row["fw_auto_ms"])))
	}

	// The licence's newest change (hosted only: the guard admin_lic; device_sync raises settings_rev the same way).
	if g, ok := wmIn["guards"].(map[string]any); ok {
		if l, isMap := g["admin_lic"].(map[string]any); isMap && l["lic"] != nil {
			take(int64(wm.Num(l["lic_ms"])))
		}
	}

	// ---- fw: firmware news the device has not heard yet (its last sync came before it) ----
	lastSync := ms(row["last_sync"])
	running := versionCode(wm.Str(row["fw"]))
	fw := false
	st, _ := row["ota_state"].(map[string]any)
	if s, isStr := row["ota_state"].(string); isStr && s != "" {
		_ = json.Unmarshal([]byte(s), &st)
	}
	// Written by the web only (device_firmware): "requested" with its time, or "" with the time of a cancel.
	// device_sync writes "ok" / "failed", and "" without a time.
	if st != nil && ms(st["at"]) >= lastSync && ms(st["at"]) > 0 {
		switch wm.Str(st["state"]) {
		case "requested":
			fw = wm.Int(row["fw_req"]) > running
		case "":
			fw = true
		}
	}
	// The newest release for this board (empty kind = x4), newer than the running version, out since that sync.
	if running > 0 {
		for _, f := range rowsOf(res, 3) {
			fk := wm.Str(f["kind"])
			if fk == "" {
				fk = "x4"
			}
			if fk != kind {
				continue
			}
			if wm.Int(f["code"]) > running && ms(f["created_at"]) >= lastSync {
				fw = true
			}
			break
		}
	}
	return map[string]any{"status": 200, "body": map[string]any{"rev": rev, "fw": fw}}
}
