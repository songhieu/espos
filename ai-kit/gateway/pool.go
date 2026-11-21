package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/exec"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"
)

var errBusy = errors.New("all workers busy")

type VoiceInfo struct {
	ID         string `json:"id"`
	Slug       string `json:"slug"`
	Label      string `json:"label"`
	SampleRate int    `json:"sample_rate"`
}

type WorkerInfo struct {
	Engine       string      `json:"engine"`
	DefaultVoice string      `json:"default_voice"`
	Voices       []VoiceInfo `json:"voices"`
}

type Worker struct {
	model *Model
	idx   int
	port  int
	// guarded by model.mu
	ready bool
	busy  int // requests in flight (at most model.slots())
}

func (w *Worker) name() string { return fmt.Sprintf("%s#%d", w.model.cfg.ID, w.idx+1) }
func (w *Worker) url(p string) string {
	return fmt.Sprintf("http://127.0.0.1:%d%s", w.port, p)
}

// Model is a pool of identical worker processes. A worker serves slots() requests
// at a time (1 for TTS); requests wait (up to queue_timeout) for a free slot.
type Model struct {
	cfg     ModelConfig
	gw      *Gateway
	workers []*Worker

	mu      sync.Mutex
	changed chan struct{} // closed and replaced whenever a worker frees up / becomes ready
	waiting int
	info    *WorkerInfo
}

func newModel(gw *Gateway, cfg ModelConfig, firstPort int) *Model {
	m := &Model{cfg: cfg, gw: gw, changed: make(chan struct{})}
	for i := 0; i < cfg.Workers; i++ {
		m.workers = append(m.workers, &Worker{model: m, idx: i, port: firstPort + i})
	}
	return m
}

// slots: requests one worker process serves at the same time.
func (m *Model) slots() int {
	if m.cfg.native() && m.cfg.Slots > 0 {
		return m.cfg.Slots
	}
	return 1
}

func (m *Model) notifyLocked() {
	close(m.changed)
	m.changed = make(chan struct{})
}

func (m *Model) acquire(ctx context.Context, timeout time.Duration, maxWaiting int) (*Worker, error) {
	m.mu.Lock()
	if m.waiting >= maxWaiting {
		m.mu.Unlock()
		return nil, errBusy
	}
	m.waiting++
	m.mu.Unlock()
	defer func() { m.mu.Lock(); m.waiting--; m.mu.Unlock() }()

	timer := time.NewTimer(timeout)
	defer timer.Stop()
	for {
		m.mu.Lock()
		var best *Worker // the least busy ready worker with a free slot
		for _, w := range m.workers {
			if w.ready && w.busy < m.slots() && (best == nil || w.busy < best.busy) {
				best = w
			}
		}
		if best != nil {
			best.busy++
			m.mu.Unlock()
			return best, nil
		}
		ch := m.changed
		m.mu.Unlock()
		select {
		case <-ch:
		case <-timer.C:
			return nil, errBusy
		case <-ctx.Done():
			return nil, ctx.Err()
		}
	}
}

// tryAcquire grabs one specific worker only if it is ready and idle.
func (m *Model) tryAcquire(w *Worker) bool {
	m.mu.Lock()
	defer m.mu.Unlock()
	if w.ready && w.busy == 0 {
		w.busy++
		return true
	}
	return false
}

// keepWarm makes an idle worker read a short phrase every `every`, so macOS
// does not compress/swap the model out while the Mac is busy with other work
// (a swapped-out model makes the next request take seconds to start).
func (w *Worker) keepWarm(ctx context.Context, every time.Duration) {
	if every <= 0 {
		return
	}
	t := time.NewTicker(every)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
		}
		if !w.model.tryAcquire(w) {
			continue // busy or restarting: it is warm anyway / nothing to do
		}
		rctx, cancel := context.WithTimeout(ctx, time.Minute)
		req, _ := http.NewRequestWithContext(rctx, "POST", w.url("/synth"),
			strings.NewReader(`{"text":"Xin chào.","quiet":true}`))
		if w.model.cfg.native() { // llm-server / stt-server: one forward pass on a spare sequence
			req, _ = http.NewRequestWithContext(rctx, "POST", w.url("/warm"), nil)
		}
		req.Header.Set("X-Worker-Token", w.model.gw.workerToken)
		if resp, err := workerClient.Do(req); err == nil {
			io.Copy(io.Discard, resp.Body)
			resp.Body.Close()
		}
		cancel()
		w.model.release(w)
	}
}

func (m *Model) release(w *Worker) {
	m.mu.Lock()
	w.busy--
	m.notifyLocked()
	m.mu.Unlock()
}

func (m *Model) setReady(w *Worker, ready bool, info *WorkerInfo) {
	m.mu.Lock()
	w.ready = ready
	if info != nil {
		m.info = info
	}
	m.notifyLocked()
	m.mu.Unlock()
}

type ModelStatus struct {
	ID           string      `json:"id"`
	Kind         string      `json:"kind"`
	Engine       string      `json:"engine"`
	Workers      int         `json:"workers"`
	Slots        int         `json:"slots"`
	Ready        int         `json:"ready"`
	Busy         int         `json:"busy"`
	Waiting      int         `json:"waiting"`
	DefaultVoice string      `json:"default_voice,omitempty"`
	Voices       []VoiceInfo `json:"voices,omitempty"`
}

