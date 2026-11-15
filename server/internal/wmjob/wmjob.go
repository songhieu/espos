// Package wmjob is what a background job (internal/jobs/…) calls: its input, the tables,
// storage, outbound HTTP and starting more jobs. Jobs run one at a time on one goroutine
// (see Runner), which is what lets the current job's input live in package state.
package wmjob

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"net/http"
	"sync"
	"time"

	"inkdesk/internal/wm"
)

// ErrAbort ends the current job early (the worker's fail()).
var ErrAbort = errors.New("job aborted")

var (
	curInput []byte
	curID    int64
)

// Input decodes the current job's input into v.
func Input(v any) error { return json.Unmarshal(curInput, v) }

// SetResult records the job's result (logged).
func SetResult(v any) error {
	b, _ := json.Marshal(v)
	log.Printf("job %d result: %s", curID, b)
	return nil
}

// Log writes a line to the server log.
func Log(level, msg string) { log.Printf("job %d %s: %s", curID, level, msg) }

// TableQuery reads rows (see wm.QueryOpts) and the number of matches.
func TableQuery(table string, opts map[string]any) ([]map[string]any, int, error) {
	return wm.QueryPage(table, opts)
}

// TableInsert adds a row.
func TableInsert(table string, values map[string]any) (map[string]any, error) {
	return wm.Insert(table, values)
}

// TablePatch merges values into a row.
func TablePatch(table string, id int64, values map[string]any) (map[string]any, error) {
	return wm.Update(table, id, values)
}

// TableDelete removes a row.
func TableDelete(table string, id int64) error { return wm.Delete(table, id) }

// StorageGet reads a stored file and its media type.
func StorageGet(key string) ([]byte, string, error) { return wm.StorageGet(key) }

// StoragePut writes a stored file (visibility is accepted for compatibility).
func StoragePut(key string, data []byte, mime, visibility string) (map[string]any, error) {
	if err := wm.StoragePut(key, data); err != nil {
		return nil, err
	}
	return map[string]any{"key": key, "size": len(data), "mime": mime}, nil
}

// StorageDelete removes a stored file.
func StorageDelete(key string) error { return wm.StorageDelete(key) }

// Enqueued is a started job.
type Enqueued struct {
	JobID     int64  `json:"job_id"`
	Duplicate bool   `json:"duplicate"`
	Channel   string `json:"channel"`
}

// Enqueue starts a job; a non-empty idemKey runs at most once per server run.
func Enqueue(worker string, input any, idemKey string) (Enqueued, error) {
	if Default == nil {
		return Enqueued{}, errors.New("no job runner")
	}
	id, dup, err := Default.add(worker, input, idemKey)
	return Enqueued{JobID: id, Duplicate: dup}, err
}

// Transport is an http.RoundTripper with a whole-request timeout.
type Transport struct {
	Timeout     time.Duration
	IdleTimeout time.Duration
}

// RoundTrip implements http.RoundTripper.
func (t Transport) RoundTrip(req *http.Request) (*http.Response, error) {
	if req.Header.Get("User-Agent") == "" {
		req.Header.Set("User-Agent", wm.UserAgent)
	}
	return http.DefaultTransport.RoundTrip(req)
}

// With runs fn as if it were a job with this input, then restores the current one.
func With(input any, fn func()) {
	b, err := json.Marshal(input)
	if err != nil {
		return
	}
	saved := curInput
	curInput = b
	defer func() {
		curInput = saved
		if p := recover(); p != nil && p != ErrAbort {
			log.Printf("job %d nested step crashed: %v", curID, p)
		}
	}()
	fn()
}

// Worker runs one job.
type Worker func()

type job struct {
	id     int64
	worker string
	input  []byte
}

// Runner runs queued jobs one after another.
type Runner struct {
	mu      sync.Mutex
	workers map[string]Worker
	queue   chan job
	seen    map[string]bool
	next    int64
}

// Default is the runner Enqueue uses.
var Default *Runner

// NewRunner makes the runner for these workers and starts it.
func NewRunner(workers map[string]Worker) *Runner {
	r := &Runner{workers: workers, queue: make(chan job, 256), seen: map[string]bool{}}
	go r.loop()
	Default = r
	return r
}

// Enqueue is the engine's job_enqueue node.
func (r *Runner) Enqueue(worker string, input any) (string, error) {
	id, _, err := r.add(worker, input, "")
	return fmt.Sprint(id), err
}

func (r *Runner) add(worker string, input any, idem string) (int64, bool, error) {
	if _, ok := r.workers[worker]; !ok {
		return 0, false, fmt.Errorf("no worker %q", worker)
	}
	b, err := json.Marshal(input)
	if err != nil {
		return 0, false, err
	}
	r.mu.Lock()
	if idem != "" && r.seen[idem] {
		r.mu.Unlock()
		return 0, true, nil
	}
	if idem != "" {
		r.seen[idem] = true
	}
	r.next++
	id := r.next
	r.mu.Unlock()
	select {
	case r.queue <- job{id: id, worker: worker, input: b}:
		return id, false, nil
	default:
		if idem != "" {
			r.mu.Lock()
			delete(r.seen, idem)
			r.mu.Unlock()
		}
		return 0, false, errors.New("job queue is full")
	}
}

func (r *Runner) loop() {
	for j := range r.queue {
		r.run(j)
	}
}

func (r *Runner) run(j job) {
	curInput, curID = j.input, j.id
	start := time.Now()
	defer func() {
		if p := recover(); p != nil && p != ErrAbort {
			log.Printf("job %d (%s) crashed: %v", j.id, j.worker, p)
		}
		log.Printf("job %d (%s) done in %s", j.id, j.worker, time.Since(start).Round(time.Millisecond))
	}()
	r.workers[j.worker]()
}
