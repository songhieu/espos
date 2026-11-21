package engine

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
	"testing/fstest"
)

// sayFlow is the shape the importer gives the device's voice flows: prep, then a tts node whose error port leads to
// a text-only answer.
const sayFlow = `{"code":"say","draft":{"nodes":[
	{"id":"in","type":"http_in"},
	{"id":"prep","type":"code","config":{"source":{"$file":"nodes/prep.go"}}},
	{"id":"say","type":"tts","config":{"text":"{{ prep.text }}","voice":"{{ prep.voice }}","rate":"{{ prep.rate }}"}},
	{"id":"out","type":"respond","config":{"stream":true,"headers":{"Content-Type":"audio/pcm","X-Sample-Rate":"{{ prep.rate }}"}}},
	{"id":"outj","type":"respond","config":{"status":200,"body":{"reply":"{{ prep.text }}"}}}],
	"edges":[{"from":"in","to":"prep"},{"from":"prep","to":"say"},{"from":"say","to":"out"},{"from":"say","to":"outj","port":"error"}]}}`

func runSay(t *testing.T) *Result {
	t.Helper()
	fsys := fstest.MapFS{"workflows/say/workflow.json": {Data: []byte(sayFlow)}}
	nodes := map[string]NodeFunc{"say/prep": func(in, ctx map[string]any) map[string]any {
		return map[string]any{"text": "Gâu, chào chủ!", "voice": "dog-voice", "rate": 16000}
	}}
	e, err := Load(fsys, "workflows", nodes)
	if err != nil {
		t.Fatal(err)
	}
	r, err := e.Run("say", map[string]any{}, nil, "http://x")
	if err != nil {
		t.Fatal(err)
	}
	return r
}

func pcm(n int, v func(i int) int16) []byte {
	b := make([]byte, 0, 2*n)
	for i := 0; i < n; i++ {
		b = binary.LittleEndian.AppendUint16(b, uint16(v(i)))
	}
	return b
}

func TestTTSOpenAIResampled(t *testing.T) {
	var got map[string]any
	var auth string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/v1/audio/speech" {
			http.NotFound(w, r)
			return
		}
		auth = r.Header.Get("Authorization")
		json.NewDecoder(r.Body).Decode(&got)
		w.Write(pcm(24000, func(i int) int16 { return int16(i % 1000) })) // one second at 24 kHz
	}))
	defer srv.Close()
	t.Setenv("ESPOS_TTS_URL", srv.URL+"/v1")
	t.Setenv("ESPOS_TTS_API", "")
	t.Setenv("ESPOS_TTS_KEY", "t-key")
	t.Setenv("ESPOS_TTS_VOICE", "nova")
	t.Setenv("ESPOS_TTS_FORMAT", "")
	t.Setenv("ESPOS_TTS_RATE", "")
	r := runSay(t)
	if r.Stream == nil || r.Headers["Content-Type"] != "audio/pcm" || r.Headers["X-Sample-Rate"] != "16000" {
		t.Fatalf("want a 16 kHz stream: %+v", r)
	}
	b, _ := io.ReadAll(r.Stream)
	r.Stream.Close()
	if n := len(b) / 2; n < 15990 || n > 16000 {
		t.Fatalf("one second at 16 kHz: got %d samples", n)
	}
	if auth != "Bearer t-key" || got["input"] != "Gâu, chào chủ!" || got["voice"] != "nova" || got["response_format"] != "pcm" ||
		got["model"] != "gpt-4o-mini-tts" {
		t.Fatalf("sent %q %v", auth, got)
	}
}

func TestTTSWav(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		data := pcm(22050, func(int) int16 { return 1000 })
		var h bytes.Buffer
		h.WriteString("RIFF")
		binary.Write(&h, binary.LittleEndian, uint32(0xFFFFFFFF)) // a streamed WAV: no length yet
		h.WriteString("WAVEfmt ")
		for _, v := range []any{uint32(16), uint16(1), uint16(1), uint32(22050), uint32(44100), uint16(2), uint16(16)} {
			binary.Write(&h, binary.LittleEndian, v)
		}
		h.WriteString("LIST")
		binary.Write(&h, binary.LittleEndian, uint32(4))
		h.WriteString("INFO")
		h.WriteString("data")
		binary.Write(&h, binary.LittleEndian, uint32(0xFFFFFFFF))
		w.Write(append(h.Bytes(), data...))
	}))
	defer srv.Close()
	t.Setenv("ESPOS_TTS_URL", srv.URL)
	t.Setenv("ESPOS_TTS_FORMAT", "wav")
	r := runSay(t)
	if r.Stream == nil {
		t.Fatalf("no stream: %+v", r)
	}
	b, _ := io.ReadAll(r.Stream)
	if n := len(b) / 2; n < 15990 || n > 16000 {
		t.Fatalf("22.05 kHz WAV to 16 kHz: got %d samples", n)
	}
	if v := int16(binary.LittleEndian.Uint16(b[200:])); v != 1000 {
		t.Fatalf("sample value changed: %d", v)
	}
}

