// Workflow desktop_screen, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"espos/internal/wm"
	"regexp"
	"sort"
	"strings"
	"time"
	"unicode/utf8"
)

// POST /v1/desktop/screen (the Mac, its device token; desktop-protocol §9.5): {s, epoch, base, frame}. Taken only
// while a device watches the session (else 409 not_watched: the Mac stops). The server keeps the last 600 lines of
// the session with its own seq per line (the S3's deltas, §9.2); the row goes 10 min after the last watch. A frame
// from a new epoch, or a full one, replaces the copy; else `base` must be the Mac seq taken last (409 rebase).
// Known secret shapes are masked again here. Never logged.

const (
	keep      = 600
	maxBytes  = 32 * 1024
	maxPerSec = 4
	lineMax   = 1000
)

var epochRe = regexp.MustCompile(`^[0-9a-f]{8,32}$`)
var idRe = regexp.MustCompile(`^[A-Za-z0-9_.:\-]{1,100}$`)

// The Mac's list (desktop internal/term Redact) again.
var secrets = []*regexp.Regexp{
	regexp.MustCompile(`sk-ant-[A-Za-z0-9_\-]{8,}`),
	regexp.MustCompile(`\bsk-[A-Za-z0-9_\-]{16,}`),
	regexp.MustCompile(`\bgh[pousr]_[A-Za-z0-9]{20,}`),
	regexp.MustCompile(`\bgithub_pat_[A-Za-z0-9_]{20,}`),
	regexp.MustCompile(`\b(AKIA|ASIA)[A-Z0-9]{16}\b`),
	regexp.MustCompile(`\bxox[abprs]-[A-Za-z0-9\-]{10,}`),
	regexp.MustCompile(`\bAIza[0-9A-Za-z_\-]{30,}`),
	regexp.MustCompile(`(?i)\bbearer\s+[A-Za-z0-9._\-+/=]{16,}`),
	regexp.MustCompile(`-----BEGIN [A-Z ]*PRIVATE KEY-----.*`),
	regexp.MustCompile(`(?i)\b(password|passwd|secret|api[_-]?key|token)\s*[=:]\s*\S{6,}`),
}

func redact(s string) string {
	for _, re := range secrets {
		s = re.ReplaceAllStringFunc(s, func(m string) string {
			if strings.HasPrefix(m, "-----") {
				return "••••" // a key block: nothing of it stays
			}
			for _, sep := range []string{"=", ":", " "} {
				if i := strings.Index(m, sep); i > 0 && i < 20 && !strings.HasPrefix(m, "sk") {
					return m[:i+1] + "••••"
				}
			}
			return "••••"
		})
	}
	return s
}

func answer(status int, body map[string]any) map[string]any {
	return map[string]any{"status": status, "body": body}
}

func listOf(v any) []any {
	if l, ok := v.([]any); ok {
		return l
	}
	if s, isText := v.(string); isText && s != "" {
		var l []any
		if json.Unmarshal([]byte(s), &l) == nil {
			return l
		}
	}
	return nil
}

