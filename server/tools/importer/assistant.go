package main

import (
	"bytes"
	_ "embed"
	"encoding/json"
	"log"
	"regexp"
	"strings"
)

// The dog assistant on a self-hosted server. The hosted workflows reach two sealed services: an AI model through a
// router ({{ secret.hubapi_key }}, in assist_reply's talk node) and a speech service ({{ secret.espos_tts_key }}, an
// http_request "say" node in the flows that answer with the dog's voice). Here both come from the environment
// (README "The dog assistant"): the model call becomes wm.AIChat, a recording can first become words with
// wm.Transcribe, and each speech request becomes the engine's own tts node, which fails over to a text-only answer.

// selfSecrets are the hosted secrets this server fills itself; a workflow that reads any other one is left out.
var selfSecrets = map[string]bool{"hubapi_key": true, "espos_tts_key": true}

var secretUse = regexp.MustCompile(`\{\{\s*secret\.([A-Za-z0-9_]+)\s*\}\}`)

// hostedSecret is the first secret in src this server cannot fill, or "".
func hostedSecret(src []byte) string {
	for _, m := range secretUse.FindAllSubmatch(src, -1) {
		if !selfSecrets[string(m[1])] {
			return string(m[1])
		}
	}
	return ""
}

// The hosted model call in talk.go, and what takes its place.
var (
	modelConst = regexp.MustCompile(`(?m)^const model = "[^"]+"\n`)
	routerCall = regexp.MustCompile(`wm\.FetchWith\("POST", "https://router\.[a-z.]+/v1/chat/completions", req, map\[string\]any\{\s*"Authorization": "Bearer \{\{ secret\.hubapi_key \}\}", "Content-Type": "application/json"\}\)`)
	textCheck  = "\tif len([]rune(text)) > 500 {\n\t\treturn fail(422, \"text_too_long\")\n\t}\n"
)

const sttStep = `	// Self-hosted: with a speech-to-text service set (ESPOS_STT_URL), the recording becomes words here and the model
	// reads them as if typed, so a model that cannot hear works too (README "The dog assistant").
	if audio != "" && wm.STTConfigured() {
		if heardText, err := wm.Transcribe(audio); err != nil {
			wm.Log("speech to text: ", err.Error(), " (the model gets the recording)")
		} else {
			text, audio = heardText, ""
			if text == "" {
				text = "..." // nothing heard: the dog asks again, no model call
			}
		}
	}
`

// portAssistant rewrites the assistant's talk node: the model and its address from the environment.
func portAssistant(code, stem, s string) string {
	if !strings.Contains(s, "secret.hubapi_key") {
		return s
	}
	n := 0
	s = modelConst.ReplaceAllStringFunc(s, func(string) string {
		n++
		return "var model = wm.AIModel() // ESPOS_AI_MODEL (internal/wm/ai.go)\n"
	})
	s = routerCall.ReplaceAllStringFunc(s, func(string) string {
		n++
		return "wm.AIChat(req)"
	})
	if strings.Contains(s, textCheck) {
		s = strings.Replace(s, textCheck, textCheck+sttStep, 1)
		n++
	}
	if n != 3 || strings.Contains(s, "secret.hubapi_key\"") || routerHost.MatchString(stripComments(s)) {
		log.Fatalf("%s/%s: the hosted model call changed; update portAssistant in tools/importer/assistant.go", code, stem)
	}
	// The comments name the hosted router and its key; here both are whatever ESPOS_AI_* say.
	s = strings.Replace(s, "// The model's key is a {{ secret.hubapi_key }} marker filled on the platform: it never enters this code.",
		"// The model's address and key come from ESPOS_AI_URL and ESPOS_AI_KEY (internal/wm/ai.go): never this code.", 1)
	s = routerHost.ReplaceAllString(s, "the AI model set in ESPOS_AI_*")
	return portShortPrompt(code, stem, s)
}

// The short prompt for a model on the person's own machine (ESPOS_AI_PROMPT=short, README "The dog assistant";
// 2026-11-21, measured with Gemma 4 E2B in the TTS server's benchmark notes). The talk node keeps
// building its full prompt; with the variable set, shortPrompt (short_prompt.go.in, appended to talk.go) swaps the
// long persona, rules and actions for short ones and moves the time and the per-turn rules to the end, so a local
// model's prefix cache covers almost all of the next turn's prompt. Without the variable nothing changes.
//
//go:embed short_prompt.go.in
var shortPromptSrc string

var (
	nowCall   = "\tdata.WriteString(nowText(now, tzMin, wm.Str(devs[0][\"name\"]), city))\n"
	whereCall = "\tdata.WriteString(whereText(wm.Str(devs[0][\"name\"]), board, boardOf(devs[0]), city, citySet, tzMin))\n"
	buildCall = "\tsystem := build(memLines, maxEarlier)\n"
)

