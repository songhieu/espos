package main

import (
	"bytes"
	"context"
	"crypto/subtle"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"
)

type TTSRequest struct {
	Model      string `json:"model"`
	Voice      string `json:"voice"`
	Text       string `json:"text"`
	Format     string `json:"format"`      // wav (default) | pcm
	SampleRate int    `json:"sample_rate"` // 0 = model's native rate
	Stream     *bool  `json:"stream"`      // default true
}

// OpenAI-compatible body for POST /v1/audio/speech.
type SpeechRequest struct {
	Model          string `json:"model"`
	Input          string `json:"input"`
	Voice          string `json:"voice"`
	ResponseFormat string `json:"response_format"`
	SampleRate     int    `json:"sample_rate"`
}

var workerClient = &http.Client{Transport: &http.Transport{
	ResponseHeaderTimeout: 2 * time.Minute,
	MaxIdleConnsPerHost:   4,
}}

func (g *Gateway) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /health", g.handleHealth)
	mux.HandleFunc("GET /v1/models", g.auth(g.handleModels))
	mux.HandleFunc("POST /v1/tts", g.auth(g.handleTTS))
	mux.HandleFunc("POST /v1/audio/speech", g.auth(g.handleSpeech))
	mux.HandleFunc("POST /v1/chat/completions", g.auth(g.handleChat))           // ai.go
	mux.HandleFunc("POST /v1/audio/transcriptions", g.auth(g.handleTranscribe)) // ai.go
	return mux
}

type ctxKey struct{}

func (g *Gateway) auth(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		got := r.Header.Get("X-API-Key")
		if h := r.Header.Get("Authorization"); got == "" && strings.HasPrefix(h, "Bearer ") {
			got = strings.TrimSpace(h[len("Bearer "):])
		}
		name := ""
		for _, k := range g.cfg.APIKeys {
			if subtle.ConstantTimeCompare([]byte(got), []byte(k.Key)) == 1 {
				name = k.Name
			}
		}
		if name == "" {
			jsonError(w, http.StatusUnauthorized, "missing or invalid API key (Authorization: Bearer <key> or X-API-Key)")
			return
		}
		next(w, r.WithContext(context.WithValue(r.Context(), ctxKey{}, name)))
	}
}

func (g *Gateway) handleHealth(w http.ResponseWriter, r *http.Request) {
	type m struct {
		Kind    string `json:"kind"`
		Ready   int    `json:"ready"`
		Workers int    `json:"workers"`
		Busy    int    `json:"busy"`
		Waiting int    `json:"waiting"`
	}
	out := map[string]m{}
	ok := true
	for _, md := range g.models {
		s := md.status(false)
		out[s.ID] = m{s.Kind, s.Ready, s.Workers, s.Busy, s.Waiting}
		ok = ok && s.Ready > 0
	}
	status := "ok"
	if !ok {
		status = "starting"
	}
	writeJSON(w, 200, map[string]any{"status": status, "models": out})
}

// handleModels: this gateway's own list ("models", "default_model" = the default TTS model) and, for OpenAI
// clients, "data" with every model.
func (g *Gateway) handleModels(w http.ResponseWriter, r *http.Request) {
	var list []ModelStatus
	var data []map[string]any
	for _, md := range g.models {
		list = append(list, md.status(true))
		data = append(data, map[string]any{"id": md.cfg.ID, "object": "model", "owned_by": "local", "kind": md.cfg.Kind()})
	}
	def := ""
	if md := g.defaultModel("tts"); md != nil {
		def = md.cfg.ID
	}
	writeJSON(w, 200, map[string]any{"models": list, "default_model": def, "object": "list", "data": data})
}

