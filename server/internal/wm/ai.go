package wm

import (
	"encoding/json"
	"errors"
	"net/http"
	"os"
	"sort"
	"strconv"
	"strings"
	"time"
)

// The dog assistant's AI model on a self-hosted server (README: "The dog assistant"). The hosted service calls its
// own model router; tools/importer puts AIChat (and Transcribe, stt.go) in its place.
//
//	ESPOS_AI_PROVIDER     a preset that sets the rest for one provider: gemini (the default), openai, anthropic,
//	                      groq, deepseek, mistral, xai, openrouter, azure, local (the AI kit or another server on
//	                      your machine). Any variable below set by hand wins over the preset.
//	ESPOS_AI_URL          an OpenAI-style API base (POST <base>/chat/completions)
//	ESPOS_AI_KEY          its key, sent as "Authorization: Bearer"
//	ESPOS_AI_MODEL        the model id (Azure: the deployment's name)
//	ESPOS_AI_JSON         object (or 1): response_format json_object; schema: json_schema with the answer's schema,
//	                      strict (Anthropic's OpenAI layer takes only that); off (or 0): none, the JSON is read
//	                      from plain text
//	ESPOS_AI_REASONING    sent as reasoning_effort (none, minimal, low, medium, high...); off: never sent
//	ESPOS_AI_TEMPERATURE  a temperature in place of the dog's (0.7, 0.4 on a retry); off: none is sent (models that
//	                      refuse one: OpenAI's reasoning models, Claude 4.7 and later)
//	ESPOS_AI_TOKENS_PARAM max_tokens or max_completion_tokens: the name of the answer's limit
//	ESPOS_AI_MAX_TOKENS   the answer's limit in tokens (the dog asks 700; a model that thinks spends some of it
//	                      thinking, so the presets of thinking models ask more)
//	ESPOS_AI_AUDIO        1: the model hears a recording itself (input_audio); 0: it cannot, so a spoken question
//	                      always goes through speech to text first (stt.go) and never reaches the model as audio
//	ESPOS_AI_PROMPT       short: the assistant's short prompt, ordered for a local model's prefix cache (see
//	                      tools/importer/short_prompt.go.in); unset: the full prompt the hosted service uses
//	ESPOS_AI_TIMEOUT      seconds to wait for the model's answer (default 25)

// DefaultAIURL is Google's OpenAI-compatible Gemini API.
const DefaultAIURL = "https://generativelanguage.googleapis.com/v1beta/openai"

// aiPreset is what one provider needs (checked against each provider's documentation, 2026-11-21).
type aiPreset struct {
	url, model  string
	json        string // object | schema | off
	reasoning   string // reasoning_effort to send; "" = none
	temperature string // "" = the dog's; off = none
	tokens      string // max_tokens | max_completion_tokens
	maxTokens   int    // 0 = the dog's (700)
	audio       bool   // takes input_audio
	prompt      string // short = the short prompt
	timeout     int    // seconds; 0 = the default
}

var aiPresets = map[string]aiPreset{
	// Gemini 3 and later think and cannot stop: the limit covers the thinking too.
	"gemini": {url: DefaultAIURL, model: "gemini-3.5-flash-lite", json: "object", reasoning: "low", maxTokens: 2048, audio: true},
	// gpt-5.4-mini does not think by default; OpenAI's reasoning models refuse temperature and max_tokens.
	"openai": {url: "https://api.openai.com/v1", model: "gpt-5.4-mini", json: "object", temperature: "off",
		tokens: "max_completion_tokens", maxTokens: 2048},
	// json_object, input_audio and a temperature below 1 (Claude 4.7 and later) are refused; reasoning_effort ignored.
	"anthropic": {url: "https://api.anthropic.com/v1", model: "claude-haiku-4-5-20251001", json: "schema", temperature: "off", maxTokens: 2048},
	"groq": {url: "https://api.groq.com/openai/v1", model: "openai/gpt-oss-20b", json: "object", reasoning: "low",
		tokens: "max_completion_tokens", maxTokens: 2048},
	// DeepSeek thinks unless told not to: "none" turns it off.
	"deepseek": {url: "https://api.deepseek.com", model: "deepseek-flash", json: "object", reasoning: "none"},
	"mistral":  {url: "https://api.mistral.ai/v1", model: "mistral-small-latest", json: "object"},
	"xai": {url: "https://api.x.ai/v1", model: "grok-4.3", json: "object", reasoning: "none",
		tokens: "max_completion_tokens", maxTokens: 2048},
	"openrouter": {url: "https://openrouter.ai/api/v1", model: "google/gemini-3.5-flash-lite", json: "object", maxTokens: 2048, audio: true},
	// The v1 API: ESPOS_AI_URL=https://<resource>.openai.azure.com/openai/v1, ESPOS_AI_MODEL=<deployment>.
	"azure": {json: "object", temperature: "off", tokens: "max_completion_tokens", maxTokens: 2048},
	// The AI kit (or any server on this machine): no key, the short prompt, a slow first answer.
	"local": {url: "http://127.0.0.1:8080/v1", model: "gemma-4-e2b", json: "object", prompt: "short", timeout: 60},
}

