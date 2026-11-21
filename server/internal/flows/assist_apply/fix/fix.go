// Workflow assist_apply, node fix (imported by tools/importer).
package fix

import (
	"espos/internal/wm"
	"strings"
	"unicode"
)

// After the web's own save (node save calls put_device_settings, which save_device_settings calls too): when it took the
// changes the dog's answer stands ("saved": true, the device gets them at its next ping/sync as any web save);
// when it refused them the dog owns up in its own words (the turn kept in assist_convos is corrected too, so the
// voice says the same) and "error" carries the save's reason.

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
	if wm.Int(in["status"]) == 200 {
		body["saved"] = true
		return map[string]any{"status": 200, "body": body}
	}
	why := ""
	if a, ok := in["answer"].(map[string]any); ok {
		why = wm.Str(a["error"])
	}
	if why == "" {
		why = "server_error"
		wm.Log("save failed: ", wm.Str(in["error"]))
	}
	oops := wm.Str(in["oops"])
	if oops == "" {
		oops = "Chưa làm được. Làm trên web giùm nha."
	}
	body["saved"] = false
	body["error"] = why
	body["done"] = []any{}
	body["reply"] = oops
	// the turn as kept: its reply and speech become the apology
	rows, err := wm.QueryOpts("assist_convos", map[string]any{"filters": []map[string]any{
		{"column": "cid", "op": "eq", "value": wm.Str(body["cid"])}, {"column": "device_id", "op": "eq", "value": wm.Int(in["device_id"])}}, "limit": 1})
	if err == nil && len(rows) == 1 {
		turns, _ := rows[0]["turns"].([]any)
		for _, x := range turns {
			t, _ := x.(map[string]any)
			if t != nil && wm.Int(t["k"]) == wm.Int(body["n"]) {
				t["a"] = oops
				t["s"] = speech(oops)
			}
		}
		if _, uerr := wm.Update("assist_convos", wm.Int(rows[0]["id"]), map[string]any{"turns": turns}); uerr != nil {
			wm.Log("turn: ", uerr.Error())
		}
	} else if err != nil {
		wm.Log("turn: ", err.Error())
	}
	return map[string]any{"status": 200, "body": body}
}
