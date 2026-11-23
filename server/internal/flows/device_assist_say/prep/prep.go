// Workflow device_assist_say, node prep (imported by tools/importer).
package prep

import (
	"espos/internal/wm"
	"strconv"
	"strings"
)

// GET /v1/device/assist/say?cid=<cid>&n=<turn>[&rate=16000][&format=json]: which words to speak, in which voice.
// The reply was saved by assist_reply with its speech text ("s": the reply as the TTS should read it) and the dog's
// voice ("v"). The same reading as say_device_dog/prep (the web's): change both.
//   n=last: the conversation's last turn; without cid, the last turn of this device's newest conversation (a device
//           whose streamed answer broke before its X-Assist header arrived finds its turn this way).
//   format=json: the turn as JSON {heard, reply, mood, cid, n, dog, dog_name} instead of its voice.

var rates = map[int]bool{8000: true, 11025: true, 16000: true, 22050: true, 24000: true, 32000: true, 44100: true, 48000: true}

var names = map[string]string{"bao": "Bao", "momo": "Momo", "boop": "Boop", "leaf": "Leaf", "volt": "Volt", "byte": "Byte",
	"shiba": "Rocket", "corgi": "Biscuit", "husky": "Frost", "golden": "Sunny"}

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
	deviceID := wm.Int(in["device_id"])
	q, _ := in["query"].(map[string]any)
	cid := strings.ToLower(strings.TrimSpace(wm.Str(q["cid"])))
	last := wm.Str(q["n"]) == "last"
	n, nerr := strconv.Atoi(wm.Str(q["n"]))
	rate := 16000
	if r, rerr := strconv.Atoi(wm.Str(q["rate"])); rerr == nil && rates[r] {
		rate = r
	}
	asJSON := wm.Str(q["format"]) == "json"
	no := map[string]any{"ok": false, "status": 404, "err": "not_found", "text": "", "voice": "", "model": "vieneu-turbo", "rate": rate, "json": false, "turn": nil}
	if deviceID == 0 || (cid != "" && len(cid) != 16) || (cid == "" && !last) || (!last && (nerr != nil || n < 1)) {
		no["status"] = 422
		no["err"] = "bad_request"
		return no
	}
	opts := map[string]any{"filters": []map[string]any{{"column": "cid", "op": "eq", "value": cid},
		{"column": "device_id", "op": "eq", "value": deviceID}}, "limit": 1}
	if cid == "" {
		opts = map[string]any{"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID},
			{"column": "source", "op": "eq", "value": "device"}}, "order_by": "last_ms", "desc": true, "limit": 1}
	}
	rows, err := wm.QueryOpts("assist_convos", opts)
	if err != nil {
		wm.Log("read: ", err.Error())
		no["status"] = 500
		no["err"] = "server_error"
		return no
	}
	if len(rows) != 1 {
		return no
	}
	turns, _ := rows[0]["turns"].([]any)
	if last && len(turns) > 0 {
		if t, ok := turns[len(turns)-1].(map[string]any); ok {
			n = wm.Int(t["k"])
		}
	}
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
		turn := map[string]any{"heard": wm.Str(t["u"]), "reply": wm.Str(t["a"]), "mood": wm.Str(t["m"]), "cid": wm.Str(rows[0]["cid"]),
			"n": n, "dog": wm.Str(t["d"]), "dog_name": names[wm.Str(t["d"])], "voice": voice}
		model, tv := ttsOf(voice)
		return map[string]any{"ok": true, "status": 200, "err": "", "text": text, "voice": tv, "model": model, "rate": rate, "json": asJSON, "turn": turn}
	}
	return no
}
