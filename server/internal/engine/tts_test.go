package engine

import (
	"bytes"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// The vendors' speech APIs, as their documentation describes them, played by local test servers: no real service is
// called and no real key is used.

func wavOf(rate int, data []byte) []byte {
	var h bytes.Buffer
	h.WriteString("RIFF")
	binary.Write(&h, binary.LittleEndian, uint32(36+len(data)))
	h.WriteString("WAVEfmt ")
	for _, v := range []any{uint32(16), uint16(1), uint16(1), uint32(rate), uint32(2 * rate), uint16(2), uint16(16)} {
		binary.Write(&h, binary.LittleEndian, v)
	}
	h.WriteString("data")
	binary.Write(&h, binary.LittleEndian, uint32(len(data)))
	return append(h.Bytes(), data...)
}

func clearTTS(t *testing.T) {
	for _, k := range []string{"ESPOS_TTS_URL", "ESPOS_TTS_API", "ESPOS_TTS_KEY", "ESPOS_TTS_VOICE", "ESPOS_TTS_MODEL",
		"ESPOS_TTS_FORMAT", "ESPOS_TTS_RATE", "ESPOS_TTS_REGION", "ESPOS_TTS_LANG"} {
		t.Setenv(k, "")
	}
}

func samples(t *testing.T, r *Result) int {
	t.Helper()
	if r.Stream == nil || r.Headers["X-Sample-Rate"] != "16000" {
		t.Fatalf("want a 16 kHz voice stream: %+v", r)
	}
	b, _ := io.ReadAll(r.Stream)
	r.Stream.Close()
	return len(b) / 2
}

// ElevenLabs: POST /v1/text-to-speech/{voice_id}/stream?output_format=pcm_16000 with xi-api-key, {text, model_id};
// the answer is raw 16-bit mono PCM at 16 kHz.
func TestTTSElevenLabs(t *testing.T) {
	var path, query, key string
	var got map[string]any
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path, query, key = r.URL.Path, r.URL.RawQuery, r.Header.Get("xi-api-key")
		got = nil
		json.NewDecoder(r.Body).Decode(&got)
		w.Header().Set("Content-Type", "audio/pcm")
		w.Write(pcm(16000, func(i int) int16 { return int16(i % 500) }))
	}))
	defer srv.Close()
	clearTTS(t)
	t.Setenv("ESPOS_TTS_API", "elevenlabs")
	t.Setenv("ESPOS_TTS_URL", srv.URL) // in place of https://api.elevenlabs.io
	t.Setenv("ESPOS_TTS_KEY", "el-test")
	// no voice: ElevenLabs' voices belong to each account, so one must be named; the text answer meanwhile
	if r := runSay(t); r.Stream != nil {
		t.Fatalf("elevenlabs without a voice: want the text answer: %+v", r)
	}
	t.Setenv("ESPOS_TTS_VOICE", "voice123")
	if n := samples(t, runSay(t)); n != 16000 {
		t.Fatalf("one second at 16 kHz passed through: got %d samples", n)
	}
	if path != "/v1/text-to-speech/voice123/stream" || query != "output_format=pcm_16000" || key != "el-test" {
		t.Fatalf("asked %s?%s with key %q", path, query, key)
	}
	if got["text"] != "Gâu, chào chủ!" || got["model_id"] != elevenLabsModel || got["language_code"] != "vi" {
		t.Fatalf("sent %v", got)
	}
	// a voice and a model of one's own
	t.Setenv("ESPOS_TTS_VOICE", "myVoice123")
	t.Setenv("ESPOS_TTS_MODEL", "eleven_multilingual_v2")
	samples(t, runSay(t))
	if path != "/v1/text-to-speech/myVoice123/stream" || got["model_id"] != "eleven_multilingual_v2" || got["language_code"] != nil {
		t.Fatalf("asked %s with %v", path, got)
	}
}

