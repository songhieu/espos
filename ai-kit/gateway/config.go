package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"time"

	"gopkg.in/yaml.v3"
)

type APIKey struct {
	Name string `yaml:"name"`
	Key  string `yaml:"key"`
}

type ModelConfig struct {
	ID           string            `yaml:"id"`     // public name used in requests
	Engine       string            `yaml:"engine"` // vieneu-turbo | vieneu-nano | piper | llm | stt
	Workers      int               `yaml:"workers"`
	Threads      int               `yaml:"threads"`
	DefaultVoice string            `yaml:"default_voice"`
	Voices       map[string]string `yaml:"voices"` // piper only: voice id -> .onnx path

	// llm and stt: native servers (bin/llm-server, bin/stt-server), one process per worker
	Model        string        `yaml:"model"`         // the model file: .gguf (llm) or ggml .bin (stt)
	Slots        int           `yaml:"slots"`         // requests one worker serves at the same time (llm: decoded together)
	Ctx          int           `yaml:"ctx"`           // llm: tokens per request, prompt + answer
	VADModel     string        `yaml:"vad_model"`     // stt: Silero VAD model (skips silence)
	Language     string        `yaml:"language"`      // stt: the spoken language (vi, en, auto)
	QueueTimeout time.Duration `yaml:"queue_timeout"` // overrides the global queue_timeout for this model
	Args         []string      `yaml:"args"`          // extra flags for the server binary
}

// Kind is what the model does: tts (POST /v1/tts, /v1/audio/speech), llm (/v1/chat/completions) or stt
// (/v1/audio/transcriptions).
func (m ModelConfig) Kind() string {
	switch m.Engine {
	case "llm", "stt":
		return m.Engine
	}
	return "tts"
}

// native: served by a C++ binary of this kit instead of the Python worker.
func (m ModelConfig) native() bool { return m.Kind() != "tts" }

type TunnelConfig struct {
	Enabled     bool   `yaml:"enabled"`
	Mode        string `yaml:"mode"`  // token | quick
	Token       string `yaml:"token"` // from the Cloudflare dashboard (mode: token)
	PublicURL   string `yaml:"public_url"`
	Cloudflared string `yaml:"cloudflared"`
}

type Config struct {
	Listen         string        `yaml:"listen"`
	APIKeys        []APIKey      `yaml:"api_keys"`
	MaxChars       int           `yaml:"max_chars"`
	QueueTimeout   time.Duration `yaml:"queue_timeout"`
	MaxWaiting     int           `yaml:"max_waiting"`
	KeepAwake      bool          `yaml:"keep_awake"`
	KeepWarm       time.Duration `yaml:"keep_warm"`
	PythonBin      string        `yaml:"python_bin"`
	WorkerScript   string        `yaml:"worker_script"`
	WorkerBasePort int           `yaml:"worker_base_port"`
	LLMBin         string        `yaml:"llm_bin"` // the native servers (install.sh builds them)
	STTBin         string        `yaml:"stt_bin"`
	MaxUploadMB    int           `yaml:"max_upload_mb"` // largest recording POST /v1/audio/transcriptions takes
	StartupTimeout time.Duration `yaml:"startup_timeout"`
	Models         []ModelConfig `yaml:"models"`
	Tunnel         TunnelConfig  `yaml:"tunnel"`
	BaseDir        string        `yaml:"-"` // directory of the config file
}

var engines = map[string]bool{"vieneu-turbo": true, "vieneu-nano": true, "piper": true, "llm": true, "stt": true}

