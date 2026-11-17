// Workflow remote_input, node work (imported by tools/importer).
package work

import (
	"inkdesk/internal/wm"
	"regexp"
)

// The gestures the device knows (s3desk/src/remote.cpp), in screen coordinates of its orientation.
var gesture = regexp.MustCompile(`^(tap \d{1,3} \d{1,3}|long \d{1,3} \d{1,3} \d{1,4}|drag \d{1,3} \d{1,3} \d{1,3} \d{1,3} \d{1,4}|key (home|back))$`)

// POST /v1/devices/:id/remote/input {events: ["tap 160 300", ...]}: the owner's browser; the device takes them at
// its next call of device_remote (back to back while it is watched).
func Run(wmIn, wmCtx map[string]any) map[string]any {
	uid := wm.Int(wmCtx["end_user_id"])
	if uid == 0 {
		uid = wm.Int(wmIn["uid"])
	}
	if uid == 0 {
		return map[string]any{"status": 401, "body": map[string]any{"error": "sign in"}}
	}
	in := wmIn
	p, _ := in["params"].(map[string]any)
	body, _ := in["body"].(map[string]any)
	dev := wm.Int(p["id"])
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": dev}, {"column": "user_id", "op": "eq", "value": uid},
		{"column": "revoked", "op": "eq", "value": false}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(devs) == 0 {
		return map[string]any{"status": 404, "body": map[string]any{"error": "no such device"}}
	}
	evs, _ := body["events"].([]any)
	if len(evs) == 0 || len(evs) > 20 {
		return map[string]any{"status": 400, "body": map[string]any{"error": "1 to 20 events"}}
	}
	n := 0
	for _, e := range evs {
		s := wm.Str(e)
		if !gesture.MatchString(s) {
			continue
		}
		if _, err := wm.Insert("remote_events", map[string]any{"device_id": dev, "ev": s}); err != nil {
			return wm.Fail(err)
		}
		n++
	}
	return map[string]any{"status": 200, "body": map[string]any{"queued": n}}
}