// Google Cloud Text-to-Speech: POST v1/text:synthesize with X-Goog-Api-Key, {input, voice, audioConfig LINEAR16};
// the answer is JSON {audioContent: base64}, and LINEAR16 audio carries a WAV header.
func TestTTSGoogle(t *testing.T) {
	var path, key string
	var got struct {
		Input struct {
			Text string `json:"text"`
		} `json:"input"`
		Voice struct {
			LanguageCode string `json:"languageCode"`
			Name         string `json:"name"`
		} `json:"voice"`
		AudioConfig struct {
			AudioEncoding   string `json:"audioEncoding"`
			SampleRateHertz int    `json:"sampleRateHertz"`
		} `json:"audioConfig"`
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path, key = r.URL.Path, r.Header.Get("X-Goog-Api-Key")
		json.NewDecoder(r.Body).Decode(&got)
		audio := wavOf(16000, pcm(16000, func(int) int16 { return 700 }))
		json.NewEncoder(w).Encode(map[string]any{"audioContent": base64.StdEncoding.EncodeToString(audio)})
	}))
	defer srv.Close()
	clearTTS(t)
	t.Setenv("ESPOS_TTS_API", "google")
	t.Setenv("ESPOS_TTS_URL", srv.URL+"/v1/text:synthesize")
	t.Setenv("ESPOS_TTS_KEY", "g-test")
	r := runSay(t)
	if r.Stream == nil {
		t.Fatalf("no stream: %+v", r)
	}
	b, _ := io.ReadAll(r.Stream)
	if len(b)/2 != 16000 || int16(binary.LittleEndian.Uint16(b[100:])) != 700 {
		t.Fatalf("the WAV's samples without its header: %d samples", len(b)/2)
	}
	if path != "/v1/text:synthesize" || key != "g-test" || got.Input.Text != "Gâu, chào chủ!" || got.Voice.Name != "vi-VN-Neural2-A" ||
		got.Voice.LanguageCode != "vi-VN" || got.AudioConfig.AudioEncoding != "LINEAR16" || got.AudioConfig.SampleRateHertz != 16000 {
		t.Fatalf("asked %s with %q: %+v", path, key, got)
	}
	// an English voice: its language comes from its name
	t.Setenv("ESPOS_TTS_VOICE", "en-US-Neural2-F")
	samples(t, runSay(t))
	if got.Voice.LanguageCode != "en-US" || got.Voice.Name != "en-US-Neural2-F" {
		t.Fatalf("voice %+v", got.Voice)
	}
}

// Azure AI Speech: POST https://{region}.tts.speech.microsoft.com/cognitiveservices/v1 with Ocp-Apim-Subscription-Key,
// SSML in, X-Microsoft-OutputFormat raw-16khz-16bit-mono-pcm: raw PCM out.
func TestTTSAzure(t *testing.T) {
	var key, format, ctype, ssml string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/cognitiveservices/v1" {
			http.NotFound(w, r)
			return
		}
		key, format, ctype = r.Header.Get("Ocp-Apim-Subscription-Key"), r.Header.Get("X-Microsoft-OutputFormat"), r.Header.Get("Content-Type")
		b, _ := io.ReadAll(r.Body)
		ssml = string(b)
		w.Write(pcm(16000, func(int) int16 { return 300 }))
	}))
	defer srv.Close()
	clearTTS(t)
	t.Setenv("ESPOS_TTS_API", "azure")
	t.Setenv("ESPOS_TTS_KEY", "az-test")
	// no region and no address: the text answer
	if r := runSay(t); r.Stream != nil || r.Status != 200 {
		t.Fatalf("azure without a region: want the text answer: %+v", r)
	}
	t.Setenv("ESPOS_TTS_URL", srv.URL+"/cognitiveservices/v1") // in place of https://{region}.tts.speech.microsoft.com
	if n := samples(t, runSay(t)); n != 16000 {
		t.Fatalf("got %d samples", n)
	}
	if key != "az-test" || format != "raw-16khz-16bit-mono-pcm" || !strings.HasPrefix(ctype, "application/ssml+xml") {
		t.Fatalf("headers: key %q format %q type %q", key, format, ctype)
	}
	if !strings.Contains(ssml, `xml:lang="vi-VN"`) || !strings.Contains(ssml, `<voice name="vi-VN-HoaiMyNeural">Gâu, chào chủ!</voice>`) {
		t.Fatalf("ssml: %s", ssml)
	}
}

func TestTTSAzureEscapesAndRegion(t *testing.T) {
	var b bytes.Buffer
	xmlEscape(&b, `Tom & "Jerry" <3`)
	if b.String() != "Tom &amp; &quot;Jerry&quot; &lt;3" {
		t.Fatalf("escaped: %s", b.String())
	}
	clearTTS(t)
	t.Setenv("ESPOS_TTS_API", "azure")
	t.Setenv("ESPOS_TTS_KEY", "k")
	t.Setenv("ESPOS_TTS_REGION", "southeastasia")
	c, err := azureCall("", "k", "x", 24000)
	if err != nil || c.url != "https://southeastasia.tts.speech.microsoft.com/cognitiveservices/v1" ||
		c.headers["X-Microsoft-OutputFormat"] != "raw-24khz-16bit-mono-pcm" || c.rate != 24000 {
		t.Fatalf("call %+v %v", c, err)
	}
	if !TTSConfigured() {
		t.Fatal("azure with a key and a region is a speech service")
	}
	c, _ = azureCall("", "k", "x", 11025) // a rate Azure does not offer: 16 kHz, resampled afterwards
	if c.rate != 16000 {
		t.Fatalf("rate %d", c.rate)
	}
}

