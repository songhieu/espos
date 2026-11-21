package wm

import (
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// The vendors' speech-to-text APIs as their documentation describes them, played by local test servers: no real
// service is called and no real key is used.

func testWav(rate int) []byte {
	b := []byte("RIFF\x00\x00\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00")
	b = binary.LittleEndian.AppendUint32(b, uint32(rate))
	b = binary.LittleEndian.AppendUint32(b, uint32(2*rate))
	b = append(b, 2, 0, 16, 0)
	b = append(b, []byte("data\x04\x00\x00\x00\x01\x02\x03\x04")...)
	return b
}

func clearSTT(t *testing.T) {
	for _, k := range []string{"ESPOS_STT_API", "ESPOS_STT_URL", "ESPOS_STT_KEY", "ESPOS_STT_MODEL", "ESPOS_STT_LANG", "ESPOS_STT_REGION"} {
		t.Setenv(k, "")
	}
}

// ElevenLabs Scribe: POST /v1/speech-to-text, multipart {model_id, file, language_code}, xi-api-key → {"text": ...}.
func TestTranscribeElevenLabs(t *testing.T) {
	var key, model, lang string
	var file []byte
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		key = r.Header.Get("xi-api-key")
		r.ParseMultipartForm(1 << 20)
		model, lang = r.FormValue("model_id"), r.FormValue("language_code")
		if f, _, err := r.FormFile("file"); err == nil {
			file, _ = io.ReadAll(f)
		}
		io.WriteString(w, `{"language_code":"vie","text":" Mấy giờ rồi? ","words":[]}`)
	}))
	defer srv.Close()
	clearSTT(t)
	t.Setenv("ESPOS_STT_API", "elevenlabs")
	t.Setenv("ESPOS_STT_KEY", "el-test")
	if !STTConfigured() {
		t.Fatal("elevenlabs with a key is set")
	}
	t.Setenv("ESPOS_STT_URL", srv.URL+"/v1/speech-to-text")
	t.Setenv("ESPOS_STT_LANG", "vi")
	wav := testWav(16000)
	text, err := Transcribe(base64.StdEncoding.EncodeToString(wav))
	if err != nil || text != "Mấy giờ rồi?" {
		t.Fatalf("got %q %v", text, err)
	}
	if key != "el-test" || model != "scribe_v2" || lang != "vi" || string(file) != string(wav) {
		t.Fatalf("sent key %q model %q lang %q, %d bytes", key, model, lang, len(file))
	}
}

// Google Cloud Speech-to-Text v1: POST speech:recognize with X-Goog-Api-Key, {config.languageCode, audio.content}
// → results[].alternatives[0].transcript.
func TestTranscribeGoogle(t *testing.T) {
	var key string
	var got struct {
		Config struct {
			LanguageCode string `json:"languageCode"`
		} `json:"config"`
		Audio struct {
			Content string `json:"content"`
		} `json:"audio"`
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		key = r.Header.Get("X-Goog-Api-Key")
		json.NewDecoder(r.Body).Decode(&got)
		io.WriteString(w, `{"results":[{"alternatives":[{"transcript":"đặt báo thức","confidence":0.9}]},{"alternatives":[{"transcript":"sáu giờ sáng"}]}]}`)
	}))
	defer srv.Close()
	clearSTT(t)
	t.Setenv("ESPOS_STT_API", "google")
	t.Setenv("ESPOS_STT_KEY", "g-test")
	t.Setenv("ESPOS_STT_URL", srv.URL+"/v1/speech:recognize")
	wav := testWav(16000)
	text, err := Transcribe(base64.StdEncoding.EncodeToString(wav))
	if err != nil || text != "đặt báo thức sáu giờ sáng" {
		t.Fatalf("got %q %v", text, err)
	}
	if key != "g-test" || got.Config.LanguageCode != "en-US" || got.Audio.Content != base64.StdEncoding.EncodeToString(wav) {
		t.Fatalf("sent %q %+v", key, got.Config)
	}
	// an OAuth access token goes as Bearer
	var auth string
	srv3 := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		auth = r.Header.Get("Authorization")
		io.WriteString(w, `{}`)
	}))
	defer srv3.Close()
	t.Setenv("ESPOS_STT_URL", srv3.URL)
	t.Setenv("ESPOS_STT_KEY", "ya29.token")
	Transcribe(base64.StdEncoding.EncodeToString(wav))
	if auth != "Bearer ya29.token" {
		t.Fatalf("token sent as %q", auth)
	}
	t.Setenv("ESPOS_STT_KEY", "g-test")
	// silence: no results, no words
	srv2 := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, `{}`) }))
	defer srv2.Close()
	t.Setenv("ESPOS_STT_URL", srv2.URL)
	if text, err := Transcribe(base64.StdEncoding.EncodeToString(wav)); err != nil || text != "" {
		t.Fatalf("silence: %q %v", text, err)
	}
}

