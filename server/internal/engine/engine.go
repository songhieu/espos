// Package engine runs the workflows in internal/backend/workflows: small graphs of nodes
// (http_in → code → branch → respond, …) whose code nodes are the Go functions in
// internal/flows. It understands the node types those graphs use, and nothing more.
package engine

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"path"
	"strings"
	"time"
	"unicode/utf8"

	"espos/internal/wm"
)

// NodeFunc is a code node: its input map and the run's context ({end_user_id}).
type NodeFunc func(in, ctx map[string]any) map[string]any

// Node is one node of a graph.
type Node struct {
	ID     string         `json:"id"`
	Type   string         `json:"type"`
	Config map[string]any `json:"config"`
}

// Edge joins two nodes; Port is "", "error", "true" or "false".
type Edge struct {
	From string `json:"from"`
	To   string `json:"to"`
	Port string `json:"port"`
}

// Flow is one workflow.
type Flow struct {
	Code  string
	Nodes map[string]Node
	Out   map[string][]Edge
	Start string
	Cron  string // the schedule_trigger expression, if any
}

// Result is how a run ended: a respond node (an HTTP answer) or a return node.
type Result struct {
	Status  int
	Headers map[string]string
	Body    any
	Data    any
	Deny    bool
	Stream  io.ReadCloser // a respond node with stream: true: this body is sent as it comes (then closed)
}

// Enqueuer starts a background job.
type Enqueuer func(worker string, input any) (string, error)

// Engine holds the loaded flows.
type Engine struct {
	Flows   map[string]*Flow
	Nodes   map[string]NodeFunc // "<workflow>/<node file stem>"
	Enqueue Enqueuer
	// Base is the server's public address for links made outside a request (schedules).
	Base string
	// ScheduleGate, when set, can skip a scheduled run (false = skip this time).
	ScheduleGate func(code string) bool
}

// Load reads every workflows/<code>/workflow.json under dir.
func Load(fsys fs.FS, dir string, nodes map[string]NodeFunc) (*Engine, error) {
	e := &Engine{Flows: map[string]*Flow{}, Nodes: nodes}
	entries, err := fs.ReadDir(fsys, dir)
	if err != nil {
		return nil, err
	}
	for _, ent := range entries {
		if !ent.IsDir() {
			continue
		}
		raw, err := fs.ReadFile(fsys, path.Join(dir, ent.Name(), "workflow.json"))
		if err != nil {
			continue
		}
		var doc struct {
			Code  string `json:"code"`
			Draft struct {
				Nodes []Node `json:"nodes"`
				Edges []Edge `json:"edges"`
			} `json:"draft"`
		}
		if err := json.Unmarshal(raw, &doc); err != nil {
			return nil, fmt.Errorf("%s: %w", ent.Name(), err)
		}
		f := &Flow{Code: doc.Code, Nodes: map[string]Node{}, Out: map[string][]Edge{}}
		for _, n := range doc.Draft.Nodes {
			f.Nodes[n.ID] = n
			switch n.Type {
			case "http_in", "function_in":
				f.Start = n.ID
			case "schedule_trigger":
				f.Start = n.ID
				f.Cron = wm.Str(n.Config["expression"])
			case "code":
				key := doc.Code + "/" + nodeStem(n)
				if _, ok := nodes[key]; !ok {
					return nil, fmt.Errorf("%s: no Go code for node %s", doc.Code, key)
				}
			}
		}
		for _, ed := range doc.Draft.Edges {
			f.Out[ed.From] = append(f.Out[ed.From], ed)
		}
		if f.Start == "" {
			return nil, fmt.Errorf("%s: no trigger node", doc.Code)
		}
		e.Flows[f.Code] = f
	}
	return e, nil
}

// nodeStem is the code file a node runs: {"source":{"$file":"nodes/work.go"}} → "work".
func nodeStem(n Node) string {
	src, _ := n.Config["source"].(map[string]any)
	file := wm.Str(src["$file"])
	return strings.TrimSuffix(path.Base(file), ".go")
}

