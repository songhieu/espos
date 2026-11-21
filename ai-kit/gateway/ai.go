package main

// The LLM and speech-to-text routes. Both are proxied to the native servers (bin/llm-server, bin/stt-server),
// which already speak OpenAI's API; the gateway adds what it adds for speech: API keys, the per-model queue and
// slot limit, restarts and a log line per request.

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"mime"
	"mime/multipart"
	"net/http"
	"strings"
	"time"
)

// defaultModel: the first enabled model of a kind (tts, llm, stt), or nil.
func (g *Gateway) defaultModel(kind string) *Model {
	for _, m := range g.models {
		if m.cfg.Kind() == kind {
			return m
		}
	}
	return nil
}

func (g *Gateway) modelIDs(kind string) []string {
	var ids []string
	for _, m := range g.models {
		if m.cfg.Kind() == kind {
			ids = append(ids, m.cfg.ID)
		}
	}
	return ids
}

// pickModel: the model of that kind named in the request; a name this server does not have falls back to the
// default one (clients such as the EspOS server send their own model names).
func (g *Gateway) pickModel(kind, name string) *Model {
	if m := g.modelByID[name]; m != nil && m.cfg.Kind() == kind {
		return m
	}
	return g.defaultModel(kind)
}

func (g *Gateway) queueTimeout(m *Model) time.Duration {
	if m.cfg.QueueTimeout > 0 {
		return m.cfg.QueueTimeout
	}
	return g.cfg.QueueTimeout
}

// openAIError writes an error the way OpenAI clients read it.
func openAIError(w http.ResponseWriter, code int, typ, msg string) {
	writeJSON(w, code, map[string]any{"error": map[string]any{"message": msg, "type": typ, "code": code}})
}

// handleChat: POST /v1/chat/completions (stream or not) → the LLM worker.
func (g *Gateway) handleChat(w http.ResponseWriter, r *http.Request) {
	t0 := time.Now()
	keyName, _ := r.Context().Value(ctxKey{}).(string)
	body, err := io.ReadAll(io.LimitReader(r.Body, 16<<20))
	if err != nil {
		openAIError(w, 400, "invalid_request_error", "could not read the body")
		return
	}
	var head struct {
		Model  string `json:"model"`
		Stream bool   `json:"stream"`
	}
	if err := json.Unmarshal(body, &head); err != nil {
		openAIError(w, 400, "invalid_request_error", "invalid JSON: "+err.Error())
		return
	}
	md := g.pickModel("llm", head.Model)
	if md == nil {
		openAIError(w, 404, "invalid_request_error", "no LLM is enabled on this server (config.yaml: a model with engine: llm)")
		return
	}
	resp, wk, queued, ok := g.forward(w, r, md, "/v1/chat/completions", "application/json", body)
	if !ok {
		return
	}
	defer md.release(wk)
	defer resp.Body.Close()

	copyHeaders(w, resp, "Content-Type", "Cache-Control", "Retry-After")
	w.Header().Set("X-Model", md.cfg.ID)
	if head.Stream {
		w.Header().Set("X-Accel-Buffering", "no")
	}
	w.WriteHeader(resp.StatusCode)
	var tail bytes.Buffer // the non-streamed answer, for the log line
	n, first := streamCopy(w, resp.Body, func(p []byte) {
		if !head.Stream && tail.Len() < 1<<20 {
			tail.Write(p)
		}
	})
	stats := ""
	if !head.Stream {
		var out struct {
			Usage struct {
				Prompt     int `json:"prompt_tokens"`
				Completion int `json:"completion_tokens"`
				Details    struct {
					Cached int `json:"cached_tokens"`
				} `json:"prompt_tokens_details"`
			} `json:"usage"`
		}
		if json.Unmarshal(tail.Bytes(), &out) == nil {
			stats = fmt.Sprintf(" prompt=%d cached=%d completion=%d", out.Usage.Prompt, out.Usage.Details.Cached, out.Usage.Completion)
		}
	}
	log.Printf("chat key=%s model=%s status=%d stream=%v queue=%dms first=%dms total=%dms bytes=%d%s",
		keyName, md.cfg.ID, resp.StatusCode, head.Stream, queued.Milliseconds(), (queued + first).Milliseconds(),
		time.Since(t0).Milliseconds(), n, stats)
}

