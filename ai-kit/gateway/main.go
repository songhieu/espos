// tts-gateway: one HTTP API (with API keys) in front of local models: TTS (VieNeu-TTS Turbo/Nano, Piper),
// an LLM (Gemma via bin/llm-server, OpenAI /v1/chat/completions) and speech-to-text (whisper via bin/stt-server,
// OpenAI /v1/audio/transcriptions), plus an optional Cloudflare Tunnel so the API is reachable from outside a
// home network with a dynamic IP.
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"runtime"
	"sync"
	"syscall"
	"time"
)

type Gateway struct {
	cfg         *Config
	models      []*Model
	modelByID   map[string]*Model
	workerToken string
}

func main() {
	cfgPath := flag.String("config", "config.yaml", "path to config.yaml")
	listen := flag.String("listen", "", "override listen (install.sh self-test)")
	noTunnel := flag.Bool("no-tunnel", false, "never start the Cloudflare tunnel (install.sh self-test)")
	setModelsFrom := flag.String("set-models", "", "replace the models: list of -config with the one in this YAML file, then exit")
	check := flag.Bool("check", false, "check the config, then exit")
	flag.Parse()
	log.SetFlags(log.Ldate | log.Ltime)

	if *setModelsFrom != "" {
		if err := setModels(*cfgPath, *setModelsFrom); err != nil {
			log.Fatalf("set-models: %v", err)
		}
		return
	}
	cfg, err := LoadConfig(*cfgPath)
	if err != nil {
		log.Fatalf("config: %v", err)
	}
	if *listen != "" {
		cfg.Listen = *listen
	}
	if *noTunnel {
		cfg.Tunnel.Enabled = false
	}
	if *check {
		for _, m := range cfg.Models {
			fmt.Printf("%s\t%s\tworkers=%d\n", m.ID, m.Engine, m.Workers)
		}
		return
	}
	if _, err := os.Stat(cfg.PythonBin); err != nil && cfg.UsesPython() {
		log.Fatalf("python not found at %s — run ./install.sh first", cfg.PythonBin)
	}

	tok := make([]byte, 24)
	rand.Read(tok)
	g := &Gateway{cfg: cfg, modelByID: map[string]*Model{}, workerToken: hex.EncodeToString(tok)}
	port := cfg.WorkerBasePort
	for _, mc := range cfg.Models {
		m := newModel(g, mc, port+1)
		port += 100
		g.models = append(g.models, m)
		g.modelByID[mc.ID] = m
	}

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()
	var wg sync.WaitGroup

	if cfg.KeepAwake && runtime.GOOS == "darwin" {
		// keeps the Mac from sleeping while this process runs (-s needs AC power)
		ca := exec.Command("caffeinate", "-i", "-s", "-w", fmt.Sprint(os.Getpid()))
		if err := ca.Start(); err == nil {
			log.Printf("keep_awake: caffeinate running")
			go ca.Wait()
		}
	}

	for _, m := range g.models {
		log.Printf("model %s: engine=%s workers=%d threads=%d slots=%d", m.cfg.ID, m.cfg.Engine, m.cfg.Workers, m.cfg.Threads, m.slots())
		for _, w := range m.workers {
			wg.Add(1)
			go w.supervise(ctx, &wg)
			go w.keepWarm(ctx, cfg.KeepWarm)
		}
	}

	srv := &http.Server{
		Addr:              cfg.Listen,
		Handler:           g.routes(),
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       2 * time.Minute,
	}
	go func() {
		log.Printf("API listening on http://%s (%d api key(s))", cfg.Listen, len(cfg.APIKeys))
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Printf("http server: %v", err)
			stop()
		}
	}()

	if cfg.Tunnel.Enabled {
		wg.Add(1)
		go g.runTunnel(ctx, &wg)
	}

	<-ctx.Done()
	log.Printf("shutting down ...")
	sctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	srv.Shutdown(sctx)
	wg.Wait()
	log.Printf("bye")
}
