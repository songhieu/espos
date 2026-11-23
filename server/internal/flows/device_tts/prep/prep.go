// Workflow device_tts, node prep (imported by tools/importer).
package prep

import (
	"espos/internal/wm"
	"regexp"
	"strconv"
	"strings"
	"unicode"
)

// GET /v1/device/tts?text=<≤160 letters>&lang=<tag>&rate=16000 (the device's token; guards require_device,
// admin_device_active; 10 a minute): a short phrase the OS speaks on its own (desktop-protocol §11: an announcement
// that a Claude session waits, at most one per 2 minutes, merged, none at night: the device decides when). The voice
// is the device's dog's (s3.pet_dog), the same TTS as the dog's replies; no model call, nothing kept.

const maxText = 160

var rates = map[int]bool{8000: true, 11025: true, 16000: true, 22050: true, 24000: true, 32000: true, 44100: true, 48000: true}
var langRe = regexp.MustCompile(`^[a-z]{2,3}(-[A-Za-z]{2,4})?$`)

// The dogs' voices (assist_reply talk.go, written by scripts/gen-assist-dogs.py): keep in step.
var dogVoice = map[string]string{"bao": "adam", "momo": "thai-son", "boop": "adam-bua", "leaf": "ngoc-huyen", "volt": "hai-dang",
	"byte": "mai-anh", "shiba": "minh-triet", "corgi": "my-duyen", "husky": "thuc-doan", "golden": "quang-son"}

func bad(status int, err string) map[string]any {
	return map[string]any{"ok": false, "status": status, "err": err, "text": "", "voice": "", "rate": 16000}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	dev := wm.Int(in["device_id"])
	if dev == 0 {
		return bad(401, "unauthorized")
	}
	q, _ := in["query"].(map[string]any)
	text := strings.Join(strings.Fields(wm.Str(q["text"])), " ")
	if text == "" {
		return bad(422, "empty")
	}
	if len([]rune(text)) > maxText {
		return bad(422, "text_too_long")
	}
	for _, r := range text {
		if unicode.IsControl(r) {
			return bad(422, "bad_text")
		}
	}
	if l := wm.Str(q["lang"]); l != "" && !langRe.MatchString(l) {
		return bad(422, "bad_lang")
	}
	rate := 16000
	if r, err := strconv.Atoi(wm.Str(q["rate"])); err == nil && rates[r] {
		rate = r
	}
	voice := "adam"
	rows, err := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
		{"column": "device_id", "op": "eq", "value": dev}, {"column": "skey", "op": "eq", "value": "s3.pet_dog"}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) == 1 {
		if v := dogVoice[strings.ToLower(strings.TrimSpace(wm.Str(rows[0]["svalue"])))]; v != "" {
			voice = v
		}
	}
	return map[string]any{"ok": true, "status": 200, "err": "", "text": text, "voice": voice, "rate": rate}
}