// Run executes a flow from its trigger with `input` as {{ input }}.
func (e *Engine) Run(code string, input map[string]any, ctx map[string]any, base string) (*Result, error) {
	f, ok := e.Flows[code]
	if !ok {
		return nil, fmt.Errorf("no workflow %q", code)
	}
	if ctx == nil {
		ctx = map[string]any{}
	}
	if base == "" {
		base = e.Base
	}
	vars := map[string]any{"input": input}
	loops := map[string]*loop{}
	cur, prev := f.Start, ""
	for steps := 0; steps < maxSteps; steps++ {
		n := f.Nodes[cur]
		var (
			port   string
			failed bool
			res    *Result
			err    error
		)
		if n.Type == "for_each" {
			port = forEach(n, loops, vars, prev)
		} else {
			port, failed, res, err = e.step(f, n, vars, ctx, base)
		}
		if err != nil {
			return nil, err
		}
		if res != nil {
			return res, nil
		}
		next := ""
		for _, ed := range f.Out[cur] {
			switch {
			case failed && ed.Port == "error":
				next = ed.To
			case !failed && ed.Port == port:
				next = ed.To
			}
			if next != "" {
				break
			}
		}
		if next == "" {
			if failed {
				out, _ := vars[n.ID].(map[string]any)
				return nil, fmt.Errorf("%s/%s failed: %s", code, n.ID, wm.Str(out["error"]))
			}
			return &Result{Status: 204}, nil
		}
		prev, cur = cur, next
	}
	return nil, fmt.Errorf("%s: too many steps", code)
}

const (
	maxSteps     = 5000 // node runs in one run, loop bodies included
	maxLoopItems = 500  // elements a for_each walks
)

// loop is a for_each node's walk through its list.
type loop struct {
	items   []any
	next    int
	results []any
}

// forEach runs a for_each node, as the hosted platform does: the first visit takes the list
// ({"items": "{{ … }}"}) and leaves by "item" with {{ id.item }}, {{ id.index }}, {{ id.total }};
// the body's last node draws an edge back, and each return records that node's output; once the
// list is walked it leaves by "done" with {{ id.results }} (every body result, in order).
func forEach(n Node, loops map[string]*loop, vars map[string]any, prev string) string {
	l := loops[n.ID]
	if l == nil {
		l = &loop{items: asList(eval(n.Config["items"], vars)), results: []any{}}
		if len(l.items) > maxLoopItems {
			log.Printf("%s: %d items, only the first %d walked", n.ID, len(l.items), maxLoopItems)
			l.items = l.items[:maxLoopItems]
		}
		loops[n.ID] = l
	} else {
		l.results = append(l.results, vars[prev])
	}
	if l.next < len(l.items) {
		i := l.next
		l.next++
		vars[n.ID] = map[string]any{"item": l.items[i], "index": i, "total": len(l.items), "results": l.results}
		return "item"
	}
	delete(loops, n.ID)
	vars[n.ID] = map[string]any{"results": l.results, "total": len(l.items)}
	return "done"
}

// asList is a list value as []any: code nodes here hand over typed Go slices
// ([]map[string]any, …) where the hosted platform passes JSON.
func asList(v any) []any {
	switch t := v.(type) {
	case nil:
		return nil
	case []any:
		return t
	}
	b, err := json.Marshal(v)
	if err != nil {
		return nil
	}
	var out []any
	if json.Unmarshal(b, &out) != nil {
		return nil
	}
	return out
}

