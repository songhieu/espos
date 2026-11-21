package engine

import (
	"encoding/base64"
	"testing"
	"testing/fstest"
	"time"

	"espos/internal/wm"
)

func TestCronMatch(t *testing.T) {
	at := func(h, m int) time.Time { return time.Date(2026, 11, 15, h, m, 0, 0, time.UTC) }
	cases := []struct {
		expr string
		t    time.Time
		want bool
	}{
		{"*/15 * * * *", at(9, 45), true},
		{"*/15 * * * *", at(9, 46), false},
		{"7 * * * *", at(3, 7), true},
		{"7 * * * *", at(3, 8), false},
		{"0 6-8 * * *", at(7, 0), true},
		{"0 6-8 * * *", at(9, 0), false},
		{"30 6,18 * * 0", at(18, 30), true}, // 15 Nov 2026 is a Sunday
	}
	for _, c := range cases {
		if got := cronMatch(c.expr, c.t); got != c.want {
			t.Errorf("cronMatch(%q, %s) = %v", c.expr, c.t.Format("15:04"), got)
		}
	}
}

func TestEvalKeepsTypes(t *testing.T) {
	vars := map[string]any{"input": map[string]any{"params": map[string]any{"id": "7"}}, "work": map[string]any{"status": 201.0, "body": map[string]any{"ok": true}}}
	if v := eval("{{ work.status }}", vars); v != 201.0 {
		t.Fatalf("whole reference: got %#v", v)
	}
	if v := eval("/v1/x/{{ input.params.id }}", vars); v != "/v1/x/7" {
		t.Fatalf("inside text: got %#v", v)
	}
	if v, _ := eval(map[string]any{"b": "{{ work.body }}"}, vars).(map[string]any); v["b"].(map[string]any)["ok"] != true {
		t.Fatalf("nested: got %#v", v)
	}
	if v := eval("{{ missing.thing }}", vars); v != nil {
		t.Fatalf("missing: got %#v", v)
	}
}

func TestRunBranchAndRespond(t *testing.T) {
	fsys := fstest.MapFS{"workflows/hello/workflow.json": {Data: []byte(`{"code":"hello","draft":{
		"nodes":[{"id":"in","type":"http_in"},
			{"id":"work","type":"code","config":{"source":{"$file":"nodes/work.go"},"input":{"name":"{{ input.body.name }}"}}},
			{"id":"ok","type":"branch","config":{"rules":[{"left":"{{ work.ok }}","op":"eq","right":true}]}},
			{"id":"yes","type":"respond","config":{"status":200,"body":{"hi":"{{ work.msg }}"}}},
			{"id":"no","type":"respond","config":{"status":422,"body":{"error":"empty"}}},
			{"id":"boom","type":"respond","config":{"status":500,"body":{"error":"{{ work.error }}"}}}],
		"edges":[{"from":"in","to":"work"},{"from":"work","to":"ok"},{"from":"work","to":"boom","port":"error"},
			{"from":"ok","to":"yes","port":"true"},{"from":"ok","to":"no","port":"false"}]}}`)}}
	nodes := map[string]NodeFunc{"hello/work": func(in, ctx map[string]any) map[string]any {
		name, _ := in["name"].(string)
		if name == "fail" {
			return map[string]any{"__fail": true, "error": "asked to fail"}
		}
		return map[string]any{"ok": name != "", "msg": "chào " + name}
	}}
	e, err := Load(fsys, "workflows", nodes)
	if err != nil {
		t.Fatal(err)
	}
	run := func(name string) *Result {
		r, err := e.Run("hello", map[string]any{"body": map[string]any{"name": name}}, nil, "http://x")
		if err != nil {
			t.Fatal(err)
		}
		return r
	}
	if r := run("An"); r.Status != 200 || r.Body.(map[string]any)["hi"] != "chào An" {
		t.Fatalf("true branch: %+v", r)
	}
	if r := run(""); r.Status != 422 {
		t.Fatalf("false branch: %+v", r)
	}
	if r := run("fail"); r.Status != 500 || r.Body.(map[string]any)["error"] != "asked to fail" {
		t.Fatalf("error edge: %+v", r)
	}
}