// handleTranscribe: POST /v1/audio/transcriptions (multipart, like OpenAI) → the STT worker.
func (g *Gateway) handleTranscribe(w http.ResponseWriter, r *http.Request) {
	t0 := time.Now()
	keyName, _ := r.Context().Value(ctxKey{}).(string)
	limit := int64(g.cfg.MaxUploadMB) << 20
	body, err := io.ReadAll(io.LimitReader(r.Body, limit+1))
	if err != nil {
		openAIError(w, 400, "invalid_request_error", "could not read the body")
		return
	}
	if int64(len(body)) > limit {
		openAIError(w, 413, "invalid_request_error", fmt.Sprintf("the recording is over %d MB (max_upload_mb)", g.cfg.MaxUploadMB))
		return
	}
	ctype := r.Header.Get("Content-Type")
	md := g.pickModel("stt", formField(ctype, body, "model"))
	if md == nil {
		openAIError(w, 404, "invalid_request_error", "no speech-to-text model is enabled on this server (config.yaml: a model with engine: stt)")
		return
	}
	resp, wk, queued, ok := g.forward(w, r, md, "/v1/audio/transcriptions", ctype, body)
	if !ok {
		return
	}
	defer md.release(wk)
	defer resp.Body.Close()
	copyHeaders(w, resp, "Content-Type", "X-Process-Ms", "X-Audio-Seconds")
	w.Header().Set("X-Model", md.cfg.ID)
	w.WriteHeader(resp.StatusCode)
	n, _ := streamCopy(w, resp.Body, nil)
	log.Printf("stt key=%s model=%s status=%d audio=%ss process=%sms queue=%dms total=%dms bytes=%d",
		keyName, md.cfg.ID, resp.StatusCode, resp.Header.Get("X-Audio-Seconds"), resp.Header.Get("X-Process-Ms"),
		queued.Milliseconds(), time.Since(t0).Milliseconds(), n)
}

// forward takes a slot of the model and sends the request to its worker. ok=false: an error was written.
func (g *Gateway) forward(w http.ResponseWriter, r *http.Request, md *Model, path, ctype string, body []byte) (*http.Response, *Worker, time.Duration, bool) {
	t0 := time.Now()
	wk, err := md.acquire(r.Context(), g.queueTimeout(md), g.cfg.MaxWaiting)
	if err != nil {
		if errors.Is(err, errBusy) {
			w.Header().Set("Retry-After", "2")
			openAIError(w, 429, "server_busy", fmt.Sprintf("model %s is busy, try again shortly", md.cfg.ID))
		}
		return nil, nil, 0, false
	}
	queued := time.Since(t0)
	req, _ := http.NewRequestWithContext(r.Context(), "POST", wk.url(path), bytes.NewReader(body))
	req.Header.Set("Content-Type", ctype)
	req.Header.Set("X-Worker-Token", g.workerToken)
	resp, err := workerClient.Do(req)
	if err != nil {
		md.release(wk)
		if r.Context().Err() == nil {
			log.Printf("[%s] worker request failed: %v", wk.name(), err)
			openAIError(w, 502, "server_error", "worker unavailable, try again")
		}
		return nil, nil, 0, false
	}
	return resp, wk, queued, true
}

// streamCopy copies the worker's answer to the client as it comes (SSE included), flushing every read.
func streamCopy(w http.ResponseWriter, src io.Reader, tap func([]byte)) (int64, time.Duration) {
	t0 := time.Now()
	flusher, _ := w.(http.Flusher)
	buf := make([]byte, 32*1024)
	var n int64
	var first time.Duration
	for {
		k, err := src.Read(buf)
		if k > 0 {
			if n == 0 {
				first = time.Since(t0)
			}
			n += int64(k)
			if tap != nil {
				tap(buf[:k])
			}
			if _, werr := w.Write(buf[:k]); werr != nil {
				return n, first // the client went away; closing the worker request cancels it there
			}
			if flusher != nil {
				flusher.Flush()
			}
		}
		if err != nil {
			return n, first
		}
	}
}

func copyHeaders(w http.ResponseWriter, resp *http.Response, names ...string) {
	for _, h := range names {
		if v := resp.Header.Get(h); v != "" {
			w.Header().Set(h, v)
		}
	}
}

// formField reads one plain field of a multipart body ("" when absent or not multipart).
func formField(ctype string, body []byte, name string) string {
	mt, params, err := mime.ParseMediaType(ctype)
	if err != nil || !strings.HasPrefix(mt, "multipart/") {
		return ""
	}
	mr := multipart.NewReader(bytes.NewReader(body), params["boundary"])
	for {
		p, err := mr.NextPart()
		if err != nil {
			return ""
		}
		if p.FormName() == name && p.FileName() == "" {
			v, _ := io.ReadAll(io.LimitReader(p, 256))
			return strings.TrimSpace(string(v))
		}
	}
}
