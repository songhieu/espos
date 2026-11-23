// Workflow confirm_user_pkg, node parse (imported by tools/importer).
package parse

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"image"
	"image/color"
	"image/jpeg"
	"regexp"
	"strings"
	"unicode/utf8"
)

// Reads the uploaded .espk (s3desk/docs/ecosystem.md §2) and checks it with the rules of the SDK's one validator
// (s3desk/sdk/packages/cli/src/validate.ts, espk.ts: the P, M and F codes, the same codes and the same limits) for
// the shapes a user may install privately:
//   - a JavaScript app: kind "js", engine "js" (main.js below 512 KB);
//   - a Java game: kind "native", engine "j2me" (game.jar of at most 2 MB; perms at most "sound").
//
// One part (core), type "app"; no packs, add-ons, faces or other engines (rule U002; the size limits are U001, both
// also in the validator for espos upload). Not checked here: the validator's C rules
// (main.js syntax and API use: the CLI and `espos check` run them; a broken main.js only fails on the user's own
// device), and its warnings. "part" may be missing (the web's Java packer writes none).
//
// The id the devices install is private: "u." + the manifest's id (an id already starting with "u." stays), so a
// user's package can never stand in for a store package (the device takes user_pkgs only with "u." ids, and the
// store's catalog wins). When the id changes, the package is written again with that id in its manifest (the device
// refuses a package whose manifest names another id); otherwise the bytes are kept as uploaded.
// What the devices get (device_sync's user_pkgs) comes from this manifest and the sha256 computed here.
const (
	maxEspk  = 4194304
	maxJ2me  = 2306867
	maxJar   = 2097152
	maxMain  = 512 * 1024
	maxIcon  = 200000 // the device ignores bigger icons (apps.cpp)
	maxIconU = 65536  // the icon kept next to the package for the store's listing (icon_url)
	maxPkgs  = 30
	maxCode  = 1999999999
)

var (
	idRe      = regexp.MustCompile(`^[a-z0-9][a-z0-9._-]{0,47}$`)
	versionRe = regexp.MustCompile(`^[0-9a-z.+-]{1,20}$`)
	hex6Re    = regexp.MustCompile(`^#[0-9a-fA-F]{6}$`)
	classRe   = regexp.MustCompile(`^[A-Za-z0-9_$.]{1,120}$`)
	screenRe  = regexp.MustCompile(`^[1-9][0-9]{1,3}x[1-9][0-9]{1,3}$`)
	partRe    = regexp.MustCompile(`^[a-z0-9_-]{1,24}$`)
)

var categories = []string{"game", "tool", "study", "health", "widget"}
var engines = []string{"js", "pet", "run", "j2me", "lang", "font", "wake", "tank"}
var permsKnown = []string{"sound", "notify", "net", "bg"}

// assistPerm: what an assistant app ("provides": ["assistant"]) may declare besides those: what the server sends its
// provider and does for it (s3desk/docs/assistant-providers.md §2.10; the validator's ASSIST_PERM and data perms).
var assistPerm = regexp.MustCompile(`^(assist\.(audio|name|alarms|todo|notes|notes\.body|memory|history|desk)|(alarms|todo|notes|settings|memory)\.write|desk\.control|location:city|data:(weather|calendar|markets|news|reading))$`)
var glyphs = []string{"timer", "alarm", "bell", "list", "drop", "flag", "globe", "sun", "moon", "photo", "edit", "font", "home",
	"search", "speaker", "play", "lap", "pin", "leaf", "power", "grid"}
var langTags = []string{"en", "vi", "zh-Hans", "es", "fr", "de", "ja", "ko", "pt-BR", "ru", "id"}
var keypads = []string{"full", "compact", "touch"}

// eapLike: a list of strings (non-strings left out).
func eapLike(v any) []string {
	out := []string{}
	l, _ := v.([]any)
	for _, x := range l {
		if s, ok := x.(string); ok {
			out = append(out, s)
		}
	}
	return out
}

