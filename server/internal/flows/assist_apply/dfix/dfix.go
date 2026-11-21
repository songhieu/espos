// Workflow assist_apply, node dfix (imported by tools/importer).
package dfix

import (
	"espos/internal/wm"
	"strings"
	"unicode"
)

// After the computer commands (EspOS Desktop, s3desk/docs/desktop-protocol.md §4): the reply is fixed from what
// really happened, in the dog's own words (assist_reply's lines):
//   all done                -> the model's line stands (it was written as success)
//   one refused or failed   -> the start of the dog's "oops" line and the reason ("Ủa, chú làm hông được rồi bây:
//                              máy tính đang tắt hay chưa mở EspOS Desktop.")
//   one waiting for a yes   -> "... Bấm xác nhận trên máy tính hay trên web thì máy mới làm.", and "done" gets
//                              "chờ bạn xác nhận: ..." so the S3 syncs at once (its note arrives)
//   one not run in 2 s      -> "... gửi lệnh qua máy tính rồi."
// body.desk_todo is taken out; body.desk lists each command's outcome. The turn kept in assist_convos is corrected
// too, so the voice says the same.

var reasons = map[string]string{
	"desktop_disabled":   "tính năng điều khiển máy tính đang tạm dừng",
	"no_desktop":         "chưa có máy tính nào được ghép với tài khoản",
	"desktop_offline":    "máy tính đang tắt hay chưa mở EspOS Desktop",
	"desktop_view_only":  "máy tính đang để chế độ chỉ xem",
	"command_off":        "lệnh này đang tắt trên máy tính",
	"unknown_command":    "máy tính chưa có lệnh đó",
	"bad_args":           "lệnh chưa đủ rõ",
	"sender_not_allowed": "máy tính chưa cho máy này ra lệnh",
	"rate_limited":       "ra lệnh nhanh quá, đợi chút rồi thử lại",
	"expired":            "máy tính không nhận lệnh kịp",
	"cancelled":          "lệnh đã bị hủy",
	"denied":             "lệnh đã bị từ chối trên máy tính",
	"not_installed":      "không thấy app đó trên máy tính",
	"not_found":          "không tìm thấy thứ đó trên máy tính",
	"not_allowed":        "lệnh này đang tắt trên máy tính",
	"busy":               "máy tính đang bận",
	"timeout":            "máy tính làm không kịp",
}

// why: a refusal code or the Mac's error, as words. The Mac's own sentence is used when it reads like one.
func why(code string) string {
	c := strings.TrimSpace(code)
	if r, ok := reasons[c]; ok {
		return r
	}
	nonASCII := false
	for _, r := range c {
		if r > 127 && unicode.IsLetter(r) {
			nonASCII = true
		}
	}
	if nonASCII && strings.Contains(c, " ") {
		r := []rune(strings.Join(strings.Fields(c), " "))
		if len(r) > 80 {
			r = r[:80]
		}
		return strings.TrimRight(string(r), " .,;:")
	}
	if c == "" || c == "server_error" {
		return "máy chủ đang trục trặc"
	}
	return "máy tính báo lỗi"
}

func speech(s string) string {
	up := 0
	letters := 0
	for _, r := range s {
		if unicode.IsLetter(r) {
			letters++
			if unicode.IsUpper(r) {
				up++
			}
		}
	}
	if letters < 8 || up*10 < letters*6 {
		return s
	}
	r := []rune(strings.ToLower(s))
	start := true
	for i, c := range r {
		if start && unicode.IsLetter(c) {
			r[i] = unicode.ToUpper(c)
			start = false
		}
		if c == '.' || c == '!' || c == '?' {
			start = true
		}
	}
	return string(r)
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	body, _ := in["body"].(map[string]any)
	if body == nil {
		body = map[string]any{}
	}
	delete(body, "desk_todo")
	results, _ := in["results"].([]any)
	if wm.Int(in["status"]) != 200 || len(results) == 0 {
		return map[string]any{"body": body}
	}
	lines, _ := in["lines"].(map[string]any)
	items, _ := in["items"].([]any)
	outcome := []any{}
	failWhy, confirmWhat, where := "", []string{}, ""
	sent := false
	for i, x := range results {
		r, _ := x.(map[string]any)
		cmd := ""
		if i < len(items) {
			it, _ := items[i].(map[string]any)
			cmd = wm.Str(it["cmd"])
		}
		state := wm.Str(r["state"])
		o := map[string]any{"cmd": cmd, "id": wm.Str(r["id"]), "state": state}
		switch {
		case !wm.Bool(r["ok"]):
			o["state"] = "refused"
			o["error"] = wm.Str(r["error"])
			if failWhy == "" {
				failWhy = why(wm.Str(r["error"]))
			}
		case state == "failed":
			o["err"] = wm.Str(r["err"])
			if failWhy == "" {
				failWhy = why(wm.Str(r["err"]))
			}
		case state == "expired" || state == "cancelled":
			if failWhy == "" {
				failWhy = why(state)
			}
		case state == "confirm":
			confirmWhat = append(confirmWhat, wm.Str(r["what"]))
			if where == "" {
				where = wm.Str(r["where"])
			}
		case state == "done":
			o["text"] = wm.Str(r["text"])
		default: // queued or sent: not run within 2 s
			sent = true
		}
		outcome = append(outcome, o)
	}
	body["desk"] = outcome
	reply := ""
	switch {
	case failWhy != "":
		head := strings.TrimSpace(wm.Str(lines["fail"]))
		if head == "" {
			head = "Chưa làm được"
		}
		reply = head + ": " + failWhy + "."
		if wm.Bool(lines["shout"]) {
			reply = strings.ToUpper(reply)
		}
	case len(confirmWhat) > 0:
		reply = wm.Str(lines["confirm"])
		if l := wm.Str(lines["confirm_"+where]); where != "either" && l != "" {
			reply = l // the Mac takes the yes in one place only (its confirm_at)
		}
		done, _ := body["done"].([]any)
		for _, w := range confirmWhat {
			done = append(done, "chờ bạn xác nhận: "+w)
		}
		body["done"] = done
	case sent:
		reply = wm.Str(lines["sent"])
	}
	if reply == "" {
		return map[string]any{"body": body} // all done: the model's line stands
	}
	body["reply"] = reply
	rows, err := wm.QueryOpts("assist_convos", map[string]any{"filters": []map[string]any{
		{"column": "cid", "op": "eq", "value": wm.Str(body["cid"])}, {"column": "device_id", "op": "eq", "value": wm.Int(in["device_id"])}}, "limit": 1})
	if err == nil && len(rows) == 1 {
		turns, _ := rows[0]["turns"].([]any)
		for _, x := range turns {
			t, _ := x.(map[string]any)
			if t != nil && wm.Int(t["k"]) == wm.Int(body["n"]) {
				t["a"] = reply
				t["s"] = speech(reply)
			}
		}
		if _, uerr := wm.Update("assist_convos", wm.Int(rows[0]["id"]), map[string]any{"turns": turns}); uerr != nil {
			wm.Log("turn: ", uerr.Error())
		}
	} else if err != nil {
		wm.Log("turn: ", err.Error())
	}
	return map[string]any{"body": body}
}