func TestRunForEach(t *testing.T) {
	fsys := fstest.MapFS{"workflows/loop/workflow.json": {Data: []byte(`{"code":"loop","draft":{
		"nodes":[{"id":"in","type":"http_in"},
			{"id":"list","type":"code","config":{"source":{"$file":"nodes/list.go"},"input":{}}},
			{"id":"each","type":"for_each","config":{"items":"{{ list.items }}"}},
			{"id":"dbl","type":"code","config":{"source":{"$file":"nodes/dbl.go"},"input":{"n":"{{ each.item }}","i":"{{ each.index }}"}}},
			{"id":"out","type":"respond","config":{"status":200,"body":{"r":"{{ each.results }}","total":"{{ each.total }}"}}}],
		"edges":[{"from":"in","to":"list"},{"from":"list","to":"each"},{"from":"each","to":"dbl","port":"item"},
			{"from":"dbl","to":"each"},{"from":"each","to":"out","port":"done"}]}}`)}}
	nodes := map[string]NodeFunc{
		// a typed Go slice, as code nodes here return
		"loop/list": func(in, ctx map[string]any) map[string]any { return map[string]any{"items": []int{1, 2, 3}} },
		"loop/dbl": func(in, ctx map[string]any) map[string]any {
			n, _ := in["n"].(float64)
			return map[string]any{"v": n * 2, "i": in["i"]}
		},
	}
	e, err := Load(fsys, "workflows", nodes)
	if err != nil {
		t.Fatal(err)
	}
	res, err := e.Run("loop", map[string]any{}, nil, "")
	if err != nil {
		t.Fatal(err)
	}
	body, _ := res.Body.(map[string]any)
	r, _ := body["r"].([]any)
	if len(r) != 3 || body["total"] != 3 {
		t.Fatalf("results %#v", body)
	}
	for i, x := range r {
		m, _ := x.(map[string]any)
		if m["v"] != float64(2*(i+1)) || m["i"] != i {
			t.Fatalf("result %d = %#v", i, m)
		}
	}
	// an empty list goes straight to done
	nodes["loop/list"] = func(in, ctx map[string]any) map[string]any { return map[string]any{"items": nil} }
	res, err = e.Run("loop", map[string]any{}, nil, "")
	if err != nil {
		t.Fatal(err)
	}
	if b, _ := res.Body.(map[string]any); len(b["r"].([]any)) != 0 {
		t.Fatalf("empty list: %#v", b)
	}
}

// storage_put writes the decoded file (read back by storage_get), and publish lets the run go on: confirm_user_pkg and
// desk_queue use them since the 0.5.9 import.
func TestRunStoragePutAndPublish(t *testing.T) {
	if err := wm.OpenStorage(t.TempDir(), []byte("0123456789abcdef0123456789abcdef")); err != nil {
		t.Fatal(err)
	}
	fsys := fstest.MapFS{"workflows/put/workflow.json": {Data: []byte(`{"code":"put","draft":{
		"nodes":[{"id":"in","type":"function_in"},
			{"id":"put","type":"storage_put","config":{"key":"{{ input.key }}","content_base64":"{{ input.b64 }}"}},
			{"id":"pub","type":"publish","config":{"channel":"rt:x","data":{"t":"sync"}}},
			{"id":"get","type":"storage_get","config":{"key":"{{ put.key }}"}},
			{"id":"out","type":"return","config":{"data":{"size":"{{ put.size }}","text":"{{ get.content }}","pub":"{{ pub.ok }}"}}},
			{"id":"bad","type":"return","config":{"data":{"error":"{{ put.error }}"}}}],
		"edges":[{"from":"in","to":"put"},{"from":"put","to":"pub"},{"from":"put","to":"bad","port":"error"},
			{"from":"pub","to":"get"},{"from":"get","to":"out"}]}}`)}}
	e, err := Load(fsys, "workflows", nil)
	if err != nil {
		t.Fatal(err)
	}
	r, err := e.Run("put", map[string]any{"key": "u/1/a.txt", "b64": base64.StdEncoding.EncodeToString([]byte("hej"))}, nil, "http://x")
	if err != nil {
		t.Fatal(err)
	}
	d, _ := r.Data.(map[string]any)
	if wm.Int(d["size"]) != 3 || d["text"] != "hej" || d["pub"] != true {
		t.Fatalf("put + publish + get: %+v", d)
	}
	r, err = e.Run("put", map[string]any{"key": "u/1/b.txt", "b64": ""}, nil, "http://x")
	if err != nil {
		t.Fatal(err)
	}
	if d, _ := r.Data.(map[string]any); wm.Str(d["error"]) == "" {
		t.Fatalf("empty content should take the error edge: %+v", d)
	}
}

// stream_token has no realtime stream behind it here: the run takes the node's error edge.
func TestRunStreamTokenFails(t *testing.T) {
	fsys := fstest.MapFS{"workflows/tok/workflow.json": {Data: []byte(`{"code":"tok","draft":{
		"nodes":[{"id":"in","type":"function_in"},
			{"id":"tok","type":"stream_token","config":{"channel":"rt:x"}},
			{"id":"ok","type":"return","config":{"data":{"token":"{{ tok.token }}"}}},
			{"id":"no","type":"return","config":{"data":{"error":"{{ tok.error }}"}}}],
		"edges":[{"from":"in","to":"tok"},{"from":"tok","to":"ok"},{"from":"tok","to":"no","port":"error"}]}}`)}}
	e, err := Load(fsys, "workflows", nil)
	if err != nil {
		t.Fatal(err)
	}
	r, err := e.Run("tok", nil, nil, "http://x")
	if err != nil {
		t.Fatal(err)
	}
	if d, _ := r.Data.(map[string]any); wm.Str(d["error"]) == "" {
		t.Fatalf("stream_token should take the error edge: %+v", d)
	}
}
