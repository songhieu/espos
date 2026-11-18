package engine

import (
	"encoding/json"
	"regexp"
	"strconv"
	"strings"

	"espos/internal/wm"
)

// Values in a node's config may hold {{ path.to.value }} references to the run's input and
// to earlier nodes' outputs. A string that is one reference keeps the value's own type;
// references inside longer text are written in as text.

var ref = regexp.MustCompile(`\{\{\s*([^{}]+?)\s*\}\}`)

func eval(v any, vars map[string]any) any {
	switch x := v.(type) {
	case string:
		if m := ref.FindStringSubmatch(x); m != nil && strings.TrimSpace(x) == m[0] {
			return lookup(m[1], vars)
		}
		if !strings.Contains(x, "{{") {
			return x
		}
		return ref.ReplaceAllStringFunc(x, func(s string) string {
			m := ref.FindStringSubmatch(s)
			val := lookup(m[1], vars)
			switch val.(type) {
			case map[string]any, []any:
				b, _ := json.Marshal(val)
				return string(b)
			}
			return wm.Str(val)
		})
	case map[string]any:
		out := make(map[string]any, len(x))
		for k, vv := range x {
			out[k] = eval(vv, vars)
		}
		return out
	case []any:
		out := make([]any, len(x))
		for i, vv := range x {
			out[i] = eval(vv, vars)
		}
		return out
	}
	return v
}

func lookup(p string, vars map[string]any) any {
	var cur any = vars
	for _, seg := range strings.Split(strings.TrimSpace(p), ".") {
		switch c := cur.(type) {
		case map[string]any:
			cur = c[seg]
		case map[string]string:
			cur = c[seg]
		case []any:
			i, err := strconv.Atoi(seg)
			if err != nil || i < 0 || i >= len(c) {
				return nil
			}
			cur = c[i]
		default:
			return nil
		}
	}
	return cur
}
