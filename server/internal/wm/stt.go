package wm

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"
)

// Speech to text before the model (README: "The dog assistant"): with it set, the recording becomes words on this
// server and the model only reads text, so a model that cannot hear works too.
//
//	ESPOS_STT_API     openai (default: any POST /audio/transcriptions, e.g. OpenAI, Groq, a local Whisper),
//	                  elevenlabs, google, azure
//	ESPOS_STT_URL     openai: the API base (the part before /audio/transcriptions). The others: optional, the
//	                  vendor's own address is used when it is empty
//	ESPOS_STT_KEY     its key: Bearer (openai), xi-api-key (elevenlabs), X-Goog-Api-Key (google),
//	                  Ocp-Apim-Subscription-Key (azure)
//	ESPOS_STT_MODEL   openai: whisper-1; elevenlabs: scribe_v2 (google, azure: none)
//	ESPOS_STT_LANG    the spoken language (en, vi, de...), for every device. Unset: each device's own language
//	                  (sys.lang) is the hint; without one, openai and elevenlabs guess, google and azure use en-US
//	ESPOS_STT_REGION  azure only: the Speech resource's region (the classic regional address), unless ESPOS_STT_URL
//	                  is set (e.g. https://<resource>.cognitiveservices.azure.com/stt/speech/recognition/conversation/cognitiveservices/v1)

const (
	elevenLabsSTTURL = "https://api.elevenlabs.io/v1/speech-to-text"
	googleSTTURL     = "https://speech.googleapis.com/v1/speech:recognize"
	azureSTTURL      = "https://%s.stt.speech.microsoft.com/speech/recognition/conversation/cognitiveservices/v1"
)

func sttAPI() string { return strings.ToLower(strings.TrimSpace(Env("ESPOS_STT_API", "openai"))) }

// STTConfigured says whether a speech-to-text service is set: an address, or a vendor and its key.
func STTConfigured() bool {
	if Env("ESPOS_STT_URL", "") != "" {
		return true
	}
	switch sttAPI() {
	case "elevenlabs", "google":
		return os.Getenv("ESPOS_STT_KEY") != ""
	case "azure":
		return os.Getenv("ESPOS_STT_KEY") != "" && Env("ESPOS_STT_REGION", "") != ""
	}
	return false
}

var sttClient = &http.Client{Timeout: 30 * time.Second}

// Transcribe turns a recording (a WAV file, base64) into its words with the speech-to-text service.
func Transcribe(wavB64 string) (string, error) { return TranscribeLang(wavB64, "") }

// TranscribeLang is Transcribe with the device's language (sys.lang: en, vi, zh-Hans, pt-BR...) as the hint when
// ESPOS_STT_LANG is not set.
func TranscribeLang(wavB64, deviceLang string) (string, error) {
	if !STTConfigured() {
		return "", errors.New("no speech-to-text service set (ESPOS_STT_URL, or ESPOS_STT_API with ESPOS_STT_KEY)")
	}
	wav, err := base64.StdEncoding.DecodeString(wavB64)
	if err != nil {
		return "", fmt.Errorf("the recording is not base64: %w", err)
	}
	key := os.Getenv("ESPOS_STT_KEY")
	base := Env("ESPOS_STT_URL", "")
	lang := strings.TrimSpace(Env("ESPOS_STT_LANG", ""))
	if lang == "" {
		lang = BaseLang(deviceLang)
	}
	switch api := sttAPI(); api {
	case "openai":
		u := strings.TrimRight(base, "/")
		if !strings.HasSuffix(u, "/audio/transcriptions") {
			u += "/audio/transcriptions"
		}
		fields := map[string]string{"model": Env("ESPOS_STT_MODEL", "whisper-1"), "response_format": "json"}
		if lang != "" {
			fields["language"] = lang
		}
		var ans struct {
			Text string `json:"text"`
		}
		err := sttPost(u, multipartBody(wav, fields), bearerHeader(key), &ans)
		return strings.TrimSpace(ans.Text), err
	case "elevenlabs":
		// POST /v1/speech-to-text, multipart {model_id, file, language_code}, xi-api-key: {"text": ...}
		if base == "" {
			base = elevenLabsSTTURL
		}
		fields := map[string]string{"model_id": Env("ESPOS_STT_MODEL", "scribe_v2")}
		if lang != "" {
			fields["language_code"] = lang
		}
		var ans struct {
			Text string `json:"text"`
		}
		err := sttPost(base, multipartBody(wav, fields), map[string]string{"xi-api-key": key}, &ans)
		return strings.TrimSpace(ans.Text), err
	case "google":
		// Cloud Speech-to-Text v1 speech:recognize: the WAV as base64; its header gives the encoding and the rate.
		if base == "" {
			base = googleSTTURL
		}
		b, _ := json.Marshal(map[string]any{
			"config": map[string]any{"languageCode": RegionLang(lang)},
			"audio":  map[string]any{"content": base64.StdEncoding.EncodeToString(wav)},
		})
		var ans struct {
			Results []struct {
				Alternatives []struct {
					Transcript string `json:"transcript"`
				} `json:"alternatives"`
			} `json:"results"`
		}
		h := map[string]string{"X-Goog-Api-Key": key} // an API key; an OAuth access token goes as Bearer
		if t, ok := strings.CutPrefix(key, "Bearer "); ok {
			h = map[string]string{"Authorization": "Bearer " + t}
		} else if strings.HasPrefix(key, "ya29.") {
			h = map[string]string{"Authorization": "Bearer " + key}
		}
		err := sttPost(base, sttBody{b, "application/json"}, h, &ans)
		var parts []string
		for _, r := range ans.Results {
			if len(r.Alternatives) > 0 {
				parts = append(parts, strings.TrimSpace(r.Alternatives[0].Transcript))
			}
		}
		return strings.TrimSpace(strings.Join(parts, " ")), err
	case "azure":
		// The speech to text REST API for short audio (up to 60 s): the WAV as the body, the language in the query.
		if base == "" {
			region := strings.TrimSpace(Env("ESPOS_STT_REGION", ""))
			if region == "" {
				return "", errors.New("azure speech: set ESPOS_STT_REGION (or ESPOS_STT_URL)")
			}
			base = fmt.Sprintf(azureSTTURL, region)
		}
		sep := "?"
		if strings.Contains(base, "?") {
			sep = "&"
		}
		u := base + sep + "language=" + url.QueryEscape(RegionLang(lang)) + "&format=simple"
		var ans struct {
			RecognitionStatus string `json:"RecognitionStatus"`
			DisplayText       string `json:"DisplayText"`
		}
		err := sttPost(u, sttBody{wav, "audio/wav; codecs=audio/pcm; samplerate=" + fmt.Sprint(wavRate(wav))},
			map[string]string{"Ocp-Apim-Subscription-Key": key, "Accept": "application/json"}, &ans)
		if err == nil && ans.RecognitionStatus != "Success" && ans.RecognitionStatus != "NoMatch" && ans.RecognitionStatus != "InitialSilenceTimeout" {
			return "", fmt.Errorf("azure speech: %s", ans.RecognitionStatus)
		}
		return strings.TrimSpace(ans.DisplayText), err
	default:
		return "", fmt.Errorf("ESPOS_STT_API %q: use openai, elevenlabs, google or azure", api)
	}
}