// step runs one node: the port it leaves by, whether it failed, or the run's end.
func (e *Engine) step(f *Flow, n Node, vars, ctx map[string]any, base string) (string, bool, *Result, error) {
	cfg := n.Config
	switch n.Type {
	case "http_in", "function_in", "schedule_trigger":
		return "", false, nil, nil

	case "code":
		in, _ := eval(cfg["input"], vars).(map[string]any)
		if in == nil {
			in = map[string]any{}
		}
		out := e.callNode(f.Code+"/"+nodeStem(n), in, ctx)
		vars[n.ID] = out
		return "", wm.Failed(out), nil, nil

	case "branch":
		rules, _ := cfg["rules"].([]any)
		anyRule := wm.Str(cfg["match"]) == "any"
		ok := !anyRule
		for _, r := range rules {
			rm, _ := r.(map[string]any)
			hit := compare(eval(rm["left"], vars), wm.Str(rm["op"]), eval(rm["right"], vars))
			if anyRule && hit {
				ok = true
			}
			if !anyRule && !hit {
				ok = false
			}
		}
		if ok {
			return "true", false, nil, nil
		}
		return "false", false, nil, nil

	case "respond":
		status := wm.Int(eval(cfg["status"], vars))
		if status == 0 {
			status = 200
		}
		res := &Result{Status: status, Body: eval(cfg["body"], vars), Headers: map[string]string{}}
		if hs, ok := eval(cfg["headers"], vars).(map[string]any); ok {
			for k, v := range hs {
				res.Headers[k] = wm.Str(v)
			}
		}
		if wm.Bool(cfg["stream"]) {
			res.Stream, _ = vars[streamKey].(io.ReadCloser)
			delete(vars, streamKey)
		}
		return "", false, res, nil

	case "http_request":
		out, stream, err := httpRequest(cfg, vars)
		if out == nil {
			out = map[string]any{}
		}
		if err != nil {
			wm.Log(f.Code, "/", n.ID, ": ", err.Error())
			out["error"] = err.Error()
			vars[n.ID] = out
			return "error", true, nil, nil
		}
		if stream != nil {
			vars[streamKey] = stream
		}
		vars[n.ID] = out
		return "", false, nil, nil

	case "tts": // the hosted speech service's place (tools/importer): the voice from ESPOS_TTS_* (net.go)
		text := wm.Str(eval(cfg["text"], vars))
		voice := wm.Str(eval(cfg["voice"], vars))
		rate := wm.Int(eval(cfg["rate"], vars))
		stream, err := ttsStream(text, voice, rate)
		if err != nil {
			wm.Log(f.Code, "/", n.ID, ": ", err.Error())
			vars[n.ID] = map[string]any{"error": err.Error()}
			return "error", true, nil, nil
		}
		vars[streamKey] = stream
		vars[n.ID] = map[string]any{"status": float64(200)}
		return "", false, nil, nil

	case "return":
		res := &Result{Data: eval(cfg["data"], vars), Deny: wm.Bool(cfg["deny"]), Status: wm.Int(eval(cfg["status"], vars)), Body: eval(cfg["body"], vars)}
		return "", false, res, nil

	case "storage_sign":
		key := wm.Str(eval(cfg["key"], vars))
		if key == "" {
			vars[n.ID] = map[string]any{"error": "no key to sign"}
			return "", true, nil, nil
		}
		ttl := wm.Int(eval(cfg["ttl_seconds"], vars))
		if ttl <= 0 {
			ttl = 900
		}
		method := strings.ToUpper(wm.Str(cfg["method"]))
		if method == "" {
			method = "GET"
		}
		u := wm.SignURL(base, key, method, time.Duration(ttl)*time.Second, int64(wm.Num(eval(cfg["max_bytes"], vars))))
		vars[n.ID] = map[string]any{"url": u, "method": method, "key": key}
		return "", false, nil, nil

	case "storage_get":
		// As the hosted node: content_base64 always, content only when the file is text.
		key := wm.Str(eval(cfg["key"], vars))
		b, mime, err := wm.StorageGet(key)
		if key == "" || err != nil {
			msg := "no key to read"
			if err != nil {
				msg = err.Error()
			}
			vars[n.ID] = map[string]any{"error": msg}
			return "", true, nil, nil
		}
		out := map[string]any{"key": key, "mime": mime, "size": len(b), "content_base64": base64.StdEncoding.EncodeToString(b)}
		if utf8.Valid(b) && !bytes.ContainsRune(b, 0) {
			out["content"] = string(b)
		}
		vars[n.ID] = out
		return "", false, nil, nil

	case "storage_put":
		// As the hosted node: {"key", "content_base64"} (or "content" as text) writes the file; {key, size} after.
		key := wm.Str(eval(cfg["key"], vars))
		var data []byte
		var err error
		if b64 := wm.Str(eval(cfg["content_base64"], vars)); b64 != "" {
			data, err = base64.StdEncoding.DecodeString(b64)
		} else {
			data = []byte(wm.Str(eval(cfg["content"], vars)))
		}
		if key == "" || err == nil && len(data) == 0 {
			err = errors.New("storage_put needs a key and content")
		}
		if err == nil {
			err = wm.StoragePut(key, data)
		}
		if err != nil {
			vars[n.ID] = map[string]any{"error": err.Error()}
			return "", true, nil, nil
		}
		vars[n.ID] = map[string]any{"key": key, "size": len(data)}
		return "", false, nil, nil

	case "publish":
		// Realtime channels (the hosted platform's push to a listening client) do not exist on this server: the
		// message is dropped and the run goes on; clients find the change at their next sync or poll.
		vars[n.ID] = map[string]any{"ok": true, "delivered": 0}
		return "", false, nil, nil

	case "stream_token":
		// A token for the hosted platform's realtime stream (a WebSocket a client subscribes to): there is none here,
		// so the node fails and the run takes its error edge; clients keep to their sync and poll.
		vars[n.ID] = map[string]any{"error": "no realtime stream on this server: sync or poll instead"}
		return "", true, nil, nil

	case "call_workflow":
		in, _ := eval(cfg["input"], vars).(map[string]any)
		res, err := e.Run(wm.Str(cfg["workflow"]), in, ctx, base)
		if err != nil {
			vars[n.ID] = map[string]any{"error": err.Error()}
			return "", true, nil, nil
		}
		out, _ := res.Data.(map[string]any)
		if out == nil {
			out = map[string]any{}
		}
		vars[n.ID] = out
		return "", false, nil, nil

	case "job_enqueue":
		if e.Enqueue == nil {
			vars[n.ID] = map[string]any{"error": "no job runner"}
			return "", true, nil, nil
		}
		id, err := e.Enqueue(wm.Str(cfg["worker"]), eval(cfg["input"], vars))
		if err != nil {
			vars[n.ID] = map[string]any{"error": err.Error()}
			return "", true, nil, nil
		}
		vars[n.ID] = map[string]any{"job_id": id, "id": id}
		return "", false, nil, nil
	}
	return "", false, nil, fmt.Errorf("%s/%s: node type %q is not supported", f.Code, n.ID, n.Type)
}