// runs: [start, len, fg, bg, attr] × ≤ 24 with sane numbers, else none.
func runs(v any) []any {
	l, _ := v.([]any)
	if len(l) == 0 || len(l) > 24 {
		return nil
	}
	out := []any{}
	for _, x := range l {
		r, _ := x.([]any)
		if len(r) != 5 {
			return nil
		}
		ns := make([]any, 5)
		for i, y := range r {
			f, ok := y.(float64)
			if !ok || f < 0 || f > 0x1ffffff || f != float64(int64(f)) {
				return nil
			}
			ns[i] = int64(f)
		}
		out = append(out, ns)
	}
	return out
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return answer(503, map[string]any{"error": "desktop_disabled"})
	}
	dev, _ := in["device"].(map[string]any)
	deskID, uid := wm.Int(dev["device_id"]), wm.Int(dev["user_id"])
	if deskID == 0 || uid == 0 {
		return answer(401, map[string]any{"error": "unauthorized"})
	}
	b, _ := in["body"].(map[string]any)
	s := wm.Str(b["s"])
	epoch := wm.Str(b["epoch"])
	f, _ := b["frame"].(map[string]any)
	if !idRe.MatchString(s) || !epochRe.MatchString(epoch) || f == nil {
		return answer(422, map[string]any{"error": "bad_frame"})
	}
	if raw, _ := json.Marshal(b); len(raw) > maxBytes {
		return answer(413, map[string]any{"error": "too_big", "max_bytes": maxBytes})
	}
	now := time.Now().UnixMilli()
	rows, err := wm.QueryOpts("desk_screens", map[string]any{"filters": []map[string]any{
		{"column": "desktop_id", "op": "eq", "value": deskID}, {"column": "session", "op": "eq", "value": s}}, "limit": 1})
	if err != nil {
		return wm.Fail(err)
	}
	if len(rows) == 0 || int64(wm.Num(rows[0]["watch_ms"])) < now-2000 {
		return answer(409, map[string]any{"error": "not_watched"})
	}
	row := rows[0]
	until := int64(wm.Num(row["watch_ms"]))
	// at most 4 uploads a second: up_ms is when the current one-second window began
	upN := wm.Int(row["up_n"])
	upMs := int64(wm.Num(row["up_ms"]))
	if now-upMs >= 1000 {
		upN = 0
		upMs = now
	}
	if upN >= maxPerSec {
		return answer(429, map[string]any{"error": "rate", "retry_ms": 1000 - (now - upMs)})
	}
	full := f["full"] == true
	newEpoch := epoch != wm.Str(row["epoch"])
	macSeq := int64(wm.Num(f["seq"]))
	if newEpoch && !full {
		return answer(409, map[string]any{"error": "rebase", "seq": 0})
	}
	if !newEpoch && !full && int64(wm.Num(b["base"])) != int64(wm.Num(row["mac_seq"])) {
		return answer(409, map[string]any{"error": "rebase", "seq": int64(wm.Num(row["mac_seq"]))})
	}
	seq := int64(wm.Num(row["seq"])) + 1
	byN := map[int64][]any{}
	epochSeq := int64(wm.Num(row["epoch_seq"]))
	if newEpoch {
		epochSeq = seq // an S3 asking since an older seq gets the whole new copy (§9.2)
	}
	if !newEpoch {
		for _, x := range listOf(row["lines"]) {
			if a, _ := x.([]any); len(a) >= 4 {
				byN[int64(wm.Num(a[0]))] = a
			}
		}
	}
	for _, x := range listOf(f["lines"]) {
		l, _ := x.(map[string]any)
		if l == nil {
			continue
		}
		n := int64(wm.Num(l["n"]))
		t := wm.Str(l["t"])
		if n < 0 || !utf8.ValidString(t) {
			continue
		}
		if utf8.RuneCountInString(t) > lineMax {
			t = string([]rune(t)[:lineMax])
		}
		t = redact(t)
		var rs any
		if r := runs(l["r"]); r != nil {
			rs = r
		}
		if old, had := byN[n]; had && wm.Str(old[1]) == t && !newEpoch {
			continue // unchanged: keeps its seq
		}
		byN[n] = []any{n, t, rs, seq}
	}
	ns := make([]int64, 0, len(byN))
	for n := range byN {
		ns = append(ns, n)
	}
	sort.Slice(ns, func(i, j int) bool { return ns[i] < ns[j] })
	if len(ns) > keep {
		ns = ns[len(ns)-keep:]
	}
	lines := make([]any, 0, len(ns))
	for _, n := range ns {
		lines = append(lines, byN[n])
	}
	cur := listOf(f["cur"])
	if len(cur) != 2 {
		cur = []any{}
	}
	vals := map[string]any{"epoch": epoch, "mac_seq": macSeq, "seq": seq, "epoch_seq": epochSeq, "lines": lines,
		"top": int64(wm.Num(f["top"])), "cur": cur, "cols": wm.Int(f["cols"]), "rows": wm.Int(f["rows"]),
		"up_ms": upMs, "up_n": upN + 1}
	if _, uerr := wm.Update("desk_screens", wm.Int(row["id"]), vals); uerr != nil {
		return wm.Fail(uerr)
	}
	return answer(200, map[string]any{"ok": true, "seq": macSeq, "watch_until_ms": until})
}