func TestTTSEsposPassThrough(t *testing.T) {
	var got map[string]any
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		json.NewDecoder(r.Body).Decode(&got)
		w.Write([]byte{1, 2, 3, 4})
	}))
	defer srv.Close()
	t.Setenv("ESPOS_TTS_URL", srv.URL+"/v1/tts")
	t.Setenv("ESPOS_TTS_API", "espos")
	t.Setenv("ESPOS_TTS_VOICE", "")
	t.Setenv("ESPOS_TTS_MODEL", "")
	r := runSay(t)
	b, _ := io.ReadAll(r.Stream)
	if !bytes.Equal(b, []byte{1, 2, 3, 4}) {
		t.Fatalf("passed through: %v", b)
	}
	if got["text"] != "Gâu, chào chủ!" || got["voice"] != "dog-voice" || got["sample_rate"] != float64(16000) || got["model"] != "vieneu-turbo" {
		t.Fatalf("sent %v", got)
	}
}

func TestTTSUnsetAnswersText(t *testing.T) {
	t.Setenv("ESPOS_TTS_URL", "")
	r := runSay(t)
	if r.Stream != nil || r.Status != 200 || r.Body.(map[string]any)["reply"] != "Gâu, chào chủ!" {
		t.Fatalf("want the text answer: %+v", r)
	}
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { http.Error(w, "quota", 429) }))
	defer srv.Close()
	t.Setenv("ESPOS_TTS_URL", srv.URL)
	if r := runSay(t); r.Stream != nil || r.Status != 200 {
		t.Fatalf("a refused voice: want the text answer: %+v", r)
	}
}

func TestHTTPRequestFillsSecret(t *testing.T) {
	var auth string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		auth = r.Header.Get("Authorization")
		w.Write([]byte("pcm"))
	}))
	defer srv.Close()
	t.Setenv("ESPOS_TTS_KEY", "sk-1")
	fsys := fstest.MapFS{"workflows/h/workflow.json": {Data: []byte(`{"code":"h","draft":{"nodes":[
		{"id":"in","type":"http_in"},
		{"id":"req","type":"http_request","config":{"method":"POST","url":"` + srv.URL + `","stream":true,
			"headers":{"Authorization":"Bearer {{ secret.espos_tts_key }}"},"body":{"a":1}}},
		{"id":"out","type":"respond","config":{"stream":true}}],
		"edges":[{"from":"in","to":"req"},{"from":"req","to":"out"}]}}`)}}
	e, err := Load(fsys, "workflows", nil)
	if err != nil {
		t.Fatal(err)
	}
	r, err := e.Run("h", map[string]any{}, nil, "http://x")
	if err != nil || r.Stream == nil {
		t.Fatalf("%+v %v", r, err)
	}
	b, _ := io.ReadAll(r.Stream)
	if string(b) != "pcm" || auth != "Bearer sk-1" {
		t.Fatalf("got %q with %q", b, auth)
	}
}

func TestResamplerOddReads(t *testing.T) {
	src := pcm(4800, func(i int) int16 { return int16(i) })
	// one byte at a time: half samples between reads must not shift the stream
	r := &resampler{src: io.NopCloser(&oneByte{b: src}), in: 48000, out: 16000}
	b, _ := io.ReadAll(r)
	if n := len(b) / 2; n < 1599 || n > 1600 {
		t.Fatalf("samples: %d", n)
	}
	for k := 0; k < 1599; k++ {
		if v := int16(binary.LittleEndian.Uint16(b[2*k:])); v != int16(3*k) {
			t.Fatalf("sample %d = %d, want %d", k, v, 3*k)
		}
	}
}

type oneByte struct{ b []byte }

func (o *oneByte) Read(p []byte) (int, error) {
	if len(o.b) == 0 {
		return 0, io.EOF
	}
	p[0] = o.b[0]
	o.b = o.b[1:]
	return 1, nil
}
