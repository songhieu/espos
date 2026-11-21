package wm

import (
	"os"
	"regexp"
	"strings"
)

// Secrets on a self-hosted server come from the environment (the hosted service keeps them sealed on its platform).
// The dog assistant's: ESPOS_AI_KEY (its AI model), ESPOS_TTS_KEY (its speech service). Any other
// {{ secret.name }} reads ESPOS_SECRET_NAME. Missing: "".
var secretEnv = map[string]string{"hubapi_key": "ESPOS_AI_KEY", "espos_tts_key": "ESPOS_TTS_KEY"}

// Secret is the value of a hosted secret's name here.
func Secret(name string) string {
	if env, ok := secretEnv[name]; ok {
		return os.Getenv(env)
	}
	return os.Getenv("ESPOS_SECRET_" + strings.ToUpper(name))
}

var secretMark = regexp.MustCompile(`\{\{\s*secret\.([A-Za-z0-9_]+)\s*\}\}`)

// FillSecrets puts the secrets' values in place of {{ secret.name }} markers.
func FillSecrets(s string) string {
	if !strings.Contains(s, "secret.") {
		return s
	}
	return secretMark.ReplaceAllStringFunc(s, func(m string) string {
		return Secret(secretMark.FindStringSubmatch(m)[1])
	})
}

// Env is an environment variable with a default.
func Env(name, def string) string {
	if v := strings.TrimSpace(os.Getenv(name)); v != "" {
		return v
	}
	return def
}