// Azure short-audio REST: POST .../speech/recognition/conversation/cognitiveservices/v1?language=vi-VN with
// Ocp-Apim-Subscription-Key and Content-Type audio/wav; codecs=audio/pcm; samplerate=16000 → {RecognitionStatus,
// DisplayText}.
func TestTranscribeAzure(t *testing.T) {
	var key, ctype, query, path string
	status := "Success"
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		key, ctype, query, path = r.Header.Get("Ocp-Apim-Subscription-Key"), r.Header.Get("Content-Type"), r.URL.RawQuery, r.URL.Path
		json.NewEncoder(w).Encode(map[string]any{"RecognitionStatus": status, "DisplayText": "Thời tiết hôm nay thế nào?", "Offset": 0, "Duration": 1})
	}))
	defer srv.Close()
	clearSTT(t)
	t.Setenv("ESPOS_STT_API", "azure")
	t.Setenv("ESPOS_STT_KEY", "az-test")
	if STTConfigured() {
		t.Fatal("azure needs a region or an address")
	}
	t.Setenv("ESPOS_STT_URL", srv.URL+"/speech/recognition/conversation/cognitiveservices/v1")
	text, err := Transcribe(base64.StdEncoding.EncodeToString(testWav(16000)))
	if err != nil || text != "Thời tiết hôm nay thế nào?" {
		t.Fatalf("got %q %v", text, err)
	}
	if key != "az-test" || ctype != "audio/wav; codecs=audio/pcm; samplerate=16000" ||
		query != "language=en-US&format=simple" || path != "/speech/recognition/conversation/cognitiveservices/v1" {
		t.Fatalf("sent key %q type %q query %q path %q", key, ctype, query, path)
	}
	status = "Error"
	if _, err := Transcribe(base64.StdEncoding.EncodeToString(testWav(16000))); err == nil || !strings.Contains(err.Error(), "Error") {
		t.Fatalf("a failed recognition: %v", err)
	}
	if RegionLang("en") != "en-US" || RegionLang("fr") != "fr-FR" || RegionLang("pt-BR") != "pt-BR" || RegionLang("") != "en-US" ||
		RegionLang("vi") != "vi-VN" || BaseLang("zh-Hans") != "zh" || BaseLang("pt-BR") != "pt" {
		t.Fatal("regionLang / baseLang")
	}
}

// Without ESPOS_STT_LANG, the device's own language is the hint (global: no language is assumed).
func TestTranscribeDeviceLanguage(t *testing.T) {
	var lang, query string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if strings.Contains(r.Header.Get("Content-Type"), "multipart") {
			r.ParseMultipartForm(1 << 20)
			lang = r.FormValue("language")
		}
		query = r.URL.RawQuery
		io.WriteString(w, `{"text":"hallo","RecognitionStatus":"Success","DisplayText":"hallo"}`)
	}))
	defer srv.Close()
	clearSTT(t)
	t.Setenv("ESPOS_STT_URL", srv.URL+"/v1")
	wav := base64.StdEncoding.EncodeToString(testWav(16000))
	TranscribeLang(wav, "de")
	if lang != "de" {
		t.Fatalf("whisper language: %q", lang)
	}
	TranscribeLang(wav, "")
	if lang != "" {
		t.Fatalf("no device language: the service guesses, got %q", lang)
	}
	t.Setenv("ESPOS_STT_LANG", "ja")
	TranscribeLang(wav, "de")
	if lang != "ja" {
		t.Fatalf("ESPOS_STT_LANG wins: %q", lang)
	}
	t.Setenv("ESPOS_STT_LANG", "")
	t.Setenv("ESPOS_STT_API", "azure")
	t.Setenv("ESPOS_STT_KEY", "k")
	TranscribeLang(wav, "zh-Hans")
	if query != "language=zh-CN&format=simple" {
		t.Fatalf("azure language: %q", query)
	}
}

func TestSTTUnknownAPI(t *testing.T) {
	clearSTT(t)
	t.Setenv("ESPOS_STT_API", "nope")
	t.Setenv("ESPOS_STT_URL", "http://x")
	if _, err := Transcribe(base64.StdEncoding.EncodeToString(testWav(16000))); err == nil || !strings.Contains(err.Error(), "google or azure") {
		t.Fatalf("unknown api: %v", err)
	}
}
