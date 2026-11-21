package engine

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strings"
	"time"
	"unicode"

	"espos/internal/wm"
)

// ---- the tts node: the dog's voice from the speech service set in the environment ----
//
//	ESPOS_TTS_API    which API: openai (default), espos, elevenlabs, google, azure
//	ESPOS_TTS_URL    openai: an OpenAI-style base (https://api.openai.com/v1, or any server with POST /audio/speech);
//	                 espos: the full URL of a hosted-style /v1/tts. elevenlabs, google, azure: optional, the
//	                 vendor's own address is used when it is empty
//	ESPOS_TTS_KEY    its key: Bearer (openai, espos), xi-api-key (elevenlabs), X-Goog-Api-Key (google: an API
//	                 key; an OAuth access token "ya29..." goes as Bearer), Ocp-Apim-Subscription-Key (azure)
//	ESPOS_TTS_MODEL  openai: gpt-4o-mini-tts; espos: vieneu-turbo; elevenlabs: eleven_flash_v2_5 (google, azure: none)
//	ESPOS_TTS_VOICE  openai: alloy; espos: the dog's own voice; elevenlabs: a voice id from your account (required);
//	                 google, azure: unset = a voice for the reply's language (below)
//	ESPOS_TTS_LANG   the language the dog speaks (en, vi, de...). Unset: guessed from the reply's letters, English
//	                 when nothing marks another language. It picks google's and azure's voice and ElevenLabs'
//	                 language_code; a voice set in ESPOS_TTS_VOICE is used whatever the language
//	ESPOS_TTS_REGION azure only: the Speech resource's region (southeastasia, eastus...), unless ESPOS_TTS_URL is set
//	ESPOS_TTS_FORMAT openai only: pcm (default, 16-bit mono at ESPOS_TTS_RATE) or wav
//	ESPOS_TTS_RATE   openai pcm only: its sample rate, 24000 (OpenAI's)
//
// The device asks for 16-bit mono PCM at its own rate (16000 on the S3). ElevenLabs and Azure are asked for raw PCM
// at that rate when they offer it, Google for LINEAR16 at that rate; anything else is resampled as it streams.
// Without a service set the node fails, and the flows answer with text only.

// The vendors' own addresses (ESPOS_TTS_URL overrides them, e.g. for a proxy or a test).
const (
	elevenLabsBase = "https://api.elevenlabs.io"
	googleTTSURL   = "https://texttospeech.googleapis.com/v1/text:synthesize"
	azureTTSURL    = "https://%s.tts.speech.microsoft.com/cognitiveservices/v1"
)

// elevenLabsModel: the low-latency model, 32 languages (ElevenLabs' models page, 2026-11-21).
const elevenLabsModel = "eleven_flash_v2_5"

// Voices per language when ESPOS_TTS_VOICE is unset (names from the vendors' voice lists, 2026-11-21). Google picks
// a voice itself from the language code for the others; Azure's multilingual voice reads any language.
var (
	googleVoices = map[string]string{"en": "en-US-Neural2-F", "vi": "vi-VN-Neural2-A"}
	azureVoices  = map[string]string{"en": "en-US-AvaMultilingualNeural", "vi": "vi-VN-HoaiMyNeural"}
)

const azureAnyLanguage = "en-US-AvaMultilingualNeural"

// TTSConfigured says whether a speech service is set: an address, or a vendor and its key.
func TTSConfigured() bool {
	if wm.Env("ESPOS_TTS_URL", "") != "" {
		return true
	}
	switch ttsAPI() {
	case "elevenlabs":
		return os.Getenv("ESPOS_TTS_KEY") != "" && wm.Env("ESPOS_TTS_VOICE", "") != ""
	case "google":
		return os.Getenv("ESPOS_TTS_KEY") != ""
	case "azure":
		return os.Getenv("ESPOS_TTS_KEY") != "" && wm.Env("ESPOS_TTS_REGION", "") != ""
	}
	return false
}