func in(list []string, s string) bool {
	for _, x := range list {
		if x == s {
			return true
		}
	}
	return false
}

var crcTable = func() [256]uint32 {
	var t [256]uint32
	for n := 0; n < 256; n++ {
		c := uint32(n)
		for k := 0; k < 8; k++ {
			if c&1 == 1 {
				c = 0xedb88320 ^ (c >> 1)
			} else {
				c >>= 1
			}
		}
		t[n] = c
	}
	return t
}()

func crc32(b []byte) uint32 {
	c := uint32(0xffffffff)
	for _, x := range b {
		c = crcTable[byte(c)^x] ^ (c >> 8)
	}
	return c ^ 0xffffffff
}

func pad16(n int) int { return (n + 15) &^ 15 }

type prob struct {
	Code string `json:"code"`
	Msg  string `json:"msg"`
}

type file struct {
	name string
	off  int
	data []byte
}

// nameProblem: espk.ts nameProblem (F001 / P015).
func nameProblem(n string) string {
	switch {
	case n == "":
		return "empty name"
	case len(n) > 51:
		return fmt.Sprintf("%d bytes; names (with folders) are at most 51 bytes", len(n))
	case n == "manifest.json":
		return "manifest.json cannot be a file of the package"
	case strings.HasPrefix(n, "/"):
		return "starts with /"
	case strings.HasSuffix(n, "/"):
		return "ends with /"
	case strings.Contains(n, ".."):
		return `contains ".."`
	case strings.Contains(n, "//"):
		return "has an empty folder name (//)"
	}
	for _, r := range n {
		if r < 0x20 || strings.ContainsRune(`\:*?"<>|`, r) {
			return `contains one of \ : * ? " < > | or a control character`
		}
	}
	return ""
}

// textOK: a string of lo..hi characters (code points), valid UTF-8.
func textOK(v any, lo, hi int) (string, bool) {
	s, ok := v.(string)
	if !ok || !utf8.ValidString(s) {
		return "", false
	}
	n := utf8.RuneCountInString(s)
	if n > hi || (lo > 0 && strings.TrimSpace(s) == "") || n < lo {
		return "", false
	}
	return s, true
}

// whole: a JSON number written as a whole number (json.Number keeps "1.0" and "1e4" apart from "10000").
func whole(v any, lo, hi int64) (int64, bool) {
	n, ok := v.(json.Number)
	if !ok || strings.ContainsAny(string(n), ".eE") {
		return 0, false
	}
	x, err := n.Int64()
	if err != nil || x < lo || x > hi {
		return 0, false
	}
	return x, true
}

// hasEllipsis: a U+2026 in any key or text of the manifest (M026).
func hasEllipsis(v any) bool {
	switch t := v.(type) {
	case string:
		return strings.ContainsRune(t, '\u2026')
	case map[string]any:
		for k, x := range t {
			if strings.ContainsRune(k, '\u2026') || hasEllipsis(x) {
				return true
			}
		}
	case []any:
		for _, x := range t {
			if hasEllipsis(x) {
				return true
			}
		}
	}
	return false
}

// jpegFrame: image.ts jpegInfo: the first frame header's size, and whether it is baseline (SOF0).
func jpegFrame(b []byte) (w, h int, baseline bool, err string) {
	if len(b) < 4 || b[0] != 0xff || b[1] != 0xd8 {
		return 0, 0, false, "not a JPEG"
	}
	i := 2
	for i+4 <= len(b) {
		if b[i] != 0xff {
			return 0, 0, false, "broken JPEG markers"
		}
		m := b[i+1]
		if m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7) || m == 0xff {
			if m == 0xff {
				i++
			} else {
				i += 2
			}
			continue
		}
		l := int(b[i+2])<<8 | int(b[i+3])
		if m >= 0xc0 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc {
			if i+9 > len(b) {
				break
			}
			h = int(b[i+5])<<8 | int(b[i+6])
			w = int(b[i+7])<<8 | int(b[i+8])
			return w, h, m == 0xc0, ""
		}
		i += 2 + l
	}
	return 0, 0, false, "JPEG without a frame header"
}

