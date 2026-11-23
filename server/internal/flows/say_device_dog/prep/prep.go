// Workflow say_device_dog, node prep (imported by tools/importer).
package prep

import (
	"espos/internal/wm"
	"strconv"
	"strings"
)

// GET /v1/devices/:id/assist/say?cid=<cid>&n=<turn>[&rate=24000][&voice=<slug>]: the web's copy of
// device_assist_say/prep (change both), for the signed-in owner of the device. ?voice= lets the owner hear a reply
// in another vieneu-turbo voice while choosing the dogs' voices (the list below is tts.espos.dev's, 2026-11-18).

var rates = map[int]bool{8000: true, 11025: true, 16000: true, 22050: true, 24000: true, 32000: true, 44100: true, 48000: true}

var voices = map[string]bool{"adam-bua": true, "truc-ly": true, "thien-minh": true, "mai-anh": true, "hai-dang": true,
	"thuy-dung": true, "thien-tam-duc": true, "ngoc-huyen": true, "quang-son": true, "ngoc-tran": true, "minh-duc": true,
	"pham-tuyen": true, "thai-son": true, "xuan-vinh": true, "thanh-binh": true, "ngoc-linh": true, "doan-trang": true,
	"thuc-doan": true, "minh-triet": true, "my-duyen": true, "quynh-anh": true, "duc-tri": true, "kim-thanh": true,
	"adam": true, "quoc-tuan": true}

// ttsOf: the TTS model and voice of a turn's voice ("v"): "piper:<voice>" is tts.espos.dev's piper model (a dog
// speaking another language than Vietnamese, assist_reply's dogVoices); any other is a vieneu-turbo voice, as before.
// The same in device_assist/prep, device_assist_say/prep and say_device_dog/prep: change all three.
func ttsOf(v string) (string, string) {
	if strings.HasPrefix(v, "piper:") {
		return "piper", strings.TrimPrefix(v, "piper:")
	}
	return "vieneu-turbo", v
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	params, _ := in["params"].(map[string]any)
	q, _ := in["query"].(map[string]any)
	id := wm.Int(params["id"])
	cid := strings.ToLower(strings.TrimSpace(wm.Str(q["cid"])))
	n, nerr := strconv.Atoi(wm.Str(q["n"]))
	rate := 24000
	if r, rerr := strconv.Atoi(wm.Str(q["rate"])); rerr == nil && rates[r] {
		rate = r
	}
	no := map[string]any{"ok": false, "status": 404, "err": "not_found", "text": "", "voice": "", "model": "vieneu-turbo", "rate": rate}
	if uid == 0 || id == 0 || len(cid) != 16 || nerr != nil || n < 1 {
		no["status"] = 422
		no["err"] = "bad_request"
		return no
	}
	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "id", "op": "eq", "value": id}}, "limit": 1, "with_total": false},
		{"table": "assist_convos", "filters": []map[string]any{{"column": "cid", "op": "eq", "value": cid},
			{"column": "device_id", "op": "eq", "value": id}}, "limit": 1, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		no["status"] = 500
		no["err"] = "server_error"
		return no
	}
	devs := rowsOf(res, 0)
	if len(devs) != 1 || wm.Int(devs[0]["user_id"]) != uid {
		return no
	}
	rows := rowsOf(res, 1)
	if len(rows) != 1 || wm.Int(rows[0]["user_id"]) != uid {
		return no
	}
	turns, _ := rows[0]["turns"].([]any)
	for _, x := range turns {
		t, _ := x.(map[string]any)
		if t == nil || wm.Int(t["k"]) != n {
			continue
		}
		text := wm.Str(t["s"])
		if text == "" {
			text = wm.Str(t["a"])
		}
		voice := wm.Str(t["v"])
		if v := wm.Str(q["voice"]); voices[v] {
			voice = v
		}
		if text == "" || voice == "" {
			return no
		}
		model, tv := ttsOf(voice)
		return map[string]any{"ok": true, "status": 200, "err": "", "text": text, "voice": tv, "model": model, "rate": rate}
	}
	return no
}

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
