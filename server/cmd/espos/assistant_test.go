package main

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

	"espos/internal/backend"
	"espos/internal/engine"
	"espos/internal/flows"
	"espos/internal/wm"
)

// The dog assistant end to end on this server, with a fake AI model, speech-to-text and speech service in place of
// the ones ESPOS_AI_*, ESPOS_STT_* and ESPOS_TTS_* name (README "The dog assistant"). Nothing leaves the machine.
func TestAssistantSelfHosted(t *testing.T) {
	var asked []map[string]any
	ai := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var req map[string]any
		json.NewDecoder(r.Body).Decode(&req)
		asked = append(asked, req)
		heard := "mấy giờ rồi"
		msgs, _ := req["messages"].([]any)
		if last, _ := msgs[len(msgs)-1].(map[string]any); last != nil {
			if s, ok := last["content"].(string); ok {
				heard = s
			}
		}
		ans, _ := json.Marshal(map[string]any{"heard": heard, "reply": "Gâu! Chủ hỏi gì Boop cũng nghe nè.", "mood": "happy", "topic": "chat"})
		json.NewEncoder(w).Encode(map[string]any{"choices": []any{map[string]any{"message": map[string]any{"content": string(ans)}}},
			"usage": map[string]any{"prompt_tokens": 10, "completion_tokens": 5}})
	}))
	defer ai.Close()
	stt := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, `{"text":"hôm nay trời thế nào"}`)
	}))
	defer stt.Close()
	tts := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write(make([]byte, 24000*2)) // one second of 24 kHz silence
	}))
	defer tts.Close()
	t.Setenv("ESPOS_AI_URL", ai.URL+"/v1")
	t.Setenv("ESPOS_AI_KEY", "test")
	t.Setenv("ESPOS_AI_MODEL", "fake-model")
	t.Setenv("ESPOS_STT_URL", "")
	t.Setenv("ESPOS_TTS_URL", "")

	open(t.TempDir())
	t.Cleanup(func() { wm.Close() })
	eng, err := engine.Load(backend.FS, "workflows", flows.Nodes)
	if err != nil {
		t.Fatal(err)
	}
	dev, err := wm.Insert("devices", map[string]any{"user_id": 1, "name": "Desk", "kind": "s3", "token_hash": "x"})
	if err != nil {
		t.Fatal(err)
	}
	id := wm.Int(dev["id"])
	guard := map[string]any{"require_device": map[string]any{"device_id": id, "user_id": 1}}

	// 1. The web's chat box: text in, the dog's words out.
	r, err := eng.Run("chat_device_dog", map[string]any{"user": map[string]any{"id": 1}, "params": map[string]any{"id": id},
		"body": map[string]any{"text": "mấy giờ rồi"}}, nil, "http://x")
	if err != nil || r.Status != 200 {
		t.Fatalf("web chat: %+v %v", r, err)
	}
	if b, _ := json.Marshal(r.Body); !strings.Contains(string(b), "Boop cũng nghe") {
		t.Fatalf("web chat body: %s", b)
	}
	if len(asked) != 1 || asked[0]["model"] != "fake-model" {
		t.Fatalf("model asked: %v", asked)
	}

	// 2. The device asks for the voice, but no speech service is set: the JSON answer (the device shows the words).
	r, err = eng.Run("device_assist", map[string]any{"guard": guard, "query": map[string]any{"stream": "1"},
		"headers": map[string]any{"content-type": "application/json"}, "body": map[string]any{"text": "kể chuyện đi"}}, nil, "http://x")
	if err != nil || r.Stream != nil || r.Status != 200 {
		t.Fatalf("no voice set: %+v %v", r, err)
	}

	// 3. A recording, a speech-to-text service and a speech service: the words go to the model as text, the voice
	// streams back at the device's 16 kHz.
	t.Setenv("ESPOS_STT_URL", stt.URL+"/v1")
	t.Setenv("ESPOS_TTS_URL", tts.URL+"/v1")
	r, err = eng.Run("device_assist", map[string]any{"guard": guard, "query": map[string]any{"stream": "1"},
		"headers": map[string]any{"content-type": "audio/wav"}, "raw": "", "raw_body_base64": base64.StdEncoding.EncodeToString(wav(16000, 16000))}, nil, "http://x")
	if err != nil || r.Stream == nil {
		t.Fatalf("voice turn: %+v %v", r, err)
	}
	pcm, _ := io.ReadAll(r.Stream)
	r.Stream.Close()
	if n := len(pcm) / 2; n < 15990 || n > 16000 || r.Headers["X-Sample-Rate"] != "16000" || r.Headers["X-Assist"] == "" {
		t.Fatalf("voice: %d samples, headers %v", n, r.Headers)
	}
	last := asked[len(asked)-1]
	if b, _ := json.Marshal(last["messages"]); !strings.Contains(string(b), "hôm nay trời thế nào") || strings.Contains(string(b), "input_audio") {
		t.Fatal("the model should get the transcribed words, not the recording")
	}
}

// wav is a 16-bit mono WAV of n samples of a soft tone.
func wav(rate, n int) []byte {
	var b bytes.Buffer
	b.WriteString("RIFF")
	binary.Write(&b, binary.LittleEndian, uint32(36+2*n))
	b.WriteString("WAVEfmt ")
	for _, v := range []any{uint32(16), uint16(1), uint16(1), uint32(rate), uint32(2 * rate), uint16(2), uint16(16)} {
		binary.Write(&b, binary.LittleEndian, v)
	}
	b.WriteString("data")
	binary.Write(&b, binary.LittleEndian, uint32(2*n))
	for i := 0; i < n; i++ {
		binary.Write(&b, binary.LittleEndian, int16((i%40-20)*300))
	}
	return b.Bytes()
}