func ttsAPI() string { return strings.ToLower(strings.TrimSpace(wm.Env("ESPOS_TTS_API", "openai"))) }

// ttsCall is one request to a speech service and how to read its answer.
type ttsCall struct {
	url     string
	body    []byte
	headers map[string]string
	answer  string // "raw": 16-bit mono PCM at rate; "wav": a WAV file; "b64wav": JSON {audioContent: base64 WAV}
	rate    int    // the answer's sample rate, for "raw"
}

func ttsStream(text, voice string, rate int) (io.ReadCloser, error) {
	if !TTSConfigured() {
		return nil, errors.New("no speech service set (ESPOS_TTS_URL, or ESPOS_TTS_API with ESPOS_TTS_KEY)")
	}
	if rate <= 0 {
		rate = 16000
	}
	key := os.Getenv("ESPOS_TTS_KEY")
	base := wm.Env("ESPOS_TTS_URL", "")
	var c ttsCall
	var err error
	switch api := ttsAPI(); api {
	case "espos":
		if v := wm.Env("ESPOS_TTS_VOICE", ""); v != "" {
			voice = v
		}
		c = jsonCall(base, map[string]any{"model": wm.Env("ESPOS_TTS_MODEL", "vieneu-turbo"), "voice": voice, "text": text,
			"format": "pcm", "sample_rate": rate, "stream": true}, bearer(key), "raw", rate)
	case "openai":
		u := strings.TrimRight(base, "/")
		if !strings.HasSuffix(u, "/audio/speech") {
			u += "/audio/speech"
		}
		format := strings.ToLower(wm.Env("ESPOS_TTS_FORMAT", "pcm"))
		answer := "raw"
		if format == "wav" {
			answer = "wav"
		}
		c = jsonCall(u, map[string]any{"model": wm.Env("ESPOS_TTS_MODEL", "gpt-4o-mini-tts"), "voice": wm.Env("ESPOS_TTS_VOICE", "alloy"),
			"input": text, "response_format": format}, bearer(key), answer, int(wm.Num(wm.Env("ESPOS_TTS_RATE", "24000"))))
	case "elevenlabs":
		c, err = elevenLabsCall(base, key, text, rate)
	case "google":
		c = googleCall(base, key, text, rate)
	case "azure":
		c, err = azureCall(base, key, text, rate)
	default:
		return nil, fmt.Errorf("ESPOS_TTS_API %q: use openai, espos, elevenlabs, google or azure", api)
	}
	if err != nil {
		return nil, err
	}
	return c.do(rate)
}

func bearer(key string) map[string]string {
	if key == "" {
		return map[string]string{}
	}
	return map[string]string{"Authorization": "Bearer " + key}
}

func jsonCall(u string, body map[string]any, headers map[string]string, answer string, rate int) ttsCall {
	b, _ := json.Marshal(body)
	headers["Content-Type"] = "application/json"
	return ttsCall{url: u, body: b, headers: headers, answer: answer, rate: rate}
}

// pick: the device's rate when the vendor offers it, else 16000 (resampled afterwards).
func pick(rate int, offered ...int) int {
	for _, r := range offered {
		if r == rate {
			return r
		}
	}
	return 16000
}

// elevenLabsCall: POST /v1/text-to-speech/{voice}/stream?output_format=pcm_16000 (16-bit PCM), xi-api-key.
func elevenLabsCall(base, key, text string, rate int) (ttsCall, error) {
	if base == "" {
		base = elevenLabsBase
	}
	voice := wm.Env("ESPOS_TTS_VOICE", "")
	if voice == "" {
		return ttsCall{}, errors.New("elevenlabs: set ESPOS_TTS_VOICE to a voice id from your ElevenLabs voices")
	}
	in := pick(rate, 8000, 16000, 22050, 24000, 44100, 48000)
	u := strings.TrimRight(base, "/") + "/v1/text-to-speech/" + url.PathEscape(voice) + "/stream?output_format=pcm_" + fmt.Sprint(in)
	model := wm.Env("ESPOS_TTS_MODEL", elevenLabsModel)
	body := map[string]any{"text": text, "model_id": model}
	if !strings.Contains(model, "multilingual_v2") { // the only model that refuses language_code
		body["language_code"] = ttsLang(text)
	}
	return jsonCall(u, body, map[string]string{"xi-api-key": key, "Accept": "audio/pcm"}, "raw", in), nil
}

