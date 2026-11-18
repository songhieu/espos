// Workflow confirm_user_pkg, node save (imported by tools/importer).
package save

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"strings"
	"time"
	"unicode/utf8"
)

// Reads the uploaded .espk (s3desk/docs/ecosystem.md §2, built in the browser like the admin's Java games) and
// keeps it only when it is one Java game for this id: the header, the manifest (id, kind native, engine j2me,
// version, code newer than the last upload), the file table (game.jar, optional icon.jpg, nothing else) with each
// CRC-32. What the devices get (device_sync's user_pkgs) comes from this manifest and the sha256 computed here,
// never from what the browser said. A new version replaces the old one and its files.
const (
	maxJar  = 2097152
	maxIcon = 65536
	minFw   = 400 // the firmware with the Java engine
)

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

// text: a string of 0..max characters without control characters or an ellipsis (the device never shows one).
func text(v any, max int, need bool) (string, bool) {
	s, ok := v.(string)
	if !ok {
		return "", !need && v == nil
	}
	if (need && strings.TrimSpace(s) == "") || utf8.RuneCountInString(s) > max || !utf8.ValidString(s) || strings.Contains(s, "\u2026") {
		return "", false
	}
	for _, r := range s {
		if r < 0x20 || r == 0x7f {
			return "", false
		}
	}
	return s, true
}

func whole(v any, lo, hi float64) (int, bool) {
	f, ok := v.(float64)
	if !ok || f < lo || f > hi || f != float64(int64(f)) {
		return 0, false
	}
	return int(f), true
}

func hexColor(v any) bool {
	s, ok := v.(string)
	if !ok || len(s) != 7 || s[0] != '#' {
		return false
	}
	return strings.Trim(strings.ToLower(s[1:]), "0123456789abcdef") == ""
}

