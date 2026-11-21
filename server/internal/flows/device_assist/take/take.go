// Workflow device_assist, node take (imported by tools/importer).
package take

import (
	"encoding/base64"
	"encoding/binary"
	"espos/internal/wm"
	"strconv"
	"strings"
)

// POST /v1/device/assist (s3desk/docs/assistant-ai.md): what the person said, as the device sends it.
//   - audio: the body is the recording itself: a WAV (16-bit PCM, mono, 8-48 kHz; 16 kHz is what the device
//     records) or raw 16-bit little-endian mono PCM (Content-Type audio/L16 or application/octet-stream, the rate
//     in ?rate=, default 16000). 0.3 to 20 seconds. ?cid= continues a conversation, ?dog= names the dog, ?wake=1 says
//     the turn began with the wake word ("gâu gâu"): nothing heard after it = no answer, no turn kept. ?stream=1 (or
//     Accept: audio/pcm): the answer is the reply's voice, PCM at ?say_rate= (16000), the turn in the X-Assist header.
//   - text (tests, a keyboard, the pop-up's chips): JSON {"text": "…", "cid": "…", "dog": "…", "intent": "news"}; intent
//     "news" (optional) asks for the news summary whatever the words.
// The bytes come from the staged upload (wm.FileBytes) or, when the request carried none, from the raw body.
// Out: {ok, text, audio (a clean WAV, base64), cid, dog} or {ok: false, status, err}.

const maxSec = 20

var sayRates = map[int]bool{8000: true, 11025: true, 16000: true, 22050: true, 24000: true, 32000: true, 44100: true, 48000: true}

func bad(status int, err string) map[string]any {
	return map[string]any{"ok": false, "status": status, "err": err, "text": "", "audio": "", "cid": "", "dog": "", "wake": false,
		"stream": false, "say_rate": 16000, "intent": ""}
}