func (g *Gateway) handleSpeech(w http.ResponseWriter, r *http.Request) {
	var sr SpeechRequest
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&sr); err != nil {
		jsonError(w, 400, "invalid JSON: "+err.Error())
		return
	}
	req := TTSRequest{Model: sr.Model, Voice: sr.Voice, Text: sr.Input, Format: sr.ResponseFormat, SampleRate: sr.SampleRate}
	if req.Format == "" {
		req.Format = "wav"
	}
	if req.Format == "pcm" && req.SampleRate == 0 {
		req.SampleRate = 24000 // OpenAI's pcm is 24 kHz
	}
	g.synthesize(w, r, req)
}

func (g *Gateway) handleTTS(w http.ResponseWriter, r *http.Request) {
	var req TTSRequest
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<20)).Decode(&req); err != nil {
		jsonError(w, 400, "invalid JSON: "+err.Error())
		return
	}
	g.synthesize(w, r, req)
}

func (g *Gateway) synthesize(w http.ResponseWriter, r *http.Request, req TTSRequest) {
	t0 := time.Now()
	keyName, _ := r.Context().Value(ctxKey{}).(string)

	req.Text = strings.TrimSpace(req.Text)
	if req.Text == "" {
		jsonError(w, 400, "text is empty")
		return
	}
	if n := utf8.RuneCountInString(req.Text); n > g.cfg.MaxChars {
		jsonError(w, 400, fmt.Sprintf("text too long: %d chars (max_chars = %d)", n, g.cfg.MaxChars))
		return
	}
	format := strings.ToLower(req.Format)
	if format == "" {
		format = "wav"
	}
	if format != "wav" && format != "pcm" {
		jsonError(w, 400, "format must be wav or pcm")
		return
	}
	if req.SampleRate != 0 && (req.SampleRate < 8000 || req.SampleRate > 48000) {
		jsonError(w, 400, "sample_rate must be 8000..48000 (or 0 for the model's native rate)")
		return
	}
	stream := req.Stream == nil || *req.Stream

	md := g.defaultModel("tts")
	if req.Model != "" {
		md = g.modelByID[req.Model]
		if md == nil || md.cfg.Kind() != "tts" {
			jsonError(w, 400, fmt.Sprintf("unknown TTS model %q, available: %s", req.Model, strings.Join(g.modelIDs("tts"), ", ")))
			return
		}
	}
	if md == nil {
		jsonError(w, 404, "no TTS model is enabled on this server")
		return
	}

	wk, err := md.acquire(r.Context(), g.queueTimeout(md), g.cfg.MaxWaiting)
	if err != nil {
		if errors.Is(err, errBusy) {
			w.Header().Set("Retry-After", "2")
			jsonError(w, 429, fmt.Sprintf("model %s is busy, try again shortly", md.cfg.ID))
		}
		return
	}
	defer md.release(wk)
	queued := time.Since(t0)

	body, _ := json.Marshal(map[string]any{"text": req.Text, "voice": req.Voice, "sample_rate": req.SampleRate})
	wreq, _ := http.NewRequestWithContext(r.Context(), "POST", wk.url("/synth"), bytes.NewReader(body))
	wreq.Header.Set("Content-Type", "application/json")
	wreq.Header.Set("X-Worker-Token", g.workerToken)
	resp, err := workerClient.Do(wreq)
	if err != nil {
		if r.Context().Err() == nil {
			log.Printf("[%s] worker request failed: %v", wk.name(), err)
			jsonError(w, 502, "worker unavailable, try again")
		}
		return
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		w.Header().Set("Content-Type", "application/json")
		w.WriteHeader(resp.StatusCode)
		io.Copy(w, resp.Body)
		return
	}
	rate, _ := strconv.Atoi(resp.Header.Get("X-Sample-Rate"))
	voice := resp.Header.Get("X-Voice") // ASCII slug, e.g. hai-dang
	voiceName := resp.Header.Get("X-Voice-Name")
	if s, err := url.PathUnescape(voiceName); err == nil && s != "" {
		voice = s // for the log
	}

	h := w.Header()
	h.Set("X-Model", md.cfg.ID)
	h.Set("X-Voice", resp.Header.Get("X-Voice"))
	h.Set("X-Voice-Name", voiceName) // percent-encoded UTF-8, e.g. H%E1%BA%A3i%20%C4%90%C4%83ng
	h.Set("X-Sample-Rate", strconv.Itoa(rate))
	h.Set("X-Channels", "1")
	h.Set("X-Bits-Per-Sample", "16")
	h.Set("Cache-Control", "no-store")
	if format == "wav" {
		h.Set("Content-Type", "audio/wav")
	} else {
		h.Set("Content-Type", "audio/pcm")
	}

	var nBytes int64
	var firstByte time.Duration
	if !stream {
		pcm, err := io.ReadAll(resp.Body)
		if err != nil {
			jsonError(w, 502, "synthesis failed: "+err.Error())
			return
		}
		firstByte = time.Since(t0)
		out := pcm
		if format == "wav" {
			out = append(wavHeader(rate, uint32(len(pcm))), pcm...)
		}
		h.Set("Content-Length", strconv.Itoa(len(out)))
		w.WriteHeader(200)
		w.Write(out)
		nBytes = int64(len(pcm))
	} else {
		flusher, _ := w.(http.Flusher)
		h.Set("X-Accel-Buffering", "no")
		w.WriteHeader(200)
		if format == "wav" {
			w.Write(wavHeader(rate, 0xFFFFFFFF-36)) // unknown length: plays while streaming
		}
		// Only ever write whole 16-bit samples (even byte counts). Proxies such as
		// Cloudflare may still re-split the stream, so clients must keep a stray
		// odd byte for the next read anyway — see README "Đọc PCM đúng cách".
		buf := make([]byte, 16*1024)
		out := make([]byte, 0, len(buf)+1)
		var carry []byte
		for {
			n, rerr := resp.Body.Read(buf)
			if n > 0 {
				out = append(append(out[:0], carry...), buf[:n]...)
				even := len(out) &^ 1
				carry = append(carry[:0], out[even:]...)
				if even > 0 {
					if nBytes == 0 {
						firstByte = time.Since(t0)
					}
					nBytes += int64(even)
					if _, werr := w.Write(out[:even]); werr != nil {
						break // client went away
					}
					if flusher != nil {
						flusher.Flush()
					}
				}
			}
			if rerr != nil {
				break
			}
		}
	}
	audioSec := 0.0
	if rate > 0 {
		audioSec = float64(nBytes) / 2 / float64(rate)
	}
	log.Printf("tts key=%s model=%s voice=%s chars=%d queue=%dms first=%dms total=%dms audio=%.2fs %s/%dHz",
		keyName, md.cfg.ID, voice, utf8.RuneCountInString(req.Text), queued.Milliseconds(),
		firstByte.Milliseconds(), time.Since(t0).Milliseconds(), audioSec, format, rate)
}

// wavHeader builds a 44-byte PCM s16le mono header for dataLen bytes of audio.
func wavHeader(rate int, dataLen uint32) []byte {
	b := make([]byte, 44)
	copy(b[0:], "RIFF")
	binary.LittleEndian.PutUint32(b[4:], 36+dataLen)
	copy(b[8:], "WAVEfmt ")
	binary.LittleEndian.PutUint32(b[16:], 16)
	binary.LittleEndian.PutUint16(b[20:], 1) // PCM
	binary.LittleEndian.PutUint16(b[22:], 1) // mono
	binary.LittleEndian.PutUint32(b[24:], uint32(rate))
	binary.LittleEndian.PutUint32(b[28:], uint32(rate*2))
	binary.LittleEndian.PutUint16(b[32:], 2)
	binary.LittleEndian.PutUint16(b[34:], 16)
	copy(b[36:], "data")
	binary.LittleEndian.PutUint32(b[40:], dataLen)
	return b
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(code)
	enc := json.NewEncoder(w)
	enc.SetEscapeHTML(false)
	enc.SetIndent("", "  ")
	enc.Encode(v)
}

func jsonError(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, map[string]string{"error": msg})
}
