// Command inkdesk is the self-hosted InkDesk server for the Xteink X4: one binary, one
// account, data in one folder.
//
//	INKDESK_PASSWORD=… inkdesk                          run the server
//	inkdesk firmware add <bundle-dir> [-notes "…"]      offer a firmware build to the X4
//
// Settings come from the environment (see README): INKDESK_ADDR, INKDESK_DATA,
// INKDESK_USER, INKDESK_PASSWORD, INKDESK_PUBLIC_URL.
package main

import (
	"context"
	"crypto/rand"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"inkdesk/internal/backend"
	"inkdesk/internal/engine"
	"inkdesk/internal/flows"
	"inkdesk/internal/jobs/newsjob"
	"inkdesk/internal/server"
	"inkdesk/internal/wm"
	"inkdesk/internal/wmjob"
)

func env(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}

func main() {
	log.SetFlags(log.LstdFlags)
	if len(os.Args) > 1 && os.Args[1] == "firmware" {
		firmwareCmd(os.Args[2:])
		return
	}
	if len(os.Args) > 1 && (os.Args[1] == "-h" || os.Args[1] == "--help" || os.Args[1] == "help") {
		fmt.Println("usage: inkdesk                                  run the server (configure with INKDESK_* variables, see README)")
		fmt.Println("       inkdesk firmware add <bundle-dir> [-notes TEXT]")
		return
	}

	addr := env("INKDESK_ADDR", ":8080")
	dataDir := env("INKDESK_DATA", "./data")
	user := env("INKDESK_USER", "admin")
	password := os.Getenv("INKDESK_PASSWORD")
	if len(password) < 8 {
		log.Fatal("set INKDESK_PASSWORD (8 characters or more): it is the only way into this server")
	}
	public := os.Getenv("INKDESK_PUBLIC_URL")

	key := open(dataDir)
	defer wm.Close()
	if public != "" {
		wm.SetPublicURL(public)
	}

	eng, err := engine.Load(backend.FS, "workflows", flows.Nodes)
	if err != nil {
		log.Fatal(err)
	}
	eng.Base = public
	if eng.Base == "" {
		port := addr
		if i := strings.LastIndex(addr, ":"); i >= 0 {
			port = addr[i:]
		}
		eng.Base = "http://localhost" + port
	}
	runner := wmjob.NewRunner(map[string]wmjob.Worker{"news": newsWorker})
	eng.Enqueue = runner.Enqueue
	eng.ScheduleGate = scheduleGate

	raw, err := fs.ReadFile(backend.FS, "routes.json")
	if err != nil {
		log.Fatal(err)
	}
	var routes []server.Route
	if err := json.Unmarshal(raw, &routes); err != nil {
		log.Fatal(err)
	}
	if err := server.CheckRoutes(routes); err != nil {
		log.Fatal(err)
	}
	h := server.New(server.Config{User: user, Password: password, PublicURL: public, FirmwareDir: filepath.Join(dataDir, "firmware"), SigningKey: key},
		eng, routes, server.App())

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go eng.RunSchedules(ctx)
	// Market prices are refreshed on a schedule; fill them once now so a fresh server has some.
	go func() {
		if _, err := eng.Run("market_tick", map[string]any{}, nil, ""); err != nil {
			log.Printf("market_tick: %v", err)
		}
	}()

	srv := &http.Server{Addr: addr, Handler: h, ReadHeaderTimeout: 15 * time.Second, IdleTimeout: 120 * time.Second}
	stopped := make(chan struct{})
	go func() {
		<-ctx.Done()
		shut, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()
		_ = srv.Shutdown(shut) // waits for requests in flight
		close(stopped)
	}()
	log.Printf("InkDesk on %s (data in %s, account %q)", addr, dataDir, user)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
	<-stopped
}

// open prepares the data folder (readable by this user only): the database, the files and
// the link-signing key, which it returns.
func open(dataDir string) []byte {
	restrictUmask()
	if err := os.MkdirAll(dataDir, 0o700); err != nil {
		log.Fatal(err)
	}
	var decls []wm.Table
	entries, err := fs.ReadDir(backend.FS, "tables")
	if err != nil {
		log.Fatal(err)
	}
	for _, e := range entries {
		raw, err := fs.ReadFile(backend.FS, path.Join("tables", e.Name()))
		if err != nil {
			log.Fatal(err)
		}
		var t wm.Table
		if err := json.Unmarshal(raw, &t); err != nil {
			log.Fatalf("%s: %v", e.Name(), err)
		}
		decls = append(decls, t)
	}
	decls = append(decls, wm.Table{Code: "sessions", Columns: []wm.Column{{Code: "token_hash", Type: "text", Unique: true}}})
	if err := wm.Open(filepath.Join(dataDir, "inkdesk.db"), decls); err != nil {
		log.Fatal(err)
	}
	key := signingKey(dataDir)
	if err := wm.OpenStorage(filepath.Join(dataDir, "files"), key); err != nil {
		log.Fatal(err)
	}
	return key
}

// signingKey is a random key kept in the data folder: it signs file links.
func signingKey(dataDir string) []byte {
	p := filepath.Join(dataDir, "secret.key")
	if b, err := os.ReadFile(p); err == nil && len(b) >= 32 {
		return b
	}
	b := make([]byte, 32)
	if _, err := rand.Read(b); err != nil {
		log.Fatal(err)
	}
	if err := os.WriteFile(p, b, 0o600); err != nil {
		log.Fatal(err)
	}
	return b
}

// newsOn: the morning paper is switched on (it is off until the reader turns it on).
func newsOn() bool {
	rows, err := wm.QueryOpts("news_prefs", map[string]any{
		"filters": []map[string]any{{"column": "enabled", "op": "eq", "value": true}}, "limit": 1,
	})
	return err == nil && len(rows) > 0
}

// scheduleGate skips the VnExpress fetches while nobody reads the morning paper: the hosted
// site keeps one shared cache warm for every reader, a single-reader server has no need to.
func scheduleGate(code string) bool {
	if code == "news_refresh" || code == "news_tick" {
		return newsOn()
	}
	return true
}

// newsWorker fills an empty story cache before making an issue, so "make one now" works on
// a fresh server.
func newsWorker() {
	var in struct {
		Mode string `json:"mode"`
	}
	_ = wmjob.Input(&in)
	if in.Mode == "one" {
		if rows, err := wm.QueryOpts("news_cache", map[string]any{"limit": 1}); err == nil && len(rows) == 0 {
			wmjob.With(map[string]any{"mode": "refresh"}, newsjob.Main)
		}
	}
	newsjob.Main()
}