// AIProvider is the preset in use: ESPOS_AI_PROVIDER, else gemini when no address is set or it is Gemini's.
func AIProvider() string {
	if p := strings.ToLower(strings.TrimSpace(Env("ESPOS_AI_PROVIDER", ""))); p != "" {
		return p
	}
	if u := strings.TrimRight(Env("ESPOS_AI_URL", ""), "/"); u == "" || strings.HasPrefix(u, DefaultAIURL) {
		return "gemini"
	}
	return ""
}

func preset() aiPreset { return aiPresets[AIProvider()] }

// aiEnv: the variable if set, else the preset's value, else def.
func aiEnv(name, fromPreset, def string) string {
	if v := strings.TrimSpace(Env(name, "")); v != "" {
		return v
	}
	if fromPreset != "" {
		return fromPreset
	}
	return def
}

func aiURL() string { return aiEnv("ESPOS_AI_URL", preset().url, "") }

// AIConfigured says whether the model has what it needs: a key, or an address of its own (a local model needs none).
func AIConfigured() bool {
	if os.Getenv("ESPOS_AI_KEY") != "" || Env("ESPOS_AI_URL", "") != "" {
		return true
	}
	return AIProvider() == "local"
}

// AIModel is the model the assistant asks for.
func AIModel() string { return aiEnv("ESPOS_AI_MODEL", preset().model, "gemini-3.5-flash-lite") }

// AIPromptShort says whether the assistant uses its short prompt (ESPOS_AI_PROMPT=short), made for a model on the
// person's own machine.
func AIPromptShort() bool {
	return strings.EqualFold(aiEnv("ESPOS_AI_PROMPT", preset().prompt, ""), "short")
}

// AIHearsAudio says whether the model takes a recording itself. Without a preset that says, an unknown API is
// assumed to (ESPOS_AI_AUDIO=0 says it does not).
func AIHearsAudio() bool {
	if v := strings.ToLower(strings.TrimSpace(Env("ESPOS_AI_AUDIO", ""))); v != "" {
		return v == "1" || v == "true" || v == "yes" || v == "on"
	}
	if p, ok := aiPresets[AIProvider()]; ok {
		return p.audio
	}
	return true
}

func off(v string) bool {
	switch strings.ToLower(v) {
	case "0", "off", "false", "no", "none":
		return true
	}
	return false
}

// aiClient waits ESPOS_AI_TIMEOUT seconds (default 25, as for every other outbound request) for the model.
func aiClient() *http.Client {
	sec, err := strconv.Atoi(strings.TrimSpace(Env("ESPOS_AI_TIMEOUT", "")))
	if err != nil || sec <= 0 {
		sec = preset().timeout
	}
	if sec <= 0 {
		return client
	}
	return &http.Client{Timeout: time.Duration(sec) * time.Second, CheckRedirect: client.CheckRedirect}
}

// AIChat sends one OpenAI-style chat request ({model, messages, …}) to the model set in the environment and answers
// like FetchWith: {status, headers, body}.
func AIChat(req map[string]any) (map[string]any, error) {
	if !AIConfigured() {
		return nil, errors.New("no AI model set: ESPOS_AI_KEY (and ESPOS_AI_URL or ESPOS_AI_PROVIDER for a service other than Gemini)")
	}
	p := preset()
	url := strings.TrimRight(aiURL(), "/")
	if url == "" {
		return nil, errors.New("no AI address: set ESPOS_AI_URL (ESPOS_AI_PROVIDER=" + AIProvider() + " has none)")
	}
	if !strings.HasSuffix(url, "/chat/completions") {
		url += "/chat/completions"
	}
	out := make(map[string]any, len(req))
	for k, v := range req {
		out[k] = v
	}
	out["model"] = AIModel()

	delete(out, "reasoning_effort")
	if effort := aiEnv("ESPOS_AI_REASONING", p.reasoning, ""); effort != "" && !strings.EqualFold(effort, "off") {
		out["reasoning_effort"] = strings.ToLower(effort)
	}

	mode := strings.ToLower(aiEnv("ESPOS_AI_JSON", p.json, "object"))
	switch {
	case off(mode):
		delete(out, "response_format")
	case mode == "schema":
		if _, asked := out["response_format"]; asked {
			out["response_format"] = map[string]any{"type": "json_schema", "json_schema": map[string]any{
				"name": "dog_answer", "strict": true, "schema": answerSchema}}
		}
	}

	switch t := strings.ToLower(aiEnv("ESPOS_AI_TEMPERATURE", p.temperature, "")); {
	case t == "":
	case off(t):
		delete(out, "temperature")
	default:
		if f, err := strconv.ParseFloat(t, 64); err == nil {
			out["temperature"] = f
		}
	}

	if n, err := strconv.Atoi(aiEnv("ESPOS_AI_MAX_TOKENS", strconv.Itoa(p.maxTokens), "0")); err == nil && n > 0 {
		if _, has := out["max_tokens"]; has {
			out["max_tokens"] = n
		}
	}
	if aiEnv("ESPOS_AI_TOKENS_PARAM", p.tokens, "max_tokens") == "max_completion_tokens" {
		if v, ok := out["max_tokens"]; ok {
			out["max_completion_tokens"] = v
			delete(out, "max_tokens")
		}
	}

	h := map[string]any{"Content-Type": "application/json"}
	if k := os.Getenv("ESPOS_AI_KEY"); k != "" {
		h["Authorization"] = "Bearer " + k
	}
	r, err := fetchWithClient(aiClient(), "POST", url, out, h)
	if err != nil {
		return r, err
	}
	if Int(r["status"]) != 200 {
		Log("AI model answered ", Int(r["status"]), ": ", clip(Str(r["body"]), 300))
		return r, nil
	}
	checkAnswer(r, mode == "schema")
	return r, nil
}

