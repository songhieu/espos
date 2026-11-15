// Workflow device_sync, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"fmt"
	"inkdesk/internal/wm"
	"strconv"
	"strings"
	"time"
)

// Keys the device may read and change, with their defaults (docs/inkdesk-api.md).
var deviceDefaults = map[string]string{
	"sys.lang": "vi", "sys.sleep_min": "10",
	"read.font": "LexicaUltralegible", "read.size": "medium", "read.spacing": "1.4",
	"dash.enabled": "1", "dash.layout": "desk", "dash.interval_min": "30", "dash.clock": "1",
	"dash.charging_min": "1", "dash.save_pct": "20", "dash.quiet_from": "23:00", "dash.quiet_to": "06:00",
	"sleep.mode": "dashboard", "sleep.rotate": "sleep", "sleep.order": "seq", "sleep.clock": "1",
	"read.align": "justify", "read.margin": "narrow", "read.indent": "2", "read.para": "1", "read.hyphen": "0",
	"read.aa": "1", "read.refresh": "15", "read.status": "page,percent,battery,chapter", "read.images": "show",
	"sys.pwr_short": "ignore", "sys.side": "prev_next", "sys.front": "bclr", "sys.orient": "portrait", "sys.clock_fmt": "24",
	"read.goal_min": "30",
	"home.w":        "", "sleep.wl": "", "sleep.wp": "",
}

func list(v any) []any {
	a, _ := v.([]any)
	if len(a) > 50 {
		return a[:50]
	}
	return a
}

// "inkdesk-1.2.3" -> 10203 (0 when unreadable), as the device computes it.
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

