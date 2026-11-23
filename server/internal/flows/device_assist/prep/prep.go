// Workflow device_assist, node prep (imported by tools/importer).
package prep

import (
	"encoding/base64"
	"encoding/json"
	"espos/internal/wm"
	"strings"
)

// The voice in the same answer (?stream=1): what the TTS reads and the turn for the X-Assist header. The words come
// from the turn as kept (assist_reply wrote it, assist_apply corrected it when a change was refused), as GET /say
// reads them. X-Assist is the answer's JSON (the same fields the JSON answer has) in base64: header values are
// ASCII. On any doubt (no turn, no words) ok is false and the JSON answer goes out instead.

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
	answer, _ := in["answer"].(map[string]any)
	deviceID := wm.Int(in["device_id"])
	cid := wm.Str(answer["cid"])
	n := wm.Int(answer["n"])
	no := map[string]any{"ok": false, "text": "", "voice": "", "model": "vieneu-turbo", "meta": "", "rate": wm.Int(in["rate"])}
	if deviceID == 0 || len(cid) != 16 || n < 1 {
		return no
	}
	rows, err := wm.QueryOpts("assist_convos", map[string]any{"filters": []map[string]any{
		{"column": "cid", "op": "eq", "value": cid}, {"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return no
	}
	if len(rows) != 1 {
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
		if text == "" || voice == "" {
			return no
		}
		meta := map[string]any{}
		for _, k := range []string{"heard", "reply", "mood", "topic", "cid", "n", "dog", "dog_name", "voice", "done", "saved",
			"error", "asks_confirm", "asks", "name_saved", "remembered", "forgot", "ms"} {
			if v, ok := answer[k]; ok && v != nil {
				meta[k] = v
			}
		}
		b, _ := json.Marshal(meta)
		model, tv := ttsOf(voice)
		return map[string]any{"ok": true, "text": text, "voice": tv, "model": model, "rate": wm.Int(in["rate"]),
			"meta": base64.StdEncoding.EncodeToString(b)}
	}
	return no
}