// callNode runs a code node; a panic fails the node instead of the server.
func (e *Engine) callNode(key string, in, ctx map[string]any) (out map[string]any) {
	defer func() {
		if r := recover(); r != nil {
			log.Printf("node %s panicked: %v", key, r)
			out = wm.Fail(fmt.Sprint(r))
		}
	}()
	fn := e.Nodes[key]
	if fn == nil {
		return wm.Fail(errors.New("missing node " + key))
	}
	out = fn(in, ctx)
	if out == nil {
		out = map[string]any{}
	}
	return out
}

func compare(l any, op string, r any) bool {
	switch op {
	case "eq", "":
		return equal(l, r)
	case "ne", "neq":
		return !equal(l, r)
	case "gt":
		return wm.Num(l) > wm.Num(r)
	case "gte":
		return wm.Num(l) >= wm.Num(r)
	case "lt":
		return wm.Num(l) < wm.Num(r)
	case "lte":
		return wm.Num(l) <= wm.Num(r)
	case "exists":
		return l != nil && l != ""
	case "empty":
		return l == nil || l == ""
	case "contains":
		return strings.Contains(wm.Str(l), wm.Str(r))
	}
	return false
}

func equal(l, r any) bool {
	switch rv := r.(type) {
	case bool:
		lb, ok := l.(bool)
		return ok && lb == rv
	case float64, int, int64:
		switch l.(type) {
		case float64, int, int64, string:
			return wm.Num(l) == wm.Num(rv) && wm.Str(l) != ""
		}
		return false
	case nil:
		return l == nil
	}
	return wm.Str(l) == wm.Str(r)
}