// ttsLang: the language the reply is read in: ESPOS_TTS_LANG, else the reply's letters, else English.
func ttsLang(text string) string {
	if l := wm.BaseLang(wm.Env("ESPOS_TTS_LANG", "")); l != "" {
		return l
	}
	return detectLang(text)
}

// The letters only Vietnamese uses (ă, đ, ơ, ư and the dot-below / hook-above vowels).
const viLetters = "ăđơưạảấầẩẫậắằẳẵặẹẻẽếềểễệỉịọỏốồổỗộớờởỡợụủứừửữựỳỵỷỹ"

// detectLang guesses a reply's language from its script and a few letters that mark a language; English when
// nothing marks another. It only picks a voice: ESPOS_TTS_LANG settles any doubt.
func detectLang(s string) string {
	var hangul, kana, han, cyr, arab, thai, vi int
	has := func(set string) bool { return strings.ContainsAny(strings.ToLower(s), set) }
	for _, r := range s {
		switch {
		case unicode.In(r, unicode.Hangul):
			hangul++
		case unicode.In(r, unicode.Hiragana, unicode.Katakana):
			kana++
		case unicode.In(r, unicode.Han):
			han++
		case unicode.In(r, unicode.Cyrillic):
			cyr++
		case unicode.In(r, unicode.Arabic):
			arab++
		case unicode.In(r, unicode.Thai):
			thai++
		case strings.ContainsRune(viLetters, unicode.ToLower(r)):
			vi++
		}
	}
	switch {
	case hangul > 0:
		return "ko"
	case kana > 0:
		return "ja"
	case han > 0:
		return "zh"
	case cyr > 0:
		return "ru"
	case arab > 0:
		return "ar"
	case thai > 0:
		return "th"
	case vi > 0:
		return "vi"
	case has("ñ¿¡"):
		return "es"
	case has("ãõ"):
		return "pt"
	case has("äöüß"):
		return "de"
	case has("çœêëîïûâ"):
		return "fr"
	case has("àèìòù"):
		return "it"
	}
	return "en"
}

var langOfVoice = regexp.MustCompile(`^[a-z]{2,3}-[A-Z]{2}`)

// voiceLang: "vi-VN" from "vi-VN-HoaiMyNeural" or "en-US-Neural2-F" (en-US when the name says none).
func voiceLang(voice string) string {
	if l := langOfVoice.FindString(voice); l != "" {
		return l
	}
	return "en-US"
}

// googleCall: Cloud Text-to-Speech text:synthesize, LINEAR16 at the device's rate. The answer is JSON with the audio
// as base64; LINEAR16 audio comes with a WAV header.
func googleCall(base, key, text string, rate int) ttsCall {
	if base == "" {
		base = googleTTSURL
	}
	lang := ttsLang(text)
	sel := map[string]any{"languageCode": wm.RegionLang(lang)}
	if lang == "zh" {
		sel["languageCode"] = "cmn-CN" // Google lists Mandarin voices under cmn-CN
	}
	if voice := wm.Env("ESPOS_TTS_VOICE", googleVoices[lang]); voice != "" {
		sel = map[string]any{"languageCode": voiceLang(voice), "name": voice}
	}
	return jsonCall(base, map[string]any{
		"input":       map[string]any{"text": text},
		"voice":       sel,
		"audioConfig": map[string]any{"audioEncoding": "LINEAR16", "sampleRateHertz": rate},
	}, googleAuth(key), "b64wav", rate)
}

