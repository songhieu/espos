// Package wm is the small runtime the workflow nodes are written against: a document
// table store on SQLite, file storage on disk, outbound HTTP and value helpers.
//
// The node code under internal/flows calls these functions exactly as it would on the
// hosted platform, so the same handlers run on both.
package wm

import (
	"fmt"
	"log"
	"math"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// Str turns any value into a string ("" for nil; whole numbers without a decimal point).
func Str(v any) string {
	switch x := v.(type) {
	case nil:
		return ""
	case string:
		return x
	case float64:
		if x == math.Trunc(x) && math.Abs(x) < 1e15 {
			return strconv.FormatInt(int64(x), 10)
		}
		return strconv.FormatFloat(x, 'f', -1, 64)
	case float32:
		return Str(float64(x))
	case int:
		return strconv.Itoa(x)
	case int64:
		return strconv.FormatInt(x, 10)
	case bool:
		if x {
			return "true"
		}
		return "false"
	default:
		return fmt.Sprint(x)
	}
}

// Num turns numbers, numeric strings and booleans into a float64 (0 otherwise).
func Num(v any) float64 {
	switch x := v.(type) {
	case float64:
		return x
	case float32:
		return float64(x)
	case int:
		return float64(x)
	case int64:
		return float64(x)
	case int32:
		return float64(x)
	case uint32:
		return float64(x)
	case string:
		f, err := strconv.ParseFloat(strings.TrimSpace(x), 64)
		if err != nil {
			return 0
		}
		return f
	case bool:
		if x {
			return 1
		}
	}
	return 0
}

// Int is Num truncated to an int.
func Int(v any) int { return int(Num(v)) }

// Bool is true for true, a non-zero number, and the strings "true", "1", "yes", "on".
func Bool(v any) bool {
	switch x := v.(type) {
	case bool:
		return x
	case string:
		switch strings.ToLower(strings.TrimSpace(x)) {
		case "true", "1", "yes", "on":
			return true
		}
		return false
	case nil:
		return false
	}
	return Num(v) != 0
}

// Fail ends a node on its error edge: the engine sees the marker, and {{ node.error }}
// holds the message.
func Fail(v any) map[string]any {
	msg := ""
	switch x := v.(type) {
	case error:
		msg = x.Error()
	default:
		msg = Str(x)
	}
	return map[string]any{"__fail": true, "error": msg}
}

// Failed reports whether a node's output came from Fail.
func Failed(out map[string]any) bool { return out != nil && out["__fail"] == true }

// Log writes a line to the server log.
func Log(args ...any) { log.Println(append([]any{"[node]"}, args...)...) }

// Now is the current time in RFC3339 (UTC).
func Now() string { return time.Now().UTC().Format(time.RFC3339) }

var unsafeKey = regexp.MustCompile(`[^A-Za-z0-9._-]+`)

// SafeKey makes a string usable inside a storage key: letters, digits, dot, dash, underscore.
func SafeKey(s string) string {
	s = unsafeKey.ReplaceAllString(s, "-")
	s = strings.Trim(s, "-.")
	if s == "" {
		s = "file"
	}
	return s
}