// baseLang: the ISO 639-1 part of a device language (zh-Hans → zh, pt-BR → pt), "" for none.
func BaseLang(l string) string {
	l = strings.TrimSpace(l)
	if i := strings.IndexAny(l, "-_"); i > 0 {
		l = l[:i]
	}
	return strings.ToLower(l)
}

// localeOf: the usual locale of a language, as Google and Azure want it.
var localeOf = map[string]string{"en": "en-US", "vi": "vi-VN", "de": "de-DE", "fr": "fr-FR", "es": "es-ES", "it": "it-IT",
	"ja": "ja-JP", "ko": "ko-KR", "zh": "zh-CN", "ru": "ru-RU", "pt": "pt-BR", "ar": "ar-SA", "hi": "hi-IN", "id": "id-ID",
	"th": "th-TH", "tr": "tr-TR", "pl": "pl-PL", "nl": "nl-NL", "uk": "uk-UA", "sv": "sv-SE", "cs": "cs-CZ"}

// regionLang: the language with a region (vi → vi-VN, en → en-US, pt-BR stays; unset → en-US).
func RegionLang(lang string) string {
	if strings.Contains(lang, "-") {
		return lang
	}
	if l, ok := localeOf[strings.ToLower(lang)]; ok {
		return l
	}
	if lang == "" {
		return "en-US"
	}
	return strings.ToLower(lang) + "-" + strings.ToUpper(lang)
}

// wavRate: the sample rate in a WAV header (16000 when it cannot be read).
func wavRate(wav []byte) int {
	if len(wav) >= 28 && string(wav[0:4]) == "RIFF" && string(wav[12:16]) == "fmt " {
		if r := int(uint32(wav[24]) | uint32(wav[25])<<8 | uint32(wav[26])<<16 | uint32(wav[27])<<24); r > 0 {
			return r
		}
	}
	return 16000
}

type sttBody struct {
	data  []byte
	ctype string
}

func multipartBody(wav []byte, fields map[string]string) sttBody {
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	fw, _ := mw.CreateFormFile("file", "speech.wav")
	fw.Write(wav)
	for k, v := range fields {
		mw.WriteField(k, v)
	}
	mw.Close()
	return sttBody{body.Bytes(), mw.FormDataContentType()}
}

func bearerHeader(key string) map[string]string {
	if key == "" {
		return nil
	}
	return map[string]string{"Authorization": "Bearer " + key}
}

// sttPost sends the recording and reads the JSON answer into out.
func sttPost(u string, b sttBody, headers map[string]string, out any) error {
	req, err := http.NewRequest("POST", u, bytes.NewReader(b.data))
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", UserAgent)
	req.Header.Set("Content-Type", b.ctype)
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	res, err := sttClient.Do(req)
	if err != nil {
		return err
	}
	defer res.Body.Close()
	raw, _ := io.ReadAll(io.LimitReader(res.Body, 1<<20))
	if res.StatusCode != 200 {
		return fmt.Errorf("speech-to-text answered %d: %s", res.StatusCode, clip(strings.TrimSpace(string(raw)), 300))
	}
	if err := json.Unmarshal(raw, out); err != nil {
		return fmt.Errorf("speech-to-text: not JSON: %s", clip(string(raw), 120))
	}
	return nil
}