// thumbOf: a 48x48 JPEG data URL of the icon for the web's list (as the web's thumbOf), or "".
func thumbOf(icon []byte) string {
	src, err := jpeg.Decode(bytes.NewReader(icon))
	if err != nil {
		return ""
	}
	r := src.Bounds()
	if r.Dx() < 1 || r.Dy() < 1 || r.Dx() > 1024 || r.Dy() > 1024 {
		return ""
	}
	const n = 48
	dst := image.NewRGBA(image.Rect(0, 0, n, n))
	for y := 0; y < n; y++ {
		y0, y1 := r.Min.Y+y*r.Dy()/n, r.Min.Y+(y+1)*r.Dy()/n
		if y1 <= y0 {
			y1 = y0 + 1
		}
		for x := 0; x < n; x++ {
			x0, x1 := r.Min.X+x*r.Dx()/n, r.Min.X+(x+1)*r.Dx()/n
			if x1 <= x0 {
				x1 = x0 + 1
			}
			var sr, sg, sb, cnt uint32
			for yy := y0; yy < y1; yy++ {
				for xx := x0; xx < x1; xx++ {
					cr, cg, cb, _ := src.At(xx, yy).RGBA()
					sr += cr >> 8
					sg += cg >> 8
					sb += cb >> 8
					cnt++
				}
			}
			dst.Set(x, y, color.RGBA{uint8(sr / cnt), uint8(sg / cnt), uint8(sb / cnt), 255})
		}
	}
	var out bytes.Buffer
	if jpeg.Encode(&out, dst, &jpeg.Options{Quality: 82}) != nil {
		return ""
	}
	url := "data:image/jpeg;base64," + base64.StdEncoding.EncodeToString(out.Bytes())
	if len(url) > 16000 {
		return ""
	}
	return url
}

// repack: the same package with another manifest (the id changed), files in the same order (espk.ts buildEspk).
func repack(man []byte, files []file) []byte {
	off := 16 + pad16(len(man)) + 64*len(files)
	offs := make([]int, len(files))
	for i, f := range files {
		off = pad16(off)
		offs[i] = off
		off += len(f.data)
	}
	out := make([]byte, off)
	copy(out, "ESPK")
	binary.LittleEndian.PutUint16(out[4:], 1)
	binary.LittleEndian.PutUint32(out[8:], uint32(len(man)))
	binary.LittleEndian.PutUint32(out[12:], uint32(len(files)))
	copy(out[16:], man)
	t := 16 + pad16(len(man))
	for i, f := range files {
		copy(out[t:t+52], f.name)
		binary.LittleEndian.PutUint32(out[t+52:], uint32(offs[i]))
		binary.LittleEndian.PutUint32(out[t+56:], uint32(len(f.data)))
		binary.LittleEndian.PutUint32(out[t+60:], crc32(f.data))
		copy(out[offs[i]:], f.data)
		t += 64
	}
	return out
}

// refuse: 422 with every problem found (the validator's codes and English words) and one sentence for the web.
func refuse(ps []prob) map[string]any {
	if len(ps) > 20 {
		ps = ps[:20]
	}
	return map[string]any{"ok": false, "status": 422, "body": map[string]any{
		"error":    "Gói bị từ chối (" + ps[0].Code + "): " + ps[0].Msg,
		"code":     "pkg_invalid",
		"params":   map[string]any{"rule": ps[0].Code, "detail": ps[0].Msg},
		"problems": ps,
	}}
}

