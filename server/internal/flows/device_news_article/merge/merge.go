// Workflow device_news_article, node merge (imported by tools/importer).
package merge

import "espos/internal/wm"

// Puts the signed address of each colour picture into its block, in order; a picture that could
// not be signed is left out.
func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	art, _ := in["article"].(map[string]any)
	if art == nil {
		return wm.Fail("no article")
	}
	signed, _ := in["signed"].([]any)
	blocks, _ := art["blocks"].([]any)
	out := []any{}
	n := 0
	for _, b := range blocks {
		m, _ := b.(map[string]any)
		if m == nil {
			continue
		}
		if _, isImg := m["key"]; !isImg {
			out = append(out, m)
			continue
		}
		url := ""
		if n < len(signed) {
			s, _ := signed[n].(map[string]any)
			url = wm.Str(s["url"])
		}
		n++
		if url == "" {
			continue
		}
		img := map[string]any{"img": url, "cap": wm.Str(m["cap"])}
		if w := wm.Int(m["w"]); w > 0 {
			img["w"] = w
			img["h"] = wm.Int(m["h"])
		}
		out = append(out, img)
	}
	art["blocks"] = out
	return map[string]any{"article": art}
}
