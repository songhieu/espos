package wm

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

// The dog assistant's AI model and speech-to-text service on a self-hosted server (README: "The dog assistant").
// The hosted service calls its own model router; tools/importer puts AIChat and Transcribe in its place.
//
//	ESPOS_AI_URL        an OpenAI-style API base (POST <base>/chat/completions); default Google Gemini's
//	ESPOS_AI_KEY        its key, sent as "Authorization: Bearer"
//	ESPOS_AI_MODEL      the model; default gemini-3.5-flash-lite (the hosted service's)
//	ESPOS_AI_REASONING  sent as reasoning_effort (minimal, low, medium, high…); off: never sent. Unset: low on
//	                    Gemini's API (as the hosted service asks), not sent elsewhere (most other models refuse it)
//	ESPOS_AI_JSON       0 to stop asking for a JSON answer (response_format), for servers that refuse it
//	ESPOS_AI_PROMPT     short: the assistant's short prompt, ordered for a local model's prefix cache (see
//	                    tools/importer/short_prompt.go.in); unset: the full prompt the hosted service uses
//	ESPOS_AI_TIMEOUT    seconds to wait for the model's answer (default 25; a local model's first answer after
//	                    a start can take longer)
//	ESPOS_STT_URL       optional: an OpenAI-style base with POST /audio/transcriptions. Set, the recording becomes
//	                    words here first and the model only reads text; unset, the model hears the recording itself
//	ESPOS_STT_KEY       its key
//	ESPOS_STT_MODEL     default whisper-1
//	ESPOS_STT_LANG      the spoken language as ISO 639-1 (vi, en…); unset: the service guesses

// DefaultAIURL is Google's OpenAI-compatible Gemini API.
const DefaultAIURL = "https://generativelanguage.googleapis.com/v1beta/openai"

// AIConfigured says whether the model has what it needs: a key, or a base URL of its own (a local model needs none).
func AIConfigured() bool {
	return os.Getenv("ESPOS_AI_KEY") != "" || Env("ESPOS_AI_URL", "") != ""
}

// AIModel is the model the assistant asks for.
func AIModel() string { return Env("ESPOS_AI_MODEL", "gemini-3.5-flash-lite") }

// AIPromptShort says whether the assistant uses its short prompt (ESPOS_AI_PROMPT=short), made for a model on the
// person's own machine.
func AIPromptShort() bool {
	return strings.EqualFold(strings.TrimSpace(Env("ESPOS_AI_PROMPT", "")), "short")
}

// aiClient waits ESPOS_AI_TIMEOUT seconds (default 25, as for every other outbound request) for the model.
func aiClient() *http.Client {
	sec, err := strconv.Atoi(strings.TrimSpace(Env("ESPOS_AI_TIMEOUT", "")))
	if err != nil || sec <= 0 {
		return client
	}
	return &http.Client{Timeout: time.Duration(sec) * time.Second, CheckRedirect: client.CheckRedirect}
}

// AIChat sends one OpenAI-style chat request ({model, messages, …}) to the model set in the environment and answers
// like FetchWith: {status, headers, body}.
func AIChat(req map[string]any) (map[string]any, error) {
	if !AIConfigured() {
		return nil, errors.New("no AI model set: ESPOS_AI_KEY (and ESPOS_AI_URL for a service other than Gemini)")
	}
	url := strings.TrimRight(Env("ESPOS_AI_URL", DefaultAIURL), "/")
	if !strings.HasSuffix(url, "/chat/completions") {
		url += "/chat/completions"
	}
	out := make(map[string]any, len(req))
	for k, v := range req {
		out[k] = v
	}
	out["model"] = AIModel()
	delete(out, "reasoning_effort")
	effort := strings.ToLower(Env("ESPOS_AI_REASONING", ""))
	if effort == "" && strings.HasPrefix(url, DefaultAIURL) {
		effort = "low"
	}
	if effort != "" && effort != "off" && effort != "0" {
		out["reasoning_effort"] = effort
	}
	if v := strings.ToLower(Env("ESPOS_AI_JSON", "1")); v == "0" || v == "off" || v == "false" || v == "no" {
		delete(out, "response_format")
	}
	h := map[string]any{"Content-Type": "application/json"}
	if k := os.Getenv("ESPOS_AI_KEY"); k != "" {
		h["Authorization"] = "Bearer " + k
	}
	r, err := fetchWithClient(aiClient(), "POST", url, out, h)
	if err == nil && Int(r["status"]) != 200 {
		Log("AI model answered ", Int(r["status"]), ": ", clip(Str(r["body"]), 300))
	}
	return r, err
}

// STTConfigured says whether a speech-to-text service is set.
func STTConfigured() bool { return Env("ESPOS_STT_URL", "") != "" }

var sttClient = &http.Client{Timeout: 30 * time.Second}

// Transcribe turns a recording (a WAV file, base64) into its words with the speech-to-text service.
func Transcribe(wavB64 string) (string, error) {
	base := Env("ESPOS_STT_URL", "")
	if base == "" {
		return "", errors.New("no speech-to-text service set (ESPOS_STT_URL)")
	}
	wav, err := base64.StdEncoding.DecodeString(wavB64)
	if err != nil {
		return "", fmt.Errorf("the recording is not base64: %w", err)
	}
	url := strings.TrimRight(base, "/")
	if !strings.HasSuffix(url, "/audio/transcriptions") {
		url += "/audio/transcriptions"
	}
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	fw, _ := mw.CreateFormFile("file", "speech.wav")
	fw.Write(wav)
	mw.WriteField("model", Env("ESPOS_STT_MODEL", "whisper-1"))
	mw.WriteField("response_format", "json")
	if lang := Env("ESPOS_STT_LANG", ""); lang != "" {
		mw.WriteField("language", lang)
	}
	mw.Close()
	req, err := http.NewRequest("POST", url, &body)
	if err != nil {
		return "", err
	}
	req.Header.Set("User-Agent", UserAgent)
	req.Header.Set("Content-Type", mw.FormDataContentType())
	if k := os.Getenv("ESPOS_STT_KEY"); k != "" {
		req.Header.Set("Authorization", "Bearer "+k)
	}
	res, err := sttClient.Do(req)
	if err != nil {
		return "", err
	}
	defer res.Body.Close()
	raw, _ := io.ReadAll(io.LimitReader(res.Body, 1<<20))
	if res.StatusCode != 200 {
		return "", fmt.Errorf("speech-to-text answered %d: %s", res.StatusCode, clip(strings.TrimSpace(string(raw)), 300))
	}
	var ans struct {
		Text string `json:"text"`
	}
	if err := json.Unmarshal(raw, &ans); err != nil {
		return "", fmt.Errorf("speech-to-text: not JSON: %s", clip(string(raw), 120))
	}
	return strings.TrimSpace(ans.Text), nil
}

func clip(s string, n int) string {
	if r := []rune(s); len(r) > n {
		return string(r[:n])
	}
	return s
}