// LoadConfig reads the YAML file, expands ${ENV_VARS} in it and resolves
// relative paths against the directory of the config file.
func LoadConfig(path string) (*Config, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	cfg := &Config{
		Listen:         "0.0.0.0:8080",
		MaxChars:       1000,
		QueueTimeout:   10 * time.Second,
		MaxWaiting:     32,
		KeepAwake:      true,
		KeepWarm:       time.Minute,
		PythonBin:      "worker/.venv/bin/python",
		WorkerScript:   "worker/worker.py",
		WorkerBasePort: 9100,
		LLMBin:         "bin/llm-server",
		STTBin:         "bin/stt-server",
		MaxUploadMB:    25,
		StartupTimeout: 15 * time.Minute,
		Tunnel:         TunnelConfig{Mode: "token", Cloudflared: "cloudflared"},
	}
	if err := yaml.Unmarshal([]byte(os.ExpandEnv(string(raw))), cfg); err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	base, _ := filepath.Abs(filepath.Dir(path))
	cfg.BaseDir = base
	abs := func(p string) string {
		if p == "" || filepath.IsAbs(p) {
			return p
		}
		return filepath.Join(base, p)
	}
	cfg.PythonBin = abs(cfg.PythonBin)
	cfg.WorkerScript = abs(cfg.WorkerScript)
	cfg.LLMBin = abs(cfg.LLMBin)
	cfg.STTBin = abs(cfg.STTBin)
	if strings.Contains(cfg.Tunnel.Cloudflared, "/") {
		cfg.Tunnel.Cloudflared = abs(cfg.Tunnel.Cloudflared)
	}

	var keys []APIKey
	for _, k := range cfg.APIKeys {
		k.Key = strings.TrimSpace(k.Key)
		if k.Key == "" {
			continue
		}
		if len(k.Key) < 16 || strings.Contains(strings.ToUpper(k.Key), "CHANGE") {
			return nil, fmt.Errorf("api key %q is too short or still a placeholder (use: openssl rand -hex 24)", k.Name)
		}
		if k.Name == "" {
			k.Name = "key" + fmt.Sprint(len(keys)+1)
		}
		keys = append(keys, k)
	}
	if len(keys) == 0 {
		return nil, fmt.Errorf("no api_keys configured: refusing to start an open server")
	}
	cfg.APIKeys = keys

	seen := map[string]bool{}
	var models []ModelConfig
	for _, m := range cfg.Models {
		if m.Workers <= 0 {
			continue // disabled
		}
		if !engines[m.Engine] {
			return nil, fmt.Errorf("model %q: unknown engine %q (vieneu-turbo | vieneu-nano | piper | llm | stt)", m.ID, m.Engine)
		}
		if m.ID == "" {
			m.ID = m.Engine
		}
		if seen[m.ID] {
			return nil, fmt.Errorf("model id %q used twice", m.ID)
		}
		seen[m.ID] = true
		if m.Engine == "piper" {
			if len(m.Voices) == 0 {
				return nil, fmt.Errorf("model %q: piper needs at least one entry under voices", m.ID)
			}
			for id, p := range m.Voices {
				m.Voices[id] = abs(p)
				if _, err := os.Stat(m.Voices[id]); err != nil {
					return nil, fmt.Errorf("model %q voice %q: %w (run ./install.sh to download)", m.ID, id, err)
				}
			}
		}
		if m.native() {
			if err := checkNative(cfg, &m, abs); err != nil {
				return nil, err
			}
		}
		models = append(models, m)
	}
	if len(models) == 0 {
		return nil, fmt.Errorf("no model enabled (set workers >= 1)")
	}
	cfg.Models = models

	if cfg.Tunnel.Enabled {
		switch cfg.Tunnel.Mode {
		case "quick":
		case "token":
			if cfg.Tunnel.Token == "" {
				return nil, fmt.Errorf("tunnel.mode is token but tunnel.token is empty (or use mode: quick)")
			}
		default:
			return nil, fmt.Errorf("tunnel.mode must be token or quick")
		}
	}
	return cfg, nil
}

// checkNative fills the defaults of an llm or stt model and checks its files.
func checkNative(cfg *Config, m *ModelConfig, abs func(string) string) error {
	if m.Model == "" {
		return fmt.Errorf("model %q: engine %s needs model: (the model file)", m.ID, m.Engine)
	}
	m.Model = abs(m.Model)
	if _, err := os.Stat(m.Model); err != nil {
		return fmt.Errorf("model %q: %w (run ./install.sh to download it)", m.ID, err)
	}
	bin := cfg.LLMBin
	if m.Engine == "stt" {
		bin = cfg.STTBin
		if m.VADModel != "" {
			m.VADModel = abs(m.VADModel)
			if _, err := os.Stat(m.VADModel); err != nil {
				return fmt.Errorf("model %q vad_model: %w", m.ID, err)
			}
		}
		if m.Language == "" {
			m.Language = "vi"
		}
	}
	if _, err := os.Stat(bin); err != nil {
		return fmt.Errorf("model %q: %s not built (run ./install.sh)", m.ID, bin)
	}
	if m.Slots <= 0 {
		m.Slots = 1
	}
	if m.Threads <= 0 {
		m.Threads = 4
	}
	if m.Engine == "llm" && m.Ctx <= 0 {
		m.Ctx = 16384
	}
	if m.QueueTimeout <= 0 && m.Engine == "llm" {
		m.QueueTimeout = 60 * time.Second // an answer can take seconds: wait longer than for speech
	}
	return nil
}

// UsesPython: a TTS model is enabled (the Python worker env must exist).
func (c *Config) UsesPython() bool {
	for _, m := range c.Models {
		if !m.native() {
			return true
		}
	}
	return false
}
