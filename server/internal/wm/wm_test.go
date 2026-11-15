package wm

import (
	"net/url"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func open(t *testing.T) {
	t.Helper()
	dir := t.TempDir()
	decl := []Table{{Code: "books", Columns: []Column{
		{Code: "user_id", Type: "integer", Indexed: true},
		{Code: "status", Type: "enum", Default: "uploading"},
		{Code: "on_device", Type: "boolean"},
	}}}
	if err := Open(filepath.Join(dir, "t.db"), decl); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { Close() })
	if err := OpenStorage(filepath.Join(dir, "files"), []byte("test-key-test-key-test-key-12345")); err != nil {
		t.Fatal(err)
	}
}

func TestTables(t *testing.T) {
	open(t)
	a, err := Insert("books", map[string]any{"user_id": 1, "title": "A"})
	if err != nil {
		t.Fatal(err)
	}
	if a["status"] != "uploading" {
		t.Fatalf("default not applied: %v", a)
	}
	Insert("books", map[string]any{"user_id": 1, "title": "B", "on_device": true})
	Insert("books", map[string]any{"user_id": 2, "title": "C"})

	// "1" finds the number 1: filter values take the column's type.
	rows, err := QueryOpts("books", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": "1"}}, "order_by": "title", "desc": true})
	if err != nil || len(rows) != 2 || rows[0]["title"] != "B" {
		t.Fatalf("query: %v %v", rows, err)
	}
	rows, _ = QueryOpts("books", map[string]any{"filters": []map[string]any{{"column": "on_device", "op": "eq", "value": true}}})
	if len(rows) != 1 || rows[0]["title"] != "B" {
		t.Fatalf("bool filter: %v", rows)
	}
	if _, err := Update("books", a["id"], map[string]any{"status": "ready"}); err != nil {
		t.Fatal(err)
	}
	got, _ := Get("books", a["id"])
	if got["status"] != "ready" || got["title"] != "A" {
		t.Fatalf("update merged wrong: %v", got)
	}
	if _, err := Upsert("books", []string{"user_id", "title"}, map[string]any{"user_id": 2, "title": "C", "status": "ready"}, nil); err != nil {
		t.Fatal(err)
	}
	all, _ := QueryOpts("books", nil)
	if len(all) != 3 {
		t.Fatalf("upsert inserted instead of updating: %d rows", len(all))
	}
	if err := Delete("books", a["id"]); err != nil {
		t.Fatal(err)
	}
	if g, _ := Get("books", a["id"]); g != nil {
		t.Fatal("not deleted")
	}
	if _, err := QueryOpts("books", map[string]any{"filters": []map[string]any{{"column": "x'); DROP", "op": "eq", "value": 1}}}); err == nil {
		t.Fatal("bad column name accepted")
	}
}

func TestSignedLinks(t *testing.T) {
	open(t)
	if err := StoragePut("books/u1/a.epub", []byte("hello")); err != nil {
		t.Fatal(err)
	}
	link := SignURL("http://h", "books/u1/a.epub", "PUT", time.Minute, 100)
	u, _ := url.Parse(link)
	key := strings.TrimPrefix(u.Path, "/files/")
	if max, err := CheckSigned(key, "PUT", u.Query()); err != nil || max != 100 {
		t.Fatalf("valid link refused: %v", err)
	}
	if _, err := CheckSigned(key, "GET", u.Query()); err == nil {
		t.Fatal("PUT link worked for GET")
	}
	if _, err := CheckSigned("books/u1/b.epub", "PUT", u.Query()); err == nil {
		t.Fatal("link worked for another key")
	}
	if _, err := FilePath("../../etc/passwd"); err != nil {
		t.Fatal(err)
	}
	p, _ := FilePath("../../etc/passwd")
	if !strings.HasPrefix(p, filesRoot) {
		t.Fatalf("key escaped the files folder: %s", p)
	}
	l, _ := StorageList("books/", "", 10)
	if files := l["files"].([]any); len(files) != 1 {
		t.Fatalf("list: %v", l)
	}
}