func wav(pcm []byte, rate int) []byte {
	h := make([]byte, 44, 44+len(pcm))
	copy(h[0:], "RIFF")
	binary.LittleEndian.PutUint32(h[4:], uint32(36+len(pcm)))
	copy(h[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(h[16:], 16)
	binary.LittleEndian.PutUint16(h[20:], 1)
	binary.LittleEndian.PutUint16(h[22:], 1)
	binary.LittleEndian.PutUint32(h[24:], uint32(rate))
	binary.LittleEndian.PutUint32(h[28:], uint32(rate*2))
	binary.LittleEndian.PutUint16(h[32:], 2)
	binary.LittleEndian.PutUint16(h[34:], 16)
	copy(h[36:], "data")
	binary.LittleEndian.PutUint32(h[40:], uint32(len(pcm)))
	return append(h, pcm...)
}

// pcmOfWav finds the samples of a RIFF/WAVE file: PCM 16-bit mono only.
func pcmOfWav(b []byte) ([]byte, int, string) {
	if len(b) < 44 || string(b[0:4]) != "RIFF" || string(b[8:12]) != "WAVE" {
		return nil, 0, "bad_wav"
	}
	rate := 0
	pos := 12
	for pos+8 <= len(b) {
		id := string(b[pos : pos+4])
		size := int(binary.LittleEndian.Uint32(b[pos+4 : pos+8]))
		body := pos + 8
		if size < 0 || body+size > len(b) {
			size = len(b) - body // a recorder that never fixed its sizes: take what is there
		}
		switch id {
		case "fmt ":
			if size < 16 {
				return nil, 0, "bad_wav"
			}
			if binary.LittleEndian.Uint16(b[body:]) != 1 || binary.LittleEndian.Uint16(b[body+2:]) != 1 ||
				binary.LittleEndian.Uint16(b[body+14:]) != 16 {
				return nil, 0, "wav_not_pcm16_mono"
			}
			rate = int(binary.LittleEndian.Uint32(b[body+4:]))
		case "data":
			if rate == 0 {
				return nil, 0, "bad_wav"
			}
			return b[body : body+size], rate, ""
		}
		pos = body + size + size%2
	}
	return nil, 0, "bad_wav"
}

func clean(s string, n int) string {
	s = strings.ToLower(strings.TrimSpace(s))
	if len(s) > n {
		return ""
	}
	for _, r := range s {
		if !(r >= 'a' && r <= 'z' || r >= '0' && r <= '9' || r == '_') {
			return ""
		}
	}
	return s
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	query, _ := in["query"].(map[string]any)
	body, _ := in["body"].(map[string]any)
	files, _ := in["files"].([]any)
	cid := clean(wm.Str(query["cid"]), 16)
	dog := clean(wm.Str(query["dog"]), 16)
	// The voice in the same answer (?stream=1 or Accept: audio/pcm): the reply's PCM streams back with the turn in
	// the X-Assist header (device_assist/nodes/prep.go), saving the GET /say round trip.
	hdrs, _ := in["headers"].(map[string]any)
	// ?voice=0 (the EspOS Bot without a speaker): the JSON answer only, never the voice, whatever else asks for it.
	voiceOff := wm.Str(query["voice"]) == "0"
	stream := (wm.Str(query["stream"]) == "1" || strings.Contains(strings.ToLower(wm.Str(hdrs["accept"])), "audio/pcm")) && !voiceOff
	sayRate := 16000
	if r, rerr := strconv.Atoi(wm.Str(query["say_rate"])); rerr == nil && sayRates[r] {
		sayRate = r
	}
	raw64 := wm.Str(in["raw"])
	headers, _ := in["headers"].(map[string]any)
	ctype := strings.ToLower(wm.Str(headers["content-type"]))
	_, hasText := body["text"]
	binary := len(files) > 0 || (raw64 != "" && !hasText && !strings.Contains(ctype, "json") && !strings.HasPrefix(ctype, "text/"))
	if !binary {
		text := strings.TrimSpace(wm.Str(body["text"]))
		if c := clean(wm.Str(body["cid"]), 16); c != "" {
			cid = c
		}
		if d := clean(wm.Str(body["dog"]), 16); d != "" {
			dog = d
		}
		if text == "" {
			return bad(422, "empty")
		}
		if len([]rune(text)) > 300 {
			return bad(422, "text_too_long")
		}
		intent := ""
		if wm.Str(body["intent"]) == "news" {
			intent = "news" // the pop-up's "Today's news" chip (firmware talk.cpp askNews, 2026-11-20)
		}
		return map[string]any{"ok": true, "text": text, "audio": "", "cid": cid, "dog": dog, "status": 200, "err": "", "wake": false,
			"stream": (stream || wm.Bool(body["stream"])) && !voiceOff, "say_rate": sayRate, "intent": intent}
	}
	var raw []byte
	if len(files) > 0 {
		f, _ := files[0].(map[string]any)
		if wm.Int(f["size"]) > 48000*2*maxSec+4096 {
			return bad(413, "audio_too_long")
		}
		b, err := wm.FileBytes("")
		if err != nil {
			wm.Log("file: ", err.Error())
			return bad(500, "server_error")
		}
		raw = b
	} else {
		// the body as it arrived (the platform hands it here up to 2 MiB; the local test harness only this way)
		b, err := base64.StdEncoding.DecodeString(raw64)
		if err != nil {
			return bad(415, "bad_body")
		}
		raw = b
	}
	var pcm []byte
	rate := 16000
	if len(raw) >= 12 && string(raw[0:4]) == "RIFF" {
		p, r, why := pcmOfWav(raw)
		if why != "" {
			return bad(415, why)
		}
		pcm = p
		rate = r
	} else {
		pcm = raw
		if q, qerr := strconv.Atoi(wm.Str(query["rate"])); qerr == nil {
			rate = q
		}
	}
	if rate < 8000 || rate > 48000 {
		return bad(415, "bad_rate")
	}
	if len(pcm)%2 == 1 {
		pcm = pcm[:len(pcm)-1]
	}
	secs := float64(len(pcm)) / float64(rate*2)
	if secs < 0.3 {
		return bad(422, "audio_too_short")
	}
	if secs > maxSec+0.5 {
		return bad(413, "audio_too_long")
	}
	return map[string]any{"ok": true, "text": "", "audio": base64.StdEncoding.EncodeToString(wav(pcm, rate)), "cid": cid, "dog": dog,
		"status": 200, "err": "", "seconds": secs, "wake": wm.Bool(query["wake"]), "stream": stream, "say_rate": sayRate, "intent": ""}
}
