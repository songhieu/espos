// Workflow device_dictate, node work (imported by tools/importer).
package work

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"regexp"
	"strings"
)

// POST /v1/device/dictate?lang=<tag> (the device's token; guards require_device, admin_device_active and
// admin_ai_gate, which counts one AI turn like a dog turn): body audio/wav (or JSON {"audio": base64 WAV}), 16-bit
// mono PCM, 0.3–30 s → {text}, the
// words as said, in that language. The same model call shape as a dog turn that hears audio (assist_reply): no
// separate speech-to-text provider. Nothing is kept; the text goes back to the field, the person edits or sends it.

const (
	model  = "vx/gemini-3.5-flash-lite"
	maxSec = 30
)

var langRe = regexp.MustCompile(`^[a-z]{2,3}(-[A-Za-z]{2,4})?$`)

var langNames = map[string]string{"vi": "Vietnamese", "en": "English", "es": "Spanish", "fr": "French", "de": "German",
	"ja": "Japanese", "ko": "Korean", "pt-BR": "Brazilian Portuguese", "ru": "Russian", "id": "Indonesian", "zh-Hans": "Simplified Chinese"}

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

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
			size = len(b) - body
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

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	query, _ := in["query"].(map[string]any)
	lang := wm.Str(query["lang"])
	if lang == "" {
		lang = "vi"
	}
	if !langRe.MatchString(lang) {
		return answer(422, map[string]any{"error": "bad_lang"})
	}
	files, _ := in["files"].([]any)
	var raw []byte
	if len(files) > 0 {
		f, _ := files[0].(map[string]any)
		if wm.Int(f["size"]) > 48000*2*maxSec+4096 {
			return answer(413, map[string]any{"error": "audio_too_long"})
		}
		b, err := wm.FileBytes("")
		if err != nil {
			wm.Log("file: ", err.Error())
			return answer(500, map[string]any{"error": "server_error"})
		}
		raw = b
	} else if body, _ := in["body"].(map[string]any); body != nil && wm.Str(body["audio"]) != "" {
		// JSON {"audio": "<the WAV, base64>"} (a JS app's net.request sends JSON more easily than bytes)
		b, err := base64.StdEncoding.DecodeString(wm.Str(body["audio"]))
		if err != nil {
			return answer(415, map[string]any{"error": "bad_body"})
		}
		raw = b
	} else {
		b, err := base64.StdEncoding.DecodeString(wm.Str(in["raw"]))
		if err != nil || len(b) == 0 {
			return answer(415, map[string]any{"error": "bad_body"})
		}
		raw = b
	}
	pcm, rate, why := pcmOfWav(raw)
	if why != "" {
		return answer(415, map[string]any{"error": why})
	}
	if rate < 8000 || rate > 48000 {
		return answer(415, map[string]any{"error": "bad_rate"})
	}
	secs := float64(len(pcm)) / float64(rate*2)
	if secs < 0.3 {
		return answer(422, map[string]any{"error": "audio_too_short"})
	}
	if secs > maxSec+0.5 {
		return answer(413, map[string]any{"error": "audio_too_long"})
	}
	name := langNames[lang]
	if name == "" {
		name = lang
	}
	// ?expect=yesno&about=<what was read back>: the "say ok to send" flow of an app (the Sessions app's dictated
	// reply): the words, and whether they mean yes / no / unclear / something else. The app still decides to send.
	yesno := wm.Str(query["expect"]) == "yesno"
	about := strings.Join(strings.Fields(wm.Str(query["about"])), " ")
	if len([]rune(about)) > 300 {
		about = string([]rune(about)[:300])
	}
	sys := "Write down exactly what the person says in the recording, in " + name +
		", with normal punctuation and capitals. Do not answer it, translate it or add anything. Silence or noise: an empty text. " +
		"Answer JSON {\"text\":\"...\"}."
	if yesno {
		sys = "The person was just asked to confirm sending this: «" + about + "». Write down exactly what they say in the recording, in " +
			name + ", and decide what they meant: \"yesno\": \"confirm\" (agrees, in any wording or language), \"cancel\" (no, later, " +
			"stop), \"unclear\" (asks what it is, or cannot be understood) or \"other\" (says something else). Answer JSON " +
			"{\"text\":\"...\",\"yesno\":\"...\"}."
	}
	msgs := []any{
		map[string]any{"role": "system", "content": sys},
		map[string]any{"role": "user", "content": []any{
			map[string]any{"type": "input_audio", "input_audio": map[string]any{"data": base64.StdEncoding.EncodeToString(raw), "format": "wav"}}}},
	}
	req := map[string]any{"model": model, "messages": msgs, "max_tokens": 600, "temperature": 0, "stream": false,
		"response_format": map[string]any{"type": "json_object"}, "reasoning_effort": "low"}
	lastErr := ""
	for try := 0; try < 2; try++ {
		r, ferr := wm.AIChat(req)
		if ferr != nil {
			lastErr = ferr.Error()
			continue
		}
		if wm.Int(r["status"]) != 200 {
			lastErr = fmt.Sprintf("router status %d", wm.Int(r["status"]))
			continue
		}
		body, _ := r["body"].(map[string]any)
		ch, _ := body["choices"].([]any)
		if len(ch) == 0 {
			lastErr = "no choices"
			continue
		}
		c0, _ := ch[0].(map[string]any)
		msg, _ := c0["message"].(map[string]any)
		content := strings.TrimSpace(wm.Str(msg["content"]))
		if i, j := strings.Index(content, "{"), strings.LastIndex(content, "}"); i >= 0 && j > i {
			content = content[i : j+1]
		}
		var got map[string]any
		if jerr := json.Unmarshal([]byte(content), &got); jerr != nil {
			lastErr = "not json"
			continue
		}
		t := strings.Join(strings.Fields(wm.Str(got["text"])), " ")
		if len([]rune(t)) > 2000 {
			t = string([]rune(t)[:2000])
		}
		out := map[string]any{"text": t, "seconds": secs}
		if yesno {
			yn := wm.Str(got["yesno"])
			if yn != "confirm" && yn != "cancel" && yn != "other" {
				yn = "unclear"
			}
			out["yesno"] = yn
		}
		return answer(200, out)
	}
	wm.Log("model failed: ", lastErr)
	return answer(502, map[string]any{"error": "ai_failed"})
}