const shortSwap = `	if wm.AIPromptShort() { // ESPOS_AI_PROMPT=short: a local model's prompt (short_prompt.go.in)
		perTurn := botRule(board, long) + voiceRule(board, long, audio != "")
		lists := alarmText(st[alarmKey(board)], loc, now) + "\n" + todoText(st["s3.todo"]) + "\n" + notesText(st["s3.notes"]) + "\n"
		system = shortPrompt(system, [][2]string{
			{nowLine, ""}, {whereLine, ""}, {lists, ""},
			{personaText(dog, dogKey), shortPersona(dog, dogKey) + whereLine},
			{rulesText(name, lang, more) + perTurn, shortRules(name, lang)},
			{actionsText(board, st), shortActions(board, st)},
		}, "\n"+lists, nowLine, shortSettings(board, st), perTurn, shortNews(text, news), shortJSON(more))
	}
`

func portShortPrompt(code, stem, s string) string {
	if strings.Count(s, nowCall) != 1 || strings.Count(s, whereCall) != 1 || strings.Count(s, buildCall) != 1 {
		log.Fatalf("%s/%s: the prompt's parts changed; update portShortPrompt in tools/importer/assistant.go", code, stem)
	}
	s = strings.Replace(s, nowCall, "\tnowLine := nowText(now, tzMin, wm.Str(devs[0][\"name\"]), city)\n\tdata.WriteString(nowLine)\n", 1)
	s = strings.Replace(s, whereCall, "\twhereLine := whereText(wm.Str(devs[0][\"name\"]), board, boardOf(devs[0]), city, citySet, tzMin)\n\tdata.WriteString(whereLine)\n", 1)
	s = strings.Replace(s, buildCall, buildCall+shortSwap, 1)
	return s + "\n" + shortPromptSrc
}

var routerHost = regexp.MustCompile(`(?:https://)?router\.hubapi\.cc(?:/v1/chat/completions)?`)

// stripComments drops // comments, to check what the code itself still calls.
func stripComments(s string) string {
	var b strings.Builder
	for _, line := range strings.Split(s, "\n") {
		if i := strings.Index(line, "//"); i >= 0 && !strings.Contains(line[:i], `"`) {
			line = line[:i]
		}
		b.WriteString(line)
		b.WriteByte('\n')
	}
	return b.String()
}

type wfNode struct {
	ID     string         `json:"id"`
	Type   string         `json:"type"`
	Config map[string]any `json:"config"`
}

// portSpeech rewrites a workflow.json whose http_request nodes call the hosted speech service: each becomes a tts
// node (engine/net.go, ESPOS_TTS_*), and its error port leads to a text-only answer: the flow's JSON reply when it
// has one (the device then shows the words, as when the hosted voice fails), else a 503 the device reads the same way.
func portSpeech(code string, raw []byte) []byte {
	if !bytes.Contains(raw, []byte("secret.espos_tts_key")) {
		return raw
	}
	var doc map[string]any
	must(json.Unmarshal(raw, &doc))
	draft, _ := doc["draft"].(map[string]any)
	nodes, _ := draft["nodes"].([]any)
	edges, _ := draft["edges"].([]any)
	hasErr := map[string]bool{}
	ids := map[string]bool{}
	for _, e := range edges {
		em, _ := e.(map[string]any)
		if em["port"] == "error" {
			hasErr[str(em["from"])] = true
		}
	}
	for _, n := range nodes {
		nm, _ := n.(map[string]any)
		ids[str(nm["id"])] = true
	}
	changed := 0
	for _, n := range nodes {
		nm, _ := n.(map[string]any)
		cfg, _ := nm["config"].(map[string]any)
		if str(nm["type"]) != "http_request" || !strings.Contains(mustJSON(cfg), "secret.espos_tts_key") {
			continue
		}
		body, _ := cfg["body"].(map[string]any)
		id := str(nm["id"])
		nm["type"] = "tts"
		nm["config"] = map[string]any{"text": body["text"], "voice": body["voice"], "rate": body["sample_rate"]}
		changed++
		if hasErr[id] {
			continue
		}
		to := ""
		if ids["outj"] && code == "device_assist" { // the turn's JSON answer: words, actions, mood
			to = "outj"
		} else {
			to = id + "_none"
			nodes = append(nodes, map[string]any{"id": to, "type": "respond", "config": map[string]any{
				"status": 503, "body": map[string]any{"error": "no_voice", "detail": "{{ " + id + ".error }}"},
				"headers": map[string]any{"Cache-Control": "no-store"}}})
		}
		edges = append(edges, map[string]any{"from": id, "to": to, "port": "error"})
	}
	if changed == 0 {
		log.Fatalf("%s: reads secret.espos_tts_key outside an http_request node; update portSpeech in tools/importer/assistant.go", code)
	}
	draft["nodes"], draft["edges"] = nodes, edges
	out, err := json.MarshalIndent(doc, "", "  ")
	must(err)
	if bytes.Contains(out, []byte("secret.")) {
		log.Fatalf("%s: a secret is left after the speech rewrite", code)
	}
	return append(out, '\n')
}

func str(v any) string { s, _ := v.(string); return s }

func mustJSON(v any) string {
	b, err := json.Marshal(v)
	must(err)
	return string(b)
}