// className: a Java class name ("com.game.Main"), or "".
func className(s string) bool {
	if len(s) > 120 {
		return false
	}
	for _, r := range s {
		if !((r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') || r == '.' || r == '_' || r == '$') {
			return false
		}
	}
	return true
}

func bad(why string) map[string]any {
	return map[string]any{"status": 422, "body": map[string]any{"error": why}}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	uid := wm.Int(in["uid"])
	pkgID := wm.Str(in["pkg_id"])
	key := wm.Str(in["key"])
	dir := wm.Str(in["dir"])
	if uid == 0 || pkgID == "" || !strings.HasPrefix(key, dir) || dir == "" {
		return wm.Fail("confirm_user_pkg: missing input")
	}
	raw, derr := base64.StdEncoding.DecodeString(wm.Str(in["b64"]))
	if derr != nil {
		return bad("Không đọc được file đã tải lên")
	}
	n := len(raw)
	if n < 16 || string(raw[:4]) != "ESPK" || binary.LittleEndian.Uint16(raw[4:]) != 1 {
		return bad("File tải lên không phải gói .espk")
	}
	mlen := int(binary.LittleEndian.Uint32(raw[8:]))
	nfiles := int(binary.LittleEndian.Uint32(raw[12:]))
	if mlen < 2 || mlen > 8192 || 16+mlen > n || nfiles < 1 || nfiles > 2 {
		return bad("Gói .espk hỏng (phần đầu)")
	}
	var man map[string]any
	if json.Unmarshal(raw[16:16+mlen], &man) != nil || man == nil {
		return bad("Gói .espk hỏng (manifest)")
	}

	// ---- the file table: game.jar and maybe icon.jpg, each inside the file with its CRC ----
	t := 16 + pad16(mlen)
	if t+64*nfiles > n {
		return bad("Gói .espk hỏng (mục lục)")
	}
	jarSize := -1
	iconIn := false
	for i := 0; i < nfiles; i++ {
		if i > 0 {
			t += 64
		}
		nb := raw[t : t+52]
		end := 52
		for j, c := range nb {
			if c == 0 {
				end = j
				break
			}
		}
		name := string(nb[:end])
		off := int(binary.LittleEndian.Uint32(raw[t+52:]))
		size := int(binary.LittleEndian.Uint32(raw[t+56:]))
		crc := binary.LittleEndian.Uint32(raw[t+60:])
		if off < 16+pad16(mlen)+64*nfiles || off%16 != 0 || size < 4 || off+size > n {
			return bad("Gói .espk hỏng (" + name + " nằm ngoài file)")
		}
		data := raw[off : off+size]
		if crc32(data) != crc {
			return bad("Gói .espk hỏng (" + name + " sai CRC)")
		}
		switch {
		case name == "game.jar" && jarSize < 0:
			if size > maxJar {
				return bad("File .jar lớn hơn 2 MB. Mỗi game tối đa 2 MB.")
			}
			if string(data[:4]) != "PK\x03\x04" {
				return bad("game.jar không phải file .jar")
			}
			jarSize = size
		case name == "icon.jpg" && !iconIn:
			if size > maxIcon || data[0] != 0xff || data[1] != 0xd8 {
				return bad("Biểu tượng trong gói không hợp lệ")
			}
			iconIn = true
		default:
			return bad("Gói game Java chỉ chứa game.jar và icon.jpg")
		}
	}
	if jarSize < 0 {
		return bad("Gói thiếu game.jar")
	}

	// ---- the manifest: the admin's Java-game shape (AdminPage JarDialog), for this id ----
	if wm.Str(man["id"]) != pkgID {
		return bad("Mã game trong gói khác với file đã tải")
	}
	if wm.Str(man["kind"]) != "native" || wm.Str(man["engine"]) != "j2me" {
		return bad("Gói này không phải game Java")
	}
	if p, has := man["part"]; has && p != "core" {
		return bad("Gói game Java chỉ có một phần (core)")
	}
	name, ok := text(man["name"], 40, true)
	if !ok {
		return bad("Tên game cần từ 1 đến 40 ký tự")
	}
	version, ok := text(man["version"], 20, true)
	if !ok {
		return bad("Phiên bản không hợp lệ")
	}
	code, ok := whole(man["code"], 1, 2e9)
	if !ok {
		return bad("Mã phiên bản không hợp lệ")
	}
	if old := wm.Int(in["old_code"]); wm.Int(in["old_id"]) > 0 && code <= old {
		return map[string]any{"status": 409, "body": map[string]any{"error": "Bản này không mới hơn bản đã tải. Tải lại trang rồi thử lại."}}
	}
	minFwV, ok := whole(man["min_fw"], minFw, 2e9)
	if !ok {
		return bad("min_fw không hợp lệ")
	}
	category := wm.Str(man["category"])
	if category != "game" {
		return bad("Thể loại phải là game")
	}
	author, ok1 := text(man["author"], 40, false)
	desc, ok2 := text(man["desc"], 200, false)
	if !ok1 || !ok2 {
		return bad("Tác giả hoặc mô tả không hợp lệ")
	}
	icon := map[string]any{"bg": []any{"#5b6270", "#2b2f3a"}, "glyph": "play"}
	if ic, isMap := man["icon"].(map[string]any); isMap {
		bg, _ := ic["bg"].([]any)
		if len(bg) == 2 && hexColor(bg[0]) && hexColor(bg[1]) {
			icon["bg"] = bg
		}
	}
	perms := []any{}
	if pv, has := man["perms"]; has {
		pl, isList := pv.([]any)
		if !isList || len(pl) > 1 || (len(pl) == 1 && pl[0] != "sound") {
			return bad("Game Java chỉ được xin quyền phát âm thanh")
		}
		perms = pl
	}
	j, _ := man["j2me"].(map[string]any)
	midlet, okm := text(j["midlet"], 120, false)
	keypad := wm.Str(j["keypad"])
	profile, okp := text(j["profile"], 40, false)
	config, okc := text(j["config"], 40, false)
	if j == nil || !okm || midlet == "" || !className(midlet) || !okp || !okc || (keypad != "full" && keypad != "compact" && keypad != "touch") {
		return bad("Phần j2me trong gói không hợp lệ")
	}

	sum := sha256.Sum256(raw)
	iconKey := wm.Str(in["icon_key"])
	meta := map[string]any{
		"author": author, "desc": desc, "icon": icon, "perms": perms, "min_fw": minFwV, "category": category,
		"j2me":   map[string]any{"midlet": midlet, "keypad": keypad, "profile": profile, "config": config},
		"screen": wm.Str(in["screen"]), "jar_size": jarSize,
	}
	row, uerr := wm.Upsert("user_pkgs", []string{"user_id", "pkg_id"}, map[string]any{
		"user_id": uid, "pkg_id": pkgID, "name": name, "version": version, "code": code, "engine": "j2me",
		"size": n, "sha256": hex.EncodeToString(sum[:]), "file_key": key, "icon_key": iconKey, "meta": meta,
		"thumb": wm.Str(in["thumb"]), "url": "", "icon_url": "", "url_exp_ms": 0,
	}, nil)
	if uerr != nil {
		return wm.Fail(uerr)
	}

	// The files of earlier uploads of this id (the version replaced, uploads never confirmed) go.
	base := dir[:strings.LastIndex(strings.TrimSuffix(dir, "/"), "/")+1] // upkg/u<uid>/<pkg id>/
	if listing, lerr := wm.StorageList(base, "", 200); lerr == nil {
		files, _ := listing["files"].([]any)
		for _, f := range files {
			fm, _ := f.(map[string]any)
			k := wm.Str(fm["key"])
			if strings.HasPrefix(k, base) && !strings.HasPrefix(k, dir) {
				if serr := wm.StorageDelete(k); serr != nil {
					wm.Log("old file stays: ", serr.Error())
				}
			}
		}
	} else {
		wm.Log("old files not listed: ", lerr.Error())
	}

	id := wm.Int(row["id"])
	if id == 0 {
		id = wm.Int(in["old_id"])
	}
	return map[string]any{"status": 200, "body": map[string]any{"pkg": map[string]any{
		"id": id, "pkg_id": pkgID, "name": name, "version": version, "code": code, "size": n,
		"jar_size": jarSize, "screen": wm.Str(in["screen"]), "profile": profile, "icon_bg": icon["bg"],
		"thumb": wm.Str(in["thumb"]), "created_at": time.Now().UTC().Format(time.RFC3339),
	}}}
}