func TestGoogleAuth(t *testing.T) {
	if h := googleAuth("AIzaKey"); h["X-Goog-Api-Key"] != "AIzaKey" || len(h) != 1 {
		t.Fatalf("api key: %v", h)
	}
	if h := googleAuth("ya29.abc"); h["Authorization"] != "Bearer ya29.abc" {
		t.Fatalf("token: %v", h)
	}
	if h := googleAuth("Bearer xyz"); h["Authorization"] != "Bearer xyz" {
		t.Fatalf("bearer: %v", h)
	}
}

// The voice follows the reply's language (or ESPOS_TTS_LANG); English when nothing marks another language.
func TestTTSLanguage(t *testing.T) {
	for text, want := range map[string]string{
		"Woof! It is two o'clock.": "en", "Gâu! Bây giờ là hai giờ chiều.": "vi", "Wuff! Es ist zwei Uhr, schön.": "de",
		"¡Guau! Son las dos.": "es", "Ouaf ! Il est déjà deux heures, ça va ?": "fr", "ワン！今は二時です。": "ja",
		"멍! 지금은 두 시예요.": "ko", "汪！现在两点了。": "zh", "Гав! Сейчас два часа.": "ru", "Au! São duas horas.": "pt",
	} {
		if got := detectLang(text); got != want {
			t.Fatalf("%q: %s, want %s", text, got, want)
		}
	}
	var sel map[string]any
	var ssml string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		if strings.HasPrefix(string(b), "<speak") {
			ssml = string(b)
			w.Write(pcm(1600, func(int) int16 { return 1 }))
			return
		}
		var body map[string]any
		json.Unmarshal(b, &body)
		sel, _ = body["voice"].(map[string]any)
		json.NewEncoder(w).Encode(map[string]any{"audioContent": base64.StdEncoding.EncodeToString(wavOf(16000, pcm(1600, func(int) int16 { return 1 })))})
	}))
	defer srv.Close()
	clearTTS(t)
	t.Setenv("ESPOS_TTS_KEY", "k")
	t.Setenv("ESPOS_TTS_URL", srv.URL)
	t.Setenv("ESPOS_TTS_API", "google")
	say := func(text string) {
		s, err := ttsStream(text, "", 16000)
		if err != nil {
			t.Fatal(err)
		}
		io.ReadAll(s)
		s.Close()
	}
	say("Woof! It is two o'clock.")
	if sel["name"] != "en-US-Neural2-F" || sel["languageCode"] != "en-US" {
		t.Fatalf("english: %v", sel)
	}
	say("Wuff! Es ist zwei Uhr, schön.")
	if sel["name"] != nil || sel["languageCode"] != "de-DE" {
		t.Fatalf("german: Google picks a de-DE voice: %v", sel)
	}
	t.Setenv("ESPOS_TTS_LANG", "fr")
	say("Woof!")
	if sel["languageCode"] != "fr-FR" {
		t.Fatalf("ESPOS_TTS_LANG: %v", sel)
	}
	t.Setenv("ESPOS_TTS_LANG", "")
	t.Setenv("ESPOS_TTS_API", "azure")
	say("Wuff! Es ist zwei Uhr, schön.")
	if !strings.Contains(ssml, `<voice name="en-US-AvaMultilingualNeural">`) {
		t.Fatalf("azure, a language without its own default: the multilingual voice: %s", ssml)
	}
	say("Woof! It is two.")
	if !strings.Contains(ssml, `<voice name="en-US-AvaMultilingualNeural">`) || !strings.Contains(ssml, `xml:lang="en-US"`) {
		t.Fatalf("azure english: %s", ssml)
	}
}

func TestTTSConfigured(t *testing.T) {
	clearTTS(t)
	if TTSConfigured() {
		t.Fatal("nothing set")
	}
	t.Setenv("ESPOS_TTS_API", "elevenlabs")
	if TTSConfigured() {
		t.Fatal("elevenlabs without a key")
	}
	t.Setenv("ESPOS_TTS_KEY", "k")
	if TTSConfigured() {
		t.Fatal("elevenlabs needs a voice too")
	}
	t.Setenv("ESPOS_TTS_VOICE", "v")
	if !TTSConfigured() {
		t.Fatal("elevenlabs with a key and a voice")
	}
	t.Setenv("ESPOS_TTS_VOICE", "")
	t.Setenv("ESPOS_TTS_API", "openai")
	if TTSConfigured() {
		t.Fatal("openai needs ESPOS_TTS_URL")
	}
	t.Setenv("ESPOS_TTS_API", "nope")
	t.Setenv("ESPOS_TTS_URL", "http://x")
	if _, err := ttsStream("a", "", 16000); err == nil || !strings.Contains(err.Error(), "elevenlabs, google or azure") {
		t.Fatalf("unknown api: %v", err)
	}
}
