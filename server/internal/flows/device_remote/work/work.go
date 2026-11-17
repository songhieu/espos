// Workflow device_remote, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"time"
)

// The S3's remote control over the internet (s3desk/src/remote.cpp, docs/remote.md). The device calls this while its
// Settings › Điều khiển từ xa is on: every few seconds when nobody watches, back to back while a browser does
// (remote_view keeps watch_until ahead). Body: on (the switch), frame (JPEG, base64) + w, h when the screen changed,
// ack (the last gesture id it has done). Reply: watch (send frames), the gestures still to do.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	dev := wm.Int(in["device_id"])
	if dev == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "unauthorized"}}
	}
	body, _ := in["body"].(map[string]any)
	now := time.Now().UnixMilli()
	rows, err := wm.QueryOpts("remote_sessions", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": dev}}, "limit": 1,
	})
	if err != nil {
		return wm.Fail(err)
	}
	var row map[string]any
	if len(rows) > 0 {
		row = rows[0]
	}
	on := wm.Bool(body["on"])
	upd := map[string]any{"device_id": dev, "user_id": wm.Int(in["user_id"]), "enabled": on, "seen_at": now}
	if f := wm.Str(body["frame"]); on && f != "" && len(f) <= 200000 {
		upd["frame"] = f
		upd["frame_at"] = now
		upd["w"] = wm.Int(body["w"])
		upd["h"] = wm.Int(body["h"])
		upd["seq"] = wm.Int(row["seq"]) + 1
	}
	if !on {
		upd["frame"] = ""
		upd["watch_until"] = 0
	}
	if _, err := wm.Upsert("remote_sessions", []string{"device_id"}, upd, nil); err != nil {
		return wm.Fail(err)
	}
	evs, err := wm.QueryOpts("remote_events", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": dev}}, "order_by": "id", "limit": 60,
	})
	if err != nil {
		return wm.Fail(err)
	}
	ack := wm.Int(body["ack"])
	todo := []any{}
	for _, e := range evs {
		id := wm.Int(e["id"])
		if !on || id <= ack {
			if err := wm.Delete("remote_events", id); err != nil {
				return wm.Fail(err)
			}
			continue
		}
		todo = append(todo, map[string]any{"id": id, "ev": wm.Str(e["ev"])})
	}
	watch := on && int64(wm.Int(row["watch_until"])) > now
	return map[string]any{"status": 200, "body": map[string]any{"watch": watch, "events": todo}}
}