func answer(status int, code, why string, params map[string]any) map[string]any {
	if params == nil {
		params = map[string]any{}
	}
	return map[string]any{"ok": false, "status": status, "body": map[string]any{"error": why, "code": code, "params": params}}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in0 := wmIn
	uid := wm.Int(in0["uid"])
	dir := wm.Str(in0["dir"])
	legacyID := wm.Str(in0["legacy_id"])
	ms := wm.Int(in0["ms"])
	if uid <= 0 || dir == "" || ms <= 0 {
		return wm.Fail("confirm_user_pkg: missing input")
	}
	raw, derr := base64.StdEncoding.DecodeString(wm.Str(in0["b64"]))
	if derr != nil {
		return answer(422, "pkg_unreadable", "Không đọc được file đã tải lên", nil)
	}
	n := len(raw)
	limit := maxEspk
	if legacyID != "" {
		limit = maxJ2me
	}
	if n > limit {
		return answer(422, "pkg_too_big", fmt.Sprintf("Gói lớn hơn %.1f MB", float64(limit)/1048576), map[string]any{"max": limit})
	}

	// ---------------- the structure (espk.ts readEspk: P001-P021) ----------------
	ps := []prob{}
	P := func(code, msg string, a ...any) { ps = append(ps, prob{code, fmt.Sprintf(msg, a...)}) }
	if n < 16 || string(raw[:4]) != "ESPK" {
		P("P001", "not an .espk file (no ESPK header)")
		return refuse(ps)
	}
	if f := binary.LittleEndian.Uint16(raw[4:]); f != 1 {
		P("P002", "format %d (format 1 is the only one devices read)", f)
		return refuse(ps)
	}
	if fl := binary.LittleEndian.Uint16(raw[6:]); fl != 0 {
		P("P003", "flags %d, expected 0", fl)
	}
	mlen := int(binary.LittleEndian.Uint32(raw[8:]))
	nf := int(binary.LittleEndian.Uint32(raw[12:]))
	if mlen == 0 || mlen > 32768 {
		P("P004", "manifest length %d (1 to 32768 bytes; the device refuses longer)", mlen)
		return refuse(ps)
	}
	if nf > 512 {
		P("P005", "%d files (at most 512; the device refuses more)", nf)
		return refuse(ps)
	}
	if 16+mlen > n {
		P("P006", "the manifest runs past the end of the file")
		return refuse(ps)
	}
	mtext := raw[16 : 16+mlen]
	if !utf8.Valid(mtext) {
		P("P007", "the manifest is not UTF-8")
		return refuse(ps)
	}
	dec := json.NewDecoder(bytes.NewReader(mtext))
	dec.UseNumber()
	var mv any
	if err := dec.Decode(&mv); err != nil || dec.More() {
		P("P008", "the manifest is not JSON")
		return refuse(ps)
	}
	m, isObj := mv.(map[string]any)
	if !isObj {
		P("P009", "the manifest is not a JSON object")
		return refuse(ps)
	}
	table := 16 + pad16(mlen)
	for i := 16 + mlen; i < table && i < n; i++ {
		if raw[i] != 0 {
			P("P010", "the manifest's padding is not zero")
			break
		}
	}
	end := table + 64*nf
	if end > n {
		P("P011", "the file table runs past the end of the file")
		return refuse(ps)
	}
	files := []file{}
	seen := map[string]bool{}
	last := end
	for k := 0; k < nf; k++ {
		e := raw[table+64*k : table+64*(k+1)]
		z := bytes.IndexByte(e[:52], 0)
		if z < 0 {
			P("P012", "file %d: the name has no terminating zero (names are at most 51 bytes)", k)
			continue
		}
		if bytes.Count(e[z:52], []byte{0}) != 52-z {
			P("P013", "file %d: the name's padding is not zero", k)
		}
		if !utf8.Valid(e[:z]) {
			P("P014", "file %d: the name is not UTF-8", k)
			continue
		}
		name := string(e[:z])
		off := int(binary.LittleEndian.Uint32(e[52:]))
		size := int(binary.LittleEndian.Uint32(e[56:]))
		crc := binary.LittleEndian.Uint32(e[60:])
		if bad := nameProblem(name); bad != "" {
			P("P015", "%q: %s", name, bad)
		}
		if seen[name] {
			P("P016", "%s is in the table twice", name)
		}
		seen[name] = true
		if off%16 != 0 {
			P("P017", "%s starts at %d, not 16-byte aligned", name, off)
		}
		if off < last {
			P("P018", "%s overlaps the table or the file before it", name)
		}
		if off+size > n || off+size < off {
			P("P019", "%s runs past the end of the file", name)
			continue
		}
		data := raw[off : off+size]
		if crc32(data) != crc {
			P("P021", "%s: CRC-32 does not match", name)
		}
		if off+size > last {
			last = off + size
		}
		files = append(files, file{name, off, data})
	}
	if len(ps) > 0 {
		return refuse(ps)
	}

	// ---------------- the manifest (validate.ts checkManifest: M rules) ----------------
	id, _ := m["id"].(string)
	if !idRe.MatchString(id) {
		P("M001", "id: %q is not a package id like vn.you.app (a-z 0-9 . _ -, 1-48 characters, starting with a letter or digit)", id)
	}
	if p, has := m["part"]; has && p != "core" {
		if ps, _ := p.(string); !partRe.MatchString(ps) {
			P("M027", "part: a short lowercase name (core, roads10, ...)")
		} else {
			P("U002", "part: %v; a private upload is one part, \"core\"", p)
		}
	}
	if _, has := m["parts"]; has {
		P("U002", "parts: a private upload is one part (core), with no choices")
	}
	name, okName := textOK(m["name"], 1, 40)
	if !okName {
		P("M010", "name: text of 1 to 40 characters")
	}
	version, _ := m["version"].(string)
	if !versionRe.MatchString(version) {
		P("M011", "version: %q; text like 1.0.0 (1-20 characters of 0-9 a-z . + -)", version)
	}
	code, okCode := whole(m["code"], 1, maxCode)
	if !okCode {
		P("M012", "code: %v; a whole number from 1 to %d, higher for every release", m["code"], maxCode)
	}
	kind, _ := m["kind"].(string)
	engine, _ := m["engine"].(string)
	js := kind == "js" && engine == "js"
	j2me := kind == "native" && engine == "j2me"
	switch {
	case js || j2me:
	case kind != "js" && kind != "native":
		P("M013", "kind: %q; \"js\" or \"native\"", kind)
	case !in(engines, engine) || (kind == "js") != (engine == "js"):
		P("M014", "engine: %q; one of %s; kind js goes with engine js, native with another", engine, strings.Join(engines, ", "))
	default:
		P("U002", "kind %q, engine %q: a private upload is a JavaScript app (kind js, engine js) or a Java game (kind native, engine j2me)", kind, engine)
	}
	if t, has := m["type"]; has && t != "app" {
		if ts, _ := t.(string); in([]string{"pack", "face", "widget", "theme"}, ts) {
			P("U002", "type: %v; a private upload is an app (packs, faces, widgets and themes come from the store)", t)
		} else {
			P("M024", "type: %v; \"app\" (the default), \"pack\", or a declarative \"face\", \"widget\" or \"theme\"", t)
		}
	}
	if _, has := m["addon_of"]; has {
		P("U002", "addon_of: add-ons come from the store with their app; a private upload is an app on its own")
	}
	need := int64(300)
	if j2me {
		need = 400
	}
	minFw, okFw := whole(m["min_fw"], 0, maxCode)
	if _, has := m["min_fw"]; !has {
		P("M015", "min_fw: missing; the firmware code the package needs (%d = 0.%d.0)", need, need/100)
	} else if !okFw {
		P("M015", "min_fw: %v; a whole number (300 = 0.3.0)", m["min_fw"])
	}
	if minFw < need {
		minFw = need // M016 is a warning: the devices are told the real need
	}
	category, _ := m["category"].(string)
	if !in(categories, category) {
		P("M017", "category: %q; one of %s", category, strings.Join(categories, ", "))
	}
	author, okA := "", true
	if v, has := m["author"]; has {
		author, okA = textOK(v, 0, 40)
	}
	desc, okD := "", true
	if v, has := m["desc"]; has {
		desc, okD = textOK(v, 0, 300)
	}
	if !okA || !okD {
		P("M018", "author: text of at most 40 characters; desc: at most 300")
	}
	// perms (M019, M021); a Java game asks for sound at most
	perms := []any{}
	have := map[string]bool{}
	if pv, has := m["perms"]; has {
		pl, isList := pv.([]any)
		if !isList {
			P("M019", "perms: a list of %s", strings.Join(permsKnown, ", "))
		}
		isAssistant := false
		for _, x := range eapLike(m["provides"]) {
			if x == "assistant" {
				isAssistant = true
			}
		}
		for _, x := range pl {
			s, _ := x.(string)
			if isAssistant && assistPerm.MatchString(s) && !have[s] {
				have[s] = true
				perms = append(perms, s) // an assistant app's: the server honours them, not the device
			} else if isAssistant && assistPerm.MatchString(s) {
				continue
			} else if assistPerm.MatchString(s) && !in(permsKnown, s) && !strings.HasPrefix(s, "data:") && s != "location:city" {
				P("M049", "perms: %q is an assistant app's permission; add \"provides\": [\"assistant\"] or remove it", s)
			} else if !in(permsKnown, s) {
				P("M019", "perms: unknown %v (known: %s)", x, strings.Join(permsKnown, ", "))
			} else if !have[s] {
				have[s] = true
				perms = append(perms, s)
			}
		}
	}
	if pv, has := m["permissions"]; has {
		pl, _ := pv.([]any)
		set := map[string]bool{}
		for _, x := range pl {
			s, _ := x.(string)
			set[s] = true
		}
		differ := len(set) != len(have)
		for k := range set {
			differ = differ || !have[k]
		}
		if _, hasPerms := m["perms"]; !hasPerms {
			P("M021", "use \"perms\" instead of \"permissions\" (the 0.2.3 field)")
		} else if differ {
			P("M021", "\"permissions\" and \"perms\" differ: the device grants both lists; make them the same")
		}
	}
	if j2me && (len(perms) > 1 || (len(perms) == 1 && perms[0] != "sound")) {
		P("U002", "perms: a Java game may ask only for \"sound\"")
	}
	// icon colours (M022)
	icon := map[string]any{"bg": []any{"#5b6270", "#2b2f3a"}, "glyph": "grid"}
	if j2me {
		icon["glyph"] = "play"
	}
	if iv, has := m["icon"]; has {
		ic, isMap := iv.(map[string]any)
		bg, _ := ic["bg"].([]any)
		if !isMap || len(bg) != 2 || !hex6Re.MatchString(fmt.Sprint(bg[0])) || !hex6Re.MatchString(fmt.Sprint(bg[1])) {
			P("M022", `icon: {"bg": ["#rrggbb", "#rrggbb"], "glyph": "..."} (two colours of the gradient)`)
		} else {
			icon["bg"] = []any{strings.ToLower(fmt.Sprint(bg[0])), strings.ToLower(fmt.Sprint(bg[1]))}
		}
		if g, hasG := ic["glyph"]; isMap && hasG {
			if gs, _ := g.(string); !in(glyphs, gs) {
				P("M022", "icon.glyph: %v; one of %s", g, strings.Join(glyphs, ", "))
			} else {
				icon["glyph"] = gs
			}
		}
	}
	// j2me (M029; the class name as the old Java check)
	var j2 map[string]any
	if j2me {
		jv, _ := m["j2me"].(map[string]any)
		midlet, _ := jv["midlet"].(string)
		keypad, _ := jv["keypad"].(string)
		if keypad == "" {
			keypad = "full"
		}
		profile, okp := textOK(jv["profile"], 0, 40)
		config, okc := textOK(jv["config"], 0, 40)
		if _, has := jv["profile"]; !has {
			okp = true
		}
		if _, has := jv["config"]; !has {
			okc = true
		}
		if jv == nil || !classRe.MatchString(midlet) || !in(keypads, keypad) || !okp || !okc {
			P("M029", `j2me: {"midlet": "<class>", "keypad": "full", "profile": "MIDP-2.0", "config": "CLDC-1.1"}`)
		}
		j2 = map[string]any{"midlet": midlet, "keypad": keypad, "profile": profile, "config": config}
	}
	// i18n (M035; the tags the S3 shows are kept for its listing)
	tr := map[string]any{}
	if tv, has := m["i18n"]; has {
		t, isMap := tv.(map[string]any)
		if !isMap {
			P("M035", `i18n: {"<language tag>": {"name": ..., "desc": ...}}`)
		}
		for tag, v := range t {
			e, isE := v.(map[string]any)
			if !isE {
				P("M035", `i18n.%s: {"name": ..., "desc": ...}`, tag)
				continue
			}
			keep := map[string]any{}
			if nv, hasN := e["name"]; hasN {
				if s, ok := textOK(nv, 1, 40); ok {
					keep["name"] = s
				} else {
					P("M035", "i18n.%s.name: text of 1 to 40 characters", tag)
				}
			}
			if dv, hasD := e["desc"]; hasD {
				if s, ok := textOK(dv, 0, 300); ok {
					keep["desc"] = s
				} else {
					P("M035", "i18n.%s.desc: text of at most 300 characters", tag)
				}
			}
			if in(langTags, tag) && len(keep) > 0 {
				tr[tag] = keep
			}
		}
	}
	license := ""
	if lv, has := m["license"]; has {
		license, _ = textOK(lv, 0, 300)
	}
	if hasEllipsis(m) {
		P("M026", "a U+2026 ellipsis in the manifest; the device shows text in full (shrink or wrap)")
	}

	// ---------------- the files (validate.ts checkPart: F rules) ----------------
	byName := map[string][]byte{}
	for _, f := range files {
		byName[f.name] = f.data
	}
	if len(files) == 0 {
		P("F007", "no files")
	}
	if js {
		if mainJs, has := byName["main.js"]; !has {
			P("F002", `kind "js" needs main.js`)
		} else if len(mainJs) >= maxMain {
			P("F006", "main.js is %d bytes; the device opens main.js only below %d bytes", len(mainJs), maxMain)
		}
	}
	jarSize := 0
	if j2me {
		jar, has := byName["game.jar"]
		switch {
		case !has:
			P("F003", `engine "j2me" needs game.jar (the game)`)
		case len(jar) > maxJar:
			P("U001", "game.jar is %d bytes; a private Java game is at most 2 MB", len(jar))
		case len(jar) < 4 || string(jar[:4]) != "PK\x03\x04":
			P("F003", "game.jar is not a .jar (zip) file")
		}
		jarSize = len(jar)
	}
	iconBytes, hasIcon := byName["icon.jpg"]
	if hasIcon {
		w, h, base, ierr := jpegFrame(iconBytes)
		switch {
		case ierr != "":
			P("F004", "icon.jpg: %s", ierr)
		case !base:
			P("F004", "icon.jpg: not a baseline JPEG; the device draws baseline JPEGs only")
		case w != h:
			P("F004", "icon.jpg: must be square (120x120), is %dx%d", w, h)
		}
		if len(iconBytes) > maxIcon {
			P("F004", "icon.jpg: %d bytes; the device ignores icons over %d bytes", len(iconBytes), maxIcon)
		}
	}
	if len(ps) > 0 {
		return refuse(ps)
	}

	// ---------------- the private id, the version, the account's room ----------------
	pkgID := id
	if !strings.HasPrefix(id, "u.") {
		pkgID = "u." + id
	}
	if len(pkgID) > 48 {
		return refuse([]prob{{"U002", fmt.Sprintf("id: %s is %d characters; a private upload adds \"u.\", so the id can have at most 46", id, len(id))}})
	}
	if legacyID != "" && pkgID != legacyID {
		return answer(422, "pkg_id_mismatch", "Mã game trong gói khác với file đã tải", nil)
	}
	oldID, oldCode, count := 0, 0, 0
	hv, _ := in0["have"].([]any)
	for _, x := range hv {
		r, _ := x.(map[string]any)
		count++
		if wm.Str(r["pkg_id"]) == pkgID {
			oldID = wm.Int(r["id"])
			oldCode = wm.Int(r["code"])
		}
	}
	if oldID > 0 && int(code) <= oldCode {
		return answer(409, "pkg_not_newer", fmt.Sprintf("Bản này không mới hơn bản đã tải (mã %d, đã có %d). Tăng code trong manifest.json rồi tải lại.", code, oldCode),
			map[string]any{"code": code, "have": oldCode})
	}
	if oldID == 0 && count >= maxPkgs {
		return answer(422, "pkg_limit", fmt.Sprintf("Tối đa %d ứng dụng. Xoá bớt cái cũ rồi tải lại.", maxPkgs), map[string]any{"max": maxPkgs})
	}

	// ---------------- the bytes the devices get ----------------
	out := raw
	if pkgID != id {
		mm := map[string]any{}
		for k, v := range m {
			mm[k] = v
		}
		mm["id"] = pkgID
		var buf bytes.Buffer
		enc := json.NewEncoder(&buf)
		enc.SetEscapeHTML(false)
		if err := enc.Encode(mm); err != nil {
			return wm.Fail(err)
		}
		man := bytes.TrimRight(buf.Bytes(), "\n")
		if len(man) > 32768 {
			return refuse([]prob{{"P004", "the manifest is too long once the id is made private"}})
		}
		out = repack(man, files)
		if len(out) > maxEspk {
			return answer(422, "pkg_too_big", "Gói lớn hơn 4 MB", map[string]any{"max": maxEspk})
		}
	}
	sum := sha256.Sum256(out)
	final := fmt.Sprintf("upkg/u%d/%s/%d/", uid, pkgID, ms)
	iconB64 := ""
	if hasIcon && len(iconBytes) <= maxIconU {
		iconB64 = base64.StdEncoding.EncodeToString(iconBytes)
	}
	thumb := ""
	if hasIcon {
		thumb = thumbOf(iconBytes)
	}
	if thumb == "" {
		if t := wm.Str(in0["thumb"]); strings.HasPrefix(t, "data:image/") && len(t) <= 16000 {
			thumb = t
		}
	}
	screen := wm.Str(in0["screen"])
	if !screenRe.MatchString(screen) {
		screen = ""
	}
	names := []any{}
	for _, f := range files {
		names = append(names, f.name)
	}
	meta := map[string]any{
		"kind": kind, "engine": engine, "type": "app", "min_fw": minFw, "category": category, "author": author, "desc": desc,
		"icon": icon, "perms": perms, "files": len(files), "src_id": id,
	}
	if len(tr) > 0 {
		meta["i18n"] = tr
	}
	if license != "" {
		meta["license"] = license
	}
	// Manifest v2 parts the server acts on (s3desk docs/manifest-driven.md, app-slots.md §15): kept for keep, which
	// writes the package's app_manifests row, so its settings app.<id>.<key> are checked like a store package's.
	for _, k := range []string{"settings", "provides", "auth", "server"} {
		if v, has := m[k]; has && v != nil {
			meta[k] = v
		}
	}
	if j2me {
		meta["j2me"] = j2
		meta["jar_size"] = jarSize
		meta["screen"] = screen
	}
	return map[string]any{
		"ok": true, "status": 200, "body": map[string]any{},
		"pkg_id": pkgID, "src_id": id, "name": name, "version": version, "code": code, "engine": engine,
		"size": len(out), "sha256": hex.EncodeToString(sum[:]), "renamed": pkgID != id,
		"final_key": final + "core.espk", "b64": base64.StdEncoding.EncodeToString(out),
		"has_icon": iconB64 != "", "icon_key": final + "icon.jpg", "icon_b64": iconB64,
		"thumb": thumb, "meta": meta, "min_fw": minFw, "old_id": oldID, "old_code": oldCode,
	}
}