func (m *Model) status(withVoices bool) ModelStatus {
	m.mu.Lock()
	defer m.mu.Unlock()
	s := ModelStatus{ID: m.cfg.ID, Kind: m.cfg.Kind(), Engine: m.cfg.Engine, Workers: len(m.workers), Slots: m.slots(), Waiting: m.waiting}
	for _, w := range m.workers {
		if w.ready {
			s.Ready++
		}
		s.Busy += w.busy
	}
	if m.info != nil {
		s.DefaultVoice = m.info.DefaultVoice
		if withVoices {
			s.Voices = m.info.Voices
		}
	}
	return s
}

// supervise keeps one worker process alive until ctx is cancelled.
func (w *Worker) supervise(ctx context.Context, wg *sync.WaitGroup) {
	defer wg.Done()
	backoff := time.Second
	for ctx.Err() == nil {
		started := time.Now()
		err := w.runOnce(ctx)
		w.model.setReady(w, false, nil)
		if ctx.Err() != nil {
			return
		}
		log.Printf("[%s] worker exited: %v (restart in %s)", w.name(), err, backoff)
		if time.Since(started) > time.Minute {
			backoff = time.Second
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(backoff):
		}
		if backoff < 30*time.Second {
			backoff *= 2
		}
	}
}

// command: the Python TTS worker, or the native llm/stt server with its flags.
func (w *Worker) command() *exec.Cmd {
	gw := w.model.gw
	c := w.model.cfg
	if !c.native() {
		return exec.Command(gw.cfg.PythonBin, w.args()...)
	}
	a := []string{"--model", c.Model, "--alias", c.ID, "--port", fmt.Sprint(w.port),
		"--slots", fmt.Sprint(c.Slots), "--threads", fmt.Sprint(c.Threads)}
	bin := gw.cfg.LLMBin
	if c.Engine == "llm" {
		a = append(a, "--ctx", fmt.Sprint(c.Ctx))
	} else {
		bin = gw.cfg.STTBin
		a = append(a, "--language", c.Language)
		if c.VADModel != "" {
			a = append(a, "--vad-model", c.VADModel)
		}
	}
	return exec.Command(bin, append(a, c.Args...)...)
}

func (w *Worker) args() []string {
	c := w.model.cfg
	a := []string{w.model.gw.cfg.WorkerScript, "--engine", c.Engine, "--port", fmt.Sprint(w.port),
		"--threads", fmt.Sprint(c.Threads)}
	if c.DefaultVoice != "" {
		a = append(a, "--default-voice", c.DefaultVoice)
	}
	ids := make([]string, 0, len(c.Voices))
	for id := range c.Voices {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	for _, id := range ids {
		a = append(a, "--voice", id+"="+c.Voices[id])
	}
	return a
}

func (w *Worker) runOnce(ctx context.Context) error {
	gw := w.model.gw
	cmd := w.command()
	cmd.Env = append(os.Environ(), "WORKER_TOKEN="+gw.workerToken, "PYTHONUNBUFFERED=1")
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	out, _ := cmd.StdoutPipe()
	cmd.Stderr = cmd.Stdout
	if err := cmd.Start(); err != nil {
		return err
	}
	go prefixLines(w.name(), out)

	exited := make(chan error, 1)
	go func() { exited <- cmd.Wait() }()
	kill := func() {
		_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGTERM)
		select {
		case <-exited:
		case <-time.After(5 * time.Second):
			_ = syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
			<-exited
		}
	}

	// Wait until the worker answers /info (model loaded + warmed up).
	deadline := time.Now().Add(gw.cfg.StartupTimeout)
	tick := time.NewTicker(500 * time.Millisecond)
	defer tick.Stop()
	for {
		select {
		case err := <-exited:
			return fmt.Errorf("exited during start-up: %v", err)
		case <-ctx.Done():
			kill()
			return ctx.Err()
		case <-tick.C:
		}
		if info, err := w.fetchInfo(ctx); err == nil {
			w.model.setReady(w, true, info)
			if w.model.cfg.native() {
				log.Printf("[%s] ready on 127.0.0.1:%d", w.name(), w.port)
			} else {
				log.Printf("[%s] ready on 127.0.0.1:%d (%d voices)", w.name(), w.port, len(info.Voices))
			}
			break
		}
		if time.Now().After(deadline) {
			kill()
			return fmt.Errorf("not ready after %s", gw.cfg.StartupTimeout)
		}
	}

	select {
	case err := <-exited:
		return err
	case <-ctx.Done():
		kill()
		return ctx.Err()
	}
}

func (w *Worker) fetchInfo(ctx context.Context) (*WorkerInfo, error) {
	ctx, cancel := context.WithTimeout(ctx, 2*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", w.url("/info"), nil)
	req.Header.Set("X-Worker-Token", w.model.gw.workerToken)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("status %d", resp.StatusCode)
	}
	var info WorkerInfo
	if err := json.NewDecoder(resp.Body).Decode(&info); err != nil {
		return nil, err
	}
	return &info, nil
}

func prefixLines(name string, r io.Reader) {
	sc := bufio.NewScanner(r)
	sc.Buffer(make([]byte, 64*1024), 1024*1024)
	for sc.Scan() {
		log.Printf("[%s] %s", name, sc.Text())
	}
}