// googleAuth: an API key goes in X-Goog-Api-Key; an OAuth access token ("ya29...", or "Bearer ...") as Bearer.
func googleAuth(key string) map[string]string {
	if t, ok := strings.CutPrefix(key, "Bearer "); ok {
		return map[string]string{"Authorization": "Bearer " + t}
	}
	if strings.HasPrefix(key, "ya29.") {
		return map[string]string{"Authorization": "Bearer " + key}
	}
	return map[string]string{"X-Goog-Api-Key": key}
}

// azureCall: Azure AI Speech's REST text to speech: SSML in, raw-16khz-16bit-mono-pcm out.
func azureCall(base, key, text string, rate int) (ttsCall, error) {
	if base == "" {
		region := strings.TrimSpace(wm.Env("ESPOS_TTS_REGION", ""))
		if region == "" {
			return ttsCall{}, errors.New("azure speech: set ESPOS_TTS_REGION (or ESPOS_TTS_URL)")
		}
		base = fmt.Sprintf(azureTTSURL, region)
	}
	voice := wm.Env("ESPOS_TTS_VOICE", azureVoices[ttsLang(text)])
	if voice == "" {
		voice = azureAnyLanguage // a multilingual voice: it reads the text's own language
	}
	in := pick(rate, 8000, 16000, 24000, 48000)
	var esc bytes.Buffer
	_ = xmlEscape(&esc, text)
	ssml := fmt.Sprintf(`<speak version="1.0" xmlns="http://www.w3.org/2001/10/synthesis" xml:lang="%s"><voice name="%s">%s</voice></speak>`,
		voiceLang(voice), voice, esc.String())
	return ttsCall{url: base, body: []byte(ssml), answer: "raw", rate: in, headers: map[string]string{
		"Ocp-Apim-Subscription-Key": key,
		"Content-Type":              "application/ssml+xml",
		"X-Microsoft-OutputFormat":  fmt.Sprintf("raw-%dkhz-16bit-mono-pcm", in/1000),
	}}, nil
}

func xmlEscape(w *bytes.Buffer, s string) error {
	r := strings.NewReplacer("&", "&amp;", "<", "&lt;", ">", "&gt;", `"`, "&quot;", "'", "&apos;")
	_, err := w.WriteString(r.Replace(s))
	return err
}

// do sends the request and turns the answer into 16-bit mono PCM at rate, streaming when the service streams.
func (c ttsCall) do(rate int) (io.ReadCloser, error) {
	req, err := http.NewRequest("POST", c.url, bytes.NewReader(c.body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", wm.UserAgent)
	for k, v := range c.headers {
		req.Header.Set(k, v)
	}
	client := &http.Client{Timeout: 60 * time.Second}
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	if res.StatusCode >= 300 {
		msg, _ := io.ReadAll(io.LimitReader(res.Body, 2048))
		res.Body.Close()
		return nil, fmt.Errorf("speech service answered %d: %s", res.StatusCode, strings.TrimSpace(string(msg)))
	}
	src, in := io.ReadCloser(res.Body), c.rate
	switch c.answer {
	case "wav":
		w, err := skipWav(res.Body)
		if err != nil {
			res.Body.Close()
			return nil, err
		}
		src, in = w, w.rate
	case "b64wav":
		var out struct {
			AudioContent string `json:"audioContent"`
		}
		err := json.NewDecoder(io.LimitReader(res.Body, 32<<20)).Decode(&out)
		res.Body.Close()
		if err != nil {
			return nil, fmt.Errorf("speech service: not JSON: %v", err)
		}
		audio, err := base64.StdEncoding.DecodeString(out.AudioContent)
		if err != nil || len(audio) == 0 {
			return nil, errors.New("speech service: no audioContent")
		}
		src = io.NopCloser(bytes.NewReader(audio))
		if bytes.HasPrefix(audio, []byte("RIFF")) {
			w, err := skipWav(src)
			if err != nil {
				return nil, err
			}
			src, in = w, w.rate
		}
	}
	if in == rate {
		return src, nil
	}
	return &resampler{src: src, in: in, out: rate}, nil
}