func cut(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return string(r[:n])
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["body"].(map[string]any)
	dev, _ := in["device"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	deviceID := wm.Int(dev["device_id"])
	userID := wm.Int(dev["user_id"])
	if deviceID == 0 || userID == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	now := time.Now()
	nowMs := now.UnixMilli()

	// ---- settings: device changes win only if they are newer ----
	rows, err := wm.QueryOpts("device_settings", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 200,
	})
	if err != nil {
		return wm.Fail(err)
	}
	type cell struct {
		v  string
		ms int64
	}
	cur := map[string]cell{}
	webSet := map[string]bool{} // keys last changed on the web: they win on a device that never had them
	for _, r := range rows {
		cur[wm.Str(r["skey"])] = cell{wm.Str(r["svalue"]), int64(wm.Num(r["updated_ms"]))}
		webSet[wm.Str(r["skey"])] = wm.Str(r["source"]) == "web"
	}
	for _, it := range list(body["settings"]) {
		m, _ := it.(map[string]any)
		k := wm.Str(m["k"])
		if _, allowed := deviceDefaults[k]; !allowed {
			continue
		}
		v := cut(wm.Str(m["v"]), 60)
		if k == "home.w" || k == "sleep.wl" || k == "sleep.wp" {
			// Widget layouts are JSON up to 4000 bytes; a cut one would break the screen, so skip it instead.
			v = wm.Str(m["v"])
			var probe map[string]any
			if len(v) > 4000 || (v != "" && json.Unmarshal([]byte(v), &probe) != nil) {
				continue
			}
		}
		age := int64(wm.Num(m["age_s"]))
		if age < 0 {
			age = 0
		}
		ts := nowMs - age*1000
		if old, ok := cur[k]; ok && old.ms > ts {
			continue
		}
		if _, uerr := wm.Upsert("device_settings", []string{"device_id", "skey"}, map[string]any{
			"device_id": deviceID, "user_id": userID, "skey": k, "svalue": v, "updated_ms": ts, "source": "device",
		}, nil); uerr != nil {
			return wm.Fail(uerr)
		}
		cur[k] = cell{v, ts}
		webSet[k] = false
	}

	// ---- reading progress ----
	for _, it := range list(body["progress"]) {
		m, _ := it.(map[string]any)
		id := wm.Int(m["book_id"])
		p := wm.Int(m["percent"])
		if id <= 0 || p < 0 || p > 100 {
			continue
		}
		b, qerr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
			{"column": "id", "op": "eq", "value": id}, {"column": "user_id", "op": "eq", "value": userID}}, "limit": 1})
		if qerr != nil {
			return wm.Fail(qerr)
		}
		if len(b) == 1 {
			if _, uerr := wm.Update("books", id, map[string]any{"progress": p}); uerr != nil {
				return wm.Fail(uerr)
			}
		}
	}

	// ---- reading statistics: absolute per-day, per-book totals from the device ----
	for _, it := range list(body["reading"]) {
		m, _ := it.(map[string]any)
		day := wm.Str(m["d"])
		if len(day) != 10 || day[4] != '-' || day[7] != '-' {
			continue
		}
		key := cut(wm.Str(m["k"]), 160)
		sec, pages := wm.Int(m["s"]), wm.Int(m["p"])
		if sec <= 0 || sec > 86400 || pages < 0 || pages > 20000 {
			continue
		}
		ex, qerr := wm.QueryOpts("reading_days", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "day", "op": "eq", "value": day},
			{"column": "book_key", "op": "eq", "value": key}}, "limit": 1})
		if qerr != nil {
			return wm.Fail(qerr)
		}
		title := cut(wm.Str(m["t"]), 120)
		if len(ex) == 1 {
			// Totals only grow: a reader that lost its log must not erase what is here.
			oldSec, oldPages := wm.Int(ex[0]["seconds"]), wm.Int(ex[0]["pages"])
			if sec > oldSec || pages > oldPages {
				if sec < oldSec {
					sec = oldSec
				}
				if pages < oldPages {
					pages = oldPages
				}
				if _, uerr := wm.Update("reading_days", wm.Int(ex[0]["id"]), map[string]any{"seconds": sec, "pages": pages, "title": title}); uerr != nil {
					return wm.Fail(uerr)
				}
			}
			continue
		}
		if _, ierr := wm.Insert("reading_days", map[string]any{
			"user_id": userID, "device_id": deviceID, "day": day, "book_key": key, "book_id": wm.Int(m["id"]),
			"title": title, "seconds": sec, "pages": pages,
		}); ierr != nil {
			return wm.Fail(ierr)
		}
	}

	// ---- device heartbeat ----
	upd := map[string]any{"last_sync": now.UTC().Format(time.RFC3339)}

	// ---- firmware: the device's install reports, and the update it should get ----
	devRows, drerr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1})
	if drerr != nil {
		return wm.Fail(drerr)
	}
	fwReq, fwAuto := 0, false
	if len(devRows) == 1 {
		fwReq = wm.Int(devRows[0]["fw_req"])
		fwAuto = devRows[0]["fw_auto"] == true
	}
	runningCode := versionCode(wm.Str(body["fw"]))
	reported := false
	for _, it := range list(body["ota"]) {
		m, _ := it.(map[string]any)
		ok := m["ok"] == true
		state := "failed"
		if ok {
			state = "ok"
		}
		at := now.Add(-time.Duration(wm.Int(m["age_s"])) * time.Second).UTC().Format(time.RFC3339)
		upd["ota_state"] = map[string]any{"state": state, "version": cut(wm.Str(m["version"]), 40), "why": cut(wm.Str(m["why"]), 60), "at": at}
		// Success or failure, this request is over: the web shows the result (and offers a retry).
		upd["fw_req"] = 0
		fwReq = 0
		reported = true
	}
	if fwReq > 0 && runningCode >= fwReq {
		// Already running the requested version (its report got lost).
		upd["fw_req"] = 0
		fwReq = 0
		if !reported {
			upd["ota_state"] = map[string]any{"state": "ok", "version": cut(wm.Str(body["fw"]), 40), "at": now.UTC().Format(time.RFC3339)}
		}
	}
	var fwUpdate map[string]any
	latestFw, lferr := wm.QueryOpts("firmware", map[string]any{"order_by": "code", "desc": true, "limit": 1})
	if lferr != nil {
		return wm.Fail(lferr)
	}
	if len(latestFw) == 1 && runningCode > 0 && wm.Int(latestFw[0]["code"]) > runningCode {
		f := latestFw[0]
		code := wm.Int(f["code"])
		fwUpdate = map[string]any{
			"version": wm.Str(f["version"]), "url": wm.AbsURL(wm.Str(in["host"]), wm.Str(f["url"])), "sha256": wm.Str(f["sha256"]), "size": wm.Int(f["size"]),
			"notes": wm.Str(f["notes"]), "date": wm.Str(f["released"]),
			// Install now: asked on the web, or auto-update while the reader is charging.
			"install": (fwReq > 0 && fwReq >= code) || (fwAuto && body["usb"] == true),
		}
	}
	if b := wm.Int(body["battery"]); b > 0 && b <= 100 {
		upd["battery"] = b
	}
	if fw := cut(wm.Str(body["fw"]), 40); fw != "" {
		upd["fw"] = fw
	}
	// Reading fonts on the SD card (names only), so the web can show what is installed.
	if fl, ok := body["fonts"].([]any); ok {
		names := []any{}
		for _, f := range fl {
			if n := cut(wm.Str(f), 40); n != "" && len(names) < 40 {
				names = append(names, n)
			}
		}
		upd["fonts"] = names
	}
	// WiFi networks the device has saved, with its last result for each (names only).
	if wf, ok := body["wifi"].(map[string]any); ok {
		saved := []any{}
		for _, it := range list(wf["saved"]) {
			m, _ := it.(map[string]any)
			ssid := cut(wm.Str(m["ssid"]), 32)
			if ssid == "" || len(saved) == 10 {
				continue
			}
			saved = append(saved, map[string]any{"ssid": ssid, "ok": m["ok"] == true, "code": wm.Int(m["code"]), "age_s": wm.Int(m["age_s"])})
		}
		upd["wifi_state"] = saved
		// Acknowledged changes: the password leaves the server here.
		for _, it := range list(wf["acks"]) {
			m, _ := it.(map[string]any)
			id := wm.Int(m["id"])
			ops, qerr := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
				{"column": "id", "op": "eq", "value": id}, {"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1})
			if qerr != nil {
				return wm.Fail(qerr)
			}
			if len(ops) != 1 {
				continue
			}
			status := "done"
			result := ""
			if m["ok"] != true {
				status = "failed"
				result = cut(wm.Str(m["why"]), 80)
			}
			if _, uerr := wm.Update("wifi_ops", id, map[string]any{"status": status, "result": result, "pass": ""}); uerr != nil {
				return wm.Fail(uerr)
			}
		}
	}
	if _, uerr := wm.Update("devices", deviceID, upd); uerr != nil {
		return wm.Fail(uerr)
	}

	// Battery history: voltage tells real drain apart from the percentage settling off the charger.
	if mv := wm.Int(body["battery_mv"]); mv > 2500 && mv < 4500 {
		if _, ierr := wm.Insert("battery_log", map[string]any{
			"device_id": deviceID, "user_id": userID, "mv": mv, "pct": wm.Int(body["battery"]), "usb": body["usb"] == true, "at_ms": nowMs,
		}); ierr != nil {
			return wm.Fail(ierr)
		}
	}

	// ---- answer: merged settings ----
	settings := map[string]any{}
	var rev int64
	for k, def := range deviceDefaults {
		settings[k] = def
		if c, ok := cur[k]; ok {
			settings[k] = c.v
			if c.ms > rev {
				rev = c.ms
			}
		}
	}

	// ---- answer: books ----
	have := map[int]bool{}
	for _, h := range list(body["have_books"]) {
		have[wm.Int(h)] = true
	}
	// Byte size of each book file on the card (firmware 0.3.2 and later). A matching size is the
	// proof of a full copy: only then is the stored original deleted, as Send to Kindle does.
	haveSize := map[int]int{}
	for _, h := range list(body["have_sizes"]) {
		m, _ := h.(map[string]any)
		if id := wm.Int(m["id"]); id > 0 {
			haveSize[id] = wm.Int(m["size"])
		}
	}
	host := wm.PublicBase(wm.Str(in["host"]))
	if host == "" {
		host = wm.PublicBase("")
	}
	brows, berr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": userID}, {"column": "status", "op": "eq", "value": "ready"}},
		"order_by": "id", "limit": 500})
	if berr != nil {
		return wm.Fail(berr)
	}
	booksOut := []any{}
	for _, r := range brows {
		id := wm.Int(r["id"])
		patch := map[string]any{}
		if (r["on_device"] == true) != have[id] {
			patch["on_device"] = have[id]
		}
		delivered := wm.Str(r["delivered_at"]) != ""
		if have[id] && !delivered && wm.Int(r["size"]) > 0 && haveSize[id] == wm.Int(r["size"]) {
			// A failed delete keeps the book undelivered, so the next sync tries again.
			if serr := wm.StorageDelete(wm.Str(r["file_key"])); serr == nil {
				patch["delivered_at"] = time.Now().UTC().Format(time.RFC3339)
				delivered = true
			}
		}
		if len(patch) > 0 {
			if _, uerr := wm.Update("books", id, patch); uerr != nil {
				return wm.Fail(uerr)
			}
		}
		var u any
		if !have[id] && !delivered {
			u = fmt.Sprintf("%s/v1/device/book/%d", host, id)
		}
		booksOut = append(booksOut, map[string]any{
			"id": id, "title": cut(wm.Str(r["title"]), 80), "file": wm.Str(r["file_name"]), "size": wm.Int(r["size"]), "url": u,
		})
	}

	// ---- answer: sleep-screen pictures (a URL only for the ones the device lacks) ----
	haveWall := map[int]bool{}
	for _, h := range list(body["have_walls"]) {
		haveWall[wm.Int(h)] = true
	}
	wrows, werr := wm.QueryOpts("wallpapers", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": userID}, {"column": "status", "op": "eq", "value": "ready"}},
		"order_by": "pos", "limit": 50})
	if werr != nil {
		return wm.Fail(werr)
	}
	wallsOut := []any{}
	for _, r := range wrows {
		id := wm.Int(r["id"])
		var u any
		if !haveWall[id] {
			u = fmt.Sprintf("%s/v1/device/wallpaper/%d", host, id)
		}
		wallsOut = append(wallsOut, map[string]any{"id": id, "orient": wm.Str(r["orient"]), "url": u})
	}

	// ---- answer: WiFi changes queued on the web ----
	opRows, oerr := wm.QueryOpts("wifi_ops", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": deviceID}, {"column": "status", "op": "eq", "value": "pending"}},
		"order_by": "id", "limit": 10})
	if oerr != nil {
		return wm.Fail(oerr)
	}
	wifiOut := []any{}
	for _, r := range opRows {
		wifiOut = append(wifiOut, map[string]any{"id": wm.Int(r["id"]), "op": wm.Str(r["op"]), "ssid": wm.Str(r["ssid"]), "pass": wm.Str(r["pass"])})
	}

	tzMin := 420
	if c, ok := cur["sys.tz_min"]; ok {
		if v := wm.Int(c.v); v >= -720 && v <= 840 && (v != 0 || c.v == "0") {
			tzMin = v
		}
	}
	webKeys := []any{}
	for k := range deviceDefaults {
		if webSet[k] {
			webKeys = append(webKeys, k)
		}
	}
	return map[string]any{"status": 200, "body": map[string]any{
		"now_ms": nowMs, "tz_min": tzMin,
		"device":       map[string]any{"id": deviceID, "name": wm.Str(dev["name"]), "user": wm.Str(dev["email"])},
		"settings_rev": rev, "settings": settings, "books": booksOut,
		"wallpapers": wallsOut, "wifi_ops": wifiOut, "web_keys": webKeys, "fw_update": fwUpdate,
	}}
}