// checkAnswer says in the log why an answer is empty or cut (a thinking model that spent its limit), and in schema
// mode drops the nulls the schema made the model write, so the answer looks as in json_object mode.
func checkAnswer(r map[string]any, schema bool) {
	body, _ := r["body"].(map[string]any)
	ch, _ := body["choices"].([]any)
	if len(ch) == 0 {
		Log("AI model answered without choices: ", clip(Str(r["body"]), 200))
		return
	}
	c0, _ := ch[0].(map[string]any)
	msg, _ := c0["message"].(map[string]any)
	content := strings.TrimSpace(Str(msg["content"]))
	reason := Str(c0["finish_reason"])
	switch {
	case reason == "length" && content == "":
		Log("AI model spent its whole token limit without an answer (thinking?): raise ESPOS_AI_MAX_TOKENS or lower ESPOS_AI_REASONING")
	case reason == "length":
		Log("AI model's answer was cut at its token limit: raise ESPOS_AI_MAX_TOKENS")
	case content == "":
		Log("AI model answered with no text (finish_reason ", reason, ")")
	}
	if !schema || content == "" {
		return
	}
	var ans map[string]any
	if json.Unmarshal([]byte(content), &ans) != nil {
		return
	}
	if b, err := json.Marshal(dropNulls(ans)); err == nil {
		msg["content"] = string(b)
	}
}

func dropNulls(v any) any {
	switch x := v.(type) {
	case map[string]any:
		for k, e := range x {
			if e == nil {
				delete(x, k)
			} else {
				x[k] = dropNulls(e)
			}
		}
	case []any:
		for i := range x {
			x[i] = dropNulls(x[i])
		}
	}
	return v
}

// answerSchema is the dog's JSON answer (talk.go's rules) as a strict JSON schema: every field listed, null where
// unused (strict schemas need every property required and no others).
var answerSchema = func() map[string]any {
	str := map[string]any{"type": "string"}
	nstr := map[string]any{"type": []string{"string", "null"}}
	nint := map[string]any{"type": []string{"integer", "null"}}
	nbool := map[string]any{"type": []string{"boolean", "null"}}
	obj := func(props map[string]any) map[string]any {
		req := make([]string, 0, len(props))
		for k := range props {
			req = append(req, k)
		}
		sort.Strings(req)
		return map[string]any{"type": "object", "properties": props, "required": req, "additionalProperties": false}
	}
	arr := func(items any) map[string]any { return map[string]any{"type": "array", "items": items} }
	action := obj(map[string]any{"do": str, "h": nint, "m": nint, "days": nstr, "label": nstr, "i": nint, "on": nbool,
		"text": nstr, "done": nbool, "title": nstr, "body": nstr, "append": nstr, "key": nstr, "value": nstr, "name": nstr})
	return obj(map[string]any{
		"heard": str, "unclear": map[string]any{"type": "boolean"}, "reply": str, "mood": str, "topic": str, "keep": nstr,
		"guess": nint, "remember": arr(obj(map[string]any{"kind": str, "fact": str})),
		"update": arr(obj(map[string]any{"id": map[string]any{"type": "integer"}, "fact": str})),
		"forget": arr(str), "actions": arr(action), "ask_confirm": map[string]any{"type": "boolean"}, "confirm": nbool,
		"summary": nstr, "prev_summary": nstr,
	})
}()

func clip(s string, n int) string {
	if r := []rune(s); len(r) > n {
		return string(r[:n])
	}
	return s
}
