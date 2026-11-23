// Workflow desk_queue, node work (imported by tools/importer).
package work

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"net/url"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
	"unicode"
)

// EspOS Desktop (2026-11-21; s3desk/docs/desktop-protocol.md §2.3, §4, §6). One command for a person's Mac, asked by
// the dog (assist_apply, via "voice") or by the web (desktop_act, via "web"). It checks the command against what the
// Mac enabled in its last state (state.commands, state.allow_from, mode) and the shape of the arguments, then inserts
// the desk_cmds row: "queued" (the Mac runs it at its next sync, which the poke asks for at once) or "confirm" (a
// risky command waiting for a person's yes, with a dashboard note for the S3s). The Mac checks everything again.
// The caller pokes rt:desk:<desktop>:<chan_key> (node pub) and node hold waits up to 2 s for a queued one's result.

// The phase-1 commands with their default level (§2.3). The Mac's state.commands decides which are on and at what
// level ("safe" or "confirm"); claude.approve always needs a person's yes. scripts/test-desktop.py checks that
// assist_reply's deskHints names the same commands.
var deskCmds = map[string]string{
	"app.open": "safe", "app.switch": "safe", "app.quit": "confirm",
	"win.hide_all": "safe", "win.zoom": "safe", "win.split": "safe",
	"folder.open": "safe", "url.open": "safe", "web.search": "safe",
	"tab.new": "safe", "tab.next": "safe", "tab.prev": "safe", "page.back": "safe", "page.reload": "safe",
	"video.pause": "safe", "video.play": "safe", "video.full": "safe", "video.seek": "safe",
	"yt.play": "safe", "music.playlist": "safe",
	"media.play": "safe", "media.pause": "safe", "media.toggle": "safe", "media.next": "safe", "media.prev": "safe",
	"vol.up": "safe", "vol.down": "safe", "vol.set": "safe", "vol.mute": "safe",
	"sys.lock": "safe", "sys.display_off": "safe", "sys.bright": "safe", "sys.sleep": "confirm",
	"shortcut.run": "safe",
	"claude.deny":  "safe", "claude.approve": "confirm", "claude.reply": "confirm", "claude.new": "confirm",
	"claude.stop": "confirm", "claude.focus": "safe", "claude.key": "confirm", "claude.status": "safe",
	"claude.keys": "confirm",
}

// claude.key's keys (§9.3); esc and ctrl_c interrupt Claude: risky (the app asks once, ack_risky).
var claudeKeys = map[string]bool{"esc": true, "enter": true, "up": true, "down": true, "left": true, "right": true,
	"tab": true, "shift_tab": true, "ctrl_c": true, "backspace": true, "1": true, "2": true, "3": true, "4": true,
	"5": true, "6": true, "7": true, "8": true, "9": true, "y": true, "n": true}

const onlineMs = 180000  // a desktop that synced in the last 3 minutes is online (heartbeat every 60 s)
const safeTTL = 30000    // a safe command not delivered in 30 s expires (no late music at 3 a.m.)
const confirmTTL = 60000 // a risky command waits 60 s for a yes (then 30 s more once confirmed)
const perMin = 30        // commands a minute per desktop
const confirmsPerMin = 5 // risky ones a minute per desktop
const appPerMin = 90     // claude.reply / claude.key / claude.keys from the Sessions app a minute per desktop (§9.3)
const maxKeys = 20       // keys in one claude.keys

// typing: the Sessions app's typing commands, with their own budget (§9.3).
func typing(via, cmd string) bool {
	return via == "app" && (cmd == "claude.reply" || cmd == "claude.key" || cmd == "claude.keys")
}

// fastKey: a key from the Sessions app rides on the poke to the Mac (node pub's "cmd"), so it is typed without
// waiting for a sync; keys only, never text (§9.3, 2026-11-22). The S3 is answered at once (no hold for the result).
func fastKey(via, cmd string) bool {
	return via == "app" && (cmd == "claude.key" || cmd == "claude.keys")
}

var idRe = regexp.MustCompile(`^[A-Za-z0-9_.:\-]{1,100}$`)
var folderRe = regexp.MustCompile(`^[A-Za-z0-9_.\-]{1,64}$`)

// line: one line of text, at most n letters, or ok false (missing, not text, too long, or control characters).
func line(v any, n int, required bool) (string, bool) {
	if v == nil {
		return "", !required
	}
	s, isText := v.(string)
	if !isText {
		return "", false
	}
	s = strings.TrimSpace(s)
	for _, r := range s {
		if unicode.IsControl(r) {
			return "", false
		}
	}
	if len([]rune(s)) > n || (required && s == "") {
		return "", false
	}
	return s, true
}

// whole: an integer from a JSON number (or a numeric string), within lo..hi.
func whole(v any, lo, hi int) (int, bool) {
	var f float64
	switch x := v.(type) {
	case float64:
		f = x
	case int:
		f = float64(x)
	case int64:
		f = float64(x)
	case string:
		p, err := strconv.ParseFloat(strings.TrimSpace(x), 64)
		if err != nil {
			return 0, false
		}
		f = p
	default:
		return 0, false
	}
	if f != float64(int(f)) || int(f) < lo || int(f) > hi {
		return 0, false
	}
	return int(f), true
}

// checkArgs: the arguments of cmd, cleaned (only the keys the command takes), or "" and false when their shape is
// wrong (§2.3). Existence (app installed, folder allowed, session waiting, req id) is the Mac's to check.
// long: the Sessions app's claude.reply (§9.3).
func checkArgs(cmd string, raw any, long bool) (map[string]any, bool) {
	a, _ := raw.(map[string]any)
	if raw != nil && a == nil {
		if s, isText := raw.(string); !isText || strings.TrimSpace(s) != "" {
			return nil, false
		}
	}
	if a == nil {
		a = map[string]any{}
	}
	out := map[string]any{}
	str := func(k string, n int, required bool) bool {
		s, ok := line(a[k], n, required)
		if ok && s != "" {
			out[k] = s
		}
		return ok
	}
	ids := func(k string, re *regexp.Regexp) bool {
		s, ok := line(a[k], 100, true)
		if !ok || !re.MatchString(s) {
			return false
		}
		out[k] = s
		return true
	}
	switch cmd {
	case "app.open", "app.switch", "app.quit", "music.playlist", "shortcut.run":
		return out, str("name", 100, true)
	case "win.split":
		return out, str("a", 100, true) && str("b", 100, true)
	case "folder.open":
		return out, ids("id", folderRe)
	case "url.open":
		s, ok := line(a["url"], 500, true)
		if !ok {
			return nil, false
		}
		u, err := url.Parse(s)
		if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" {
			return nil, false
		}
		out["url"] = s
		return out, true
	case "web.search":
		e := strings.ToLower(strings.TrimSpace(wm.Str(a["engine"])))
		if e == "" {
			e = "google"
		}
		if e != "google" && e != "youtube" {
			return nil, false
		}
		out["engine"] = e
		return out, str("q", 100, true)
	case "video.seek":
		s, ok := whole(a["s"], -600, 600)
		if !ok || s == 0 {
			return nil, false
		}
		out["s"] = s
		return out, true
	case "yt.play":
		// the music intent the dog's model writes (owner 2026-11-21: "Bật nhạc Sơn Tùng" played an English collab):
		// artist / song / kind, or q for a genre or mood; next = the following candidate ("đổi bài khác"); nothing =
		// the Mac's default music (never YouTube's home)
		if k := wm.Str(a["kind"]); k != "" {
			if k != "artist" && k != "song" && k != "genre" && k != "mood" && k != "playlist" {
				return nil, false
			}
			out["kind"] = k
		}
		if n, has := a["next"]; has && n != nil {
			b, isBool := n.(bool)
			if !isBool {
				return nil, false
			}
			if b {
				out["next"] = true
			}
		}
		return out, str("q", 100, false) && str("artist", 60, false) && str("song", 80, false)
	case "vol.up", "vol.down":
		by := 10
		if a["by"] != nil {
			b, ok := whole(a["by"], 1, 30)
			if !ok {
				return nil, false
			}
			by = b
		}
		out["by"] = by
		return out, true
	case "vol.set":
		v, ok := whole(a["v"], 0, 100)
		if !ok {
			return nil, false
		}
		out["v"] = v
		return out, true
	case "vol.mute":
		on := 1
		switch x := a["on"].(type) {
		case nil:
		case bool:
			if !x {
				on = 0
			}
		default:
			v, ok := whole(x, 0, 1)
			if !ok {
				return nil, false
			}
			on = v
		}
		out["on"] = on
		return out, true
	case "sys.bright":
		by, ok := whole(a["by"], -50, 50)
		if !ok || by == 0 {
			return nil, false
		}
		out["by"] = by
		return out, true
	case "claude.deny", "claude.approve":
		return out, ids("session", idRe) && ids("req", idRe)
	case "claude.stop", "claude.focus":
		return out, ids("session", idRe)
	case "claude.status":
		if a["session"] == nil || wm.Str(a["session"]) == "" {
			return out, true // every session
		}
		return out, ids("session", idRe)
	case "claude.reply":
		if long {
			// the Sessions app (§9.3): up to 2000 characters, new lines allowed (a bracketed paste on the Mac)
			t, isText := a["text"].(string)
			t = strings.TrimRight(strings.ReplaceAll(t, "\r\n", "\n"), " \n")
			if !isText || strings.TrimSpace(t) == "" || len([]rune(t)) > 2000 {
				return nil, false
			}
			for _, r := range t {
				if (r < 0x20 && r != '\n' && r != '\t') || r == 0x7f {
					return nil, false
				}
			}
			out["text"] = t
			return out, ids("session", idRe)
		}
		return out, ids("session", idRe) && str("text", 200, true)
	case "claude.key":
		k := strings.ToLower(strings.TrimSpace(wm.Str(a["key"])))
		if !claudeKeys[k] {
			return nil, false
		}
		out["key"] = k
		return out, ids("session", idRe)
	case "claude.keys":
		ks := strings.Split(strings.ToLower(wm.Str(a["keys"])), ",")
		if len(ks) == 0 || len(ks) > maxKeys {
			return nil, false
		}
		for i, k := range ks {
			k = strings.TrimSpace(k)
			if !claudeKeys[k] {
				return nil, false
			}
			ks[i] = k
		}
		out["keys"] = strings.Join(ks, ",")
		return out, ids("session", idRe)
	case "claude.new":
		t := strings.Join(strings.Fields(wm.Str(a["task"])), " ")
		if t == "" || len([]rune(t)) > 300 {
			return nil, false
		}
		out["task"] = t
		return out, ids("folder", folderRe)
	}
	return out, true // the commands without arguments
}

// sessionName: a Claude session's name in the Mac's state, else "".
func sessionName(st map[string]any, id string) (string, map[string]any) {
	l, _ := st["sessions"].([]any)
	for _, x := range l {
		s, _ := x.(map[string]any)
		if s != nil && wm.Str(s["id"]) == id {
			req, _ := s["req"].(map[string]any)
			return wm.Str(s["name"]), req
		}
	}
	return "", nil
}

// cmdText: the command in a few Vietnamese words, for the S3's dashboard note and the web ("thoát app Safari").
func cmdText(cmd string, a map[string]any, st map[string]any) string {
	return cmdTextIn("vi", cmd, a, st)
}

// cmdLangs: the firmware's languages besides Vietnamese (s3desk/i18n/languages.json), for the confirm note's titles.
var cmdLangs = []string{"en", "de", "fr", "es", "pt-BR", "ru", "ja", "ko", "zh-Hans", "id"}

// say: a template of cmdWords in the language (Vietnamese: the template itself, the text sent before 2026-11-23, byte
// for byte), its {placeholders} filled in one pass (a value holding "{name}" stays as it is).
func say(lang, vi string, kv ...string) string {
	t := vi
	if lang != "vi" {
		if row, ok := cmdWords[vi]; ok {
			if v := row[lang]; v != "" {
				t = v
			} else if v := row["en"]; v != "" {
				t = v
			}
		}
	}
	if len(kv) == 0 {
		return t
	}
	return strings.NewReplacer(kv...).Replace(t)
}

// cmdTextIn: what a command does, in words, in a language (cmdText: Vietnamese, as the callers always had it).
func cmdTextIn(lang, cmd string, a map[string]any, st map[string]any) string {
	s := func(k string) string { return wm.Str(a[k]) }
	switch cmd {
	case "claude.status":
		if n, _ := sessionName(st, wm.Str(a["session"])); n != "" {
			return say(lang, "hỏi Claude ở {name} tình hình", "{name}", n)
		}
		return say(lang, "hỏi Claude tình hình các phiên")
	case "app.open":
		return say(lang, "mở app {name}", "{name}", s("name"))
	case "app.switch":
		return say(lang, "chuyển sang {name}", "{name}", s("name"))
	case "app.quit":
		return say(lang, "thoát app {name}", "{name}", s("name"))
	case "win.hide_all":
		return say(lang, "ẩn hết cửa sổ")
	case "win.zoom":
		return say(lang, "phóng to cửa sổ")
	case "win.split":
		return say(lang, "chia đôi màn hình {a} và {b}", "{a}", s("a"), "{b}", s("b"))
	case "folder.open":
		name := s("id")
		l, _ := st["folders"].([]any)
		for _, x := range l {
			f, _ := x.(map[string]any)
			if f != nil && wm.Str(f["id"]) == s("id") && wm.Str(f["name"]) != "" {
				name = wm.Str(f["name"])
			}
		}
		return say(lang, "mở thư mục {name}", "{name}", name)
	case "url.open":
		if u, err := url.Parse(s("url")); err == nil && u.Host != "" {
			return say(lang, "mở trang {host}", "{host}", u.Host)
		}
		return say(lang, "mở một trang web")
	case "web.search":
		if s("engine") == "youtube" {
			return say(lang, "tìm trên YouTube: {q}", "{q}", s("q"))
		}
		return say(lang, "tìm trên Google: {q}", "{q}", s("q"))
	case "tab.new":
		return say(lang, "mở thẻ mới")
	case "tab.next":
		return say(lang, "sang thẻ sau")
	case "tab.prev":
		return say(lang, "về thẻ trước")
	case "page.back":
		return say(lang, "quay lại trang trước")
	case "page.reload":
		return say(lang, "tải lại trang")
	case "video.pause":
		return say(lang, "dừng video")
	case "video.play":
		return say(lang, "phát video")
	case "video.full":
		return say(lang, "xem video toàn màn hình")
	case "video.seek":
		if n := wm.Int(a["s"]); n < 0 {
			return say(lang, "tua lại {n} giây", "{n}", fmt.Sprint(-n))
		}
		return say(lang, "tua tới {n} giây", "{n}", fmt.Sprint(wm.Int(a["s"])))
	case "yt.play":
		switch {
		case a["next"] == true:
			return say(lang, "đổi bài khác")
		case s("song") != "" && s("artist") != "":
			return say(lang, "mở bài {song} của {artist}", "{song}", s("song"), "{artist}", s("artist"))
		case s("song") != "":
			return say(lang, "mở bài {song}", "{song}", s("song"))
		case s("artist") != "":
			return say(lang, "mở nhạc {artist}", "{artist}", s("artist"))
		case s("q") != "":
			return say(lang, "mở nhạc: {q}", "{q}", s("q"))
		}
		return say(lang, "mở nhạc quen của bạn")
	case "music.playlist":
		return say(lang, "phát danh sách nhạc {name}", "{name}", s("name"))
	case "media.play":
		return say(lang, "phát nhạc")
	case "media.pause":
		return say(lang, "dừng nhạc")
	case "media.toggle":
		return say(lang, "phát hay dừng nhạc")
	case "media.next":
		return say(lang, "chuyển bài tiếp theo")
	case "media.prev":
		return say(lang, "quay lại bài trước")
	case "vol.up":
		return say(lang, "tăng âm lượng {n}", "{n}", fmt.Sprint(wm.Int(a["by"])))
	case "vol.down":
		return say(lang, "giảm âm lượng {n}", "{n}", fmt.Sprint(wm.Int(a["by"])))
	case "vol.set":
		return say(lang, "đặt âm lượng {n}%", "{n}", fmt.Sprint(wm.Int(a["v"])))
	case "vol.mute":
		if wm.Int(a["on"]) == 0 {
			return say(lang, "bật tiếng")
		}
		return say(lang, "tắt tiếng")
	case "sys.lock":
		return say(lang, "khóa màn hình")
	case "sys.display_off":
		return say(lang, "tắt màn hình")
	case "sys.bright":
		if wm.Int(a["by"]) < 0 {
			return say(lang, "giảm độ sáng màn hình")
		}
		return say(lang, "tăng độ sáng màn hình")
	case "sys.sleep":
		return say(lang, "cho máy ngủ")
	case "shortcut.run":
		return say(lang, "chạy phím tắt {name}", "{name}", s("name"))
	case "claude.deny", "claude.approve", "claude.reply", "claude.stop", "claude.focus":
		name, req := sessionName(st, s("session"))
		where := ""
		if name != "" {
			where = say(lang, " ở phiên {name}", "{name}", name)
		}
		switch cmd {
		case "claude.approve":
			if c := wm.Str(req["cmd"]); c != "" && wm.Str(req["id"]) == s("req") {
				return say(lang, "cho Claude chạy {c}{where}", "{c}", c, "{where}", where)
			}
			return say(lang, "duyệt yêu cầu của Claude{where}", "{where}", where)
		case "claude.deny":
			return say(lang, "từ chối yêu cầu của Claude{where}", "{where}", where)
		case "claude.reply":
			return say(lang, "trả lời Claude{where}: {text}", "{where}", where, "{text}", s("text"))
		case "claude.stop":
			return say(lang, "dừng Claude{where}", "{where}", where)
		}
		return say(lang, "mở phiên Claude{where}", "{where}", where)
	case "claude.new":
		return say(lang, "giao việc mới cho Claude: {task}", "{task}", s("task"))
	}
	return cmd
}

// cmdWords: cmdText's templates (the Vietnamese key, as sent before) in the firmware's other languages (2026-11-23).
// A confirm note is shown by every S3 of the account, each in its own language: the note keeps all of them
// (data.titles) and build_dashboard picks the device's.
var cmdWords = map[string]map[string]string{
	"hỏi Claude ở {name} tình hình":     {"en": "ask Claude in {name} how it's going", "de": "Claude in {name} nach dem Stand fragen", "fr": "demander à Claude dans {name} où il en est", "es": "preguntar a Claude en {name} cómo va", "pt-BR": "perguntar ao Claude em {name} como está", "ru": "спросить Claude в {name}, как дела", "ja": "{name}のClaudeに状況を聞く", "ko": "{name}의 Claude에게 진행 상황 묻기", "zh-Hans": "询问 {name} 中 Claude 的进展", "id": "tanya Claude di {name} bagaimana kemajuannya"},
	"hỏi Claude tình hình các phiên":    {"en": "ask Claude how the sessions are going", "de": "Claude nach dem Stand der Sitzungen fragen", "fr": "demander à Claude où en sont les sessions", "es": "preguntar a Claude cómo van las sesiones", "pt-BR": "perguntar ao Claude como estão as sessões", "ru": "спросить Claude, как идут сессии", "ja": "Claudeにセッションの状況を聞く", "ko": "Claude에게 세션 진행 상황 묻기", "zh-Hans": "询问 Claude 各会话的进展", "id": "tanya Claude bagaimana sesi-sesinya"},
	"mở app {name}":                     {"en": "open the app {name}", "de": "App {name} öffnen", "fr": "ouvrir l'app {name}", "es": "abrir la app {name}", "pt-BR": "abrir o app {name}", "ru": "открыть приложение {name}", "ja": "アプリ {name} を開く", "ko": "앱 {name} 열기", "zh-Hans": "打开应用 {name}", "id": "buka aplikasi {name}"},
	"chuyển sang {name}":                {"en": "switch to {name}", "de": "zu {name} wechseln", "fr": "passer à {name}", "es": "cambiar a {name}", "pt-BR": "mudar para {name}", "ru": "переключиться на {name}", "ja": "{name} に切り替える", "ko": "{name}(으)로 전환", "zh-Hans": "切换到 {name}", "id": "beralih ke {name}"},
	"thoát app {name}":                  {"en": "quit the app {name}", "de": "App {name} beenden", "fr": "quitter l'app {name}", "es": "cerrar la app {name}", "pt-BR": "fechar o app {name}", "ru": "закрыть приложение {name}", "ja": "アプリ {name} を終了", "ko": "앱 {name} 종료", "zh-Hans": "退出应用 {name}", "id": "tutup aplikasi {name}"},
	"ẩn hết cửa sổ":                     {"en": "hide all windows", "de": "alle Fenster ausblenden", "fr": "masquer toutes les fenêtres", "es": "ocultar todas las ventanas", "pt-BR": "ocultar todas as janelas", "ru": "скрыть все окна", "ja": "すべてのウィンドウを隠す", "ko": "모든 창 숨기기", "zh-Hans": "隐藏所有窗口", "id": "sembunyikan semua jendela"},
	"phóng to cửa sổ":                   {"en": "zoom the window", "de": "Fenster maximieren", "fr": "agrandir la fenêtre", "es": "ampliar la ventana", "pt-BR": "ampliar a janela", "ru": "развернуть окно", "ja": "ウィンドウを拡大", "ko": "창 확대", "zh-Hans": "放大窗口", "id": "perbesar jendela"},
	"chia đôi màn hình {a} và {b}":      {"en": "split the screen: {a} and {b}", "de": "Bildschirm teilen: {a} und {b}", "fr": "partager l'écran : {a} et {b}", "es": "dividir la pantalla: {a} y {b}", "pt-BR": "dividir a tela: {a} e {b}", "ru": "разделить экран: {a} и {b}", "ja": "画面を分割: {a} と {b}", "ko": "화면 분할: {a}, {b}", "zh-Hans": "分屏：{a} 和 {b}", "id": "bagi layar: {a} dan {b}"},
	"mở thư mục {name}":                 {"en": "open the folder {name}", "de": "Ordner {name} öffnen", "fr": "ouvrir le dossier {name}", "es": "abrir la carpeta {name}", "pt-BR": "abrir a pasta {name}", "ru": "открыть папку {name}", "ja": "フォルダ {name} を開く", "ko": "폴더 {name} 열기", "zh-Hans": "打开文件夹 {name}", "id": "buka folder {name}"},
	"mở trang {host}":                   {"en": "open {host}", "de": "{host} öffnen", "fr": "ouvrir {host}", "es": "abrir {host}", "pt-BR": "abrir {host}", "ru": "открыть {host}", "ja": "{host} を開く", "ko": "{host} 열기", "zh-Hans": "打开 {host}", "id": "buka {host}"},
	"mở một trang web":                  {"en": "open a web page", "de": "eine Webseite öffnen", "fr": "ouvrir une page web", "es": "abrir una página web", "pt-BR": "abrir uma página da web", "ru": "открыть веб-страницу", "ja": "Webページを開く", "ko": "웹 페이지 열기", "zh-Hans": "打开网页", "id": "buka halaman web"},
	"tìm trên YouTube: {q}":             {"en": "search YouTube: {q}", "de": "auf YouTube suchen: {q}", "fr": "chercher sur YouTube : {q}", "es": "buscar en YouTube: {q}", "pt-BR": "pesquisar no YouTube: {q}", "ru": "искать на YouTube: {q}", "ja": "YouTubeで検索: {q}", "ko": "YouTube에서 검색: {q}", "zh-Hans": "在 YouTube 搜索：{q}", "id": "cari di YouTube: {q}"},
	"tìm trên Google: {q}":              {"en": "search Google: {q}", "de": "auf Google suchen: {q}", "fr": "chercher sur Google : {q}", "es": "buscar en Google: {q}", "pt-BR": "pesquisar no Google: {q}", "ru": "искать в Google: {q}", "ja": "Googleで検索: {q}", "ko": "Google에서 검색: {q}", "zh-Hans": "在 Google 搜索：{q}", "id": "cari di Google: {q}"},
	"mở thẻ mới":                        {"en": "open a new tab", "de": "neuen Tab öffnen", "fr": "ouvrir un nouvel onglet", "es": "abrir una pestaña nueva", "pt-BR": "abrir uma nova aba", "ru": "открыть новую вкладку", "ja": "新しいタブを開く", "ko": "새 탭 열기", "zh-Hans": "打开新标签页", "id": "buka tab baru"},
	"sang thẻ sau":                      {"en": "next tab", "de": "nächster Tab", "fr": "onglet suivant", "es": "pestaña siguiente", "pt-BR": "próxima aba", "ru": "следующая вкладка", "ja": "次のタブ", "ko": "다음 탭", "zh-Hans": "下一个标签页", "id": "tab berikutnya"},
	"về thẻ trước":                      {"en": "previous tab", "de": "vorheriger Tab", "fr": "onglet précédent", "es": "pestaña anterior", "pt-BR": "aba anterior", "ru": "предыдущая вкладка", "ja": "前のタブ", "ko": "이전 탭", "zh-Hans": "上一个标签页", "id": "tab sebelumnya"},
	"quay lại trang trước":              {"en": "go back a page", "de": "eine Seite zurück", "fr": "revenir à la page précédente", "es": "volver a la página anterior", "pt-BR": "voltar à página anterior", "ru": "вернуться на предыдущую страницу", "ja": "前のページに戻る", "ko": "이전 페이지로", "zh-Hans": "返回上一页", "id": "kembali ke halaman sebelumnya"},
	"tải lại trang":                     {"en": "reload the page", "de": "Seite neu laden", "fr": "recharger la page", "es": "recargar la página", "pt-BR": "recarregar a página", "ru": "обновить страницу", "ja": "ページを再読み込み", "ko": "페이지 새로고침", "zh-Hans": "重新加载页面", "id": "muat ulang halaman"},
	"dừng video":                        {"en": "pause the video", "de": "Video pausieren", "fr": "mettre la vidéo en pause", "es": "pausar el video", "pt-BR": "pausar o vídeo", "ru": "поставить видео на паузу", "ja": "動画を一時停止", "ko": "동영상 일시정지", "zh-Hans": "暂停视频", "id": "jeda video"},
	"phát video":                        {"en": "play the video", "de": "Video abspielen", "fr": "lire la vidéo", "es": "reproducir el video", "pt-BR": "reproduzir o vídeo", "ru": "воспроизвести видео", "ja": "動画を再生", "ko": "동영상 재생", "zh-Hans": "播放视频", "id": "putar video"},
	"xem video toàn màn hình":           {"en": "watch the video full screen", "de": "Video im Vollbild", "fr": "vidéo en plein écran", "es": "ver el video a pantalla completa", "pt-BR": "ver o vídeo em tela cheia", "ru": "видео на весь экран", "ja": "動画を全画面表示", "ko": "동영상 전체 화면", "zh-Hans": "全屏播放视频", "id": "video layar penuh"},
	"tua lại {n} giây":                  {"en": "rewind {n} seconds", "de": "{n} Sekunden zurückspulen", "fr": "reculer de {n} secondes", "es": "retroceder {n} segundos", "pt-BR": "voltar {n} segundos", "ru": "назад на {n} с", "ja": "{n}秒戻す", "ko": "{n}초 뒤로", "zh-Hans": "后退 {n} 秒", "id": "mundur {n} detik"},
	"tua tới {n} giây":                  {"en": "skip ahead {n} seconds", "de": "{n} Sekunden vorspulen", "fr": "avancer de {n} secondes", "es": "adelantar {n} segundos", "pt-BR": "avançar {n} segundos", "ru": "вперёд на {n} с", "ja": "{n}秒進める", "ko": "{n}초 앞으로", "zh-Hans": "快进 {n} 秒", "id": "maju {n} detik"},
	"đổi bài khác":                      {"en": "play another song", "de": "anderen Song spielen", "fr": "changer de chanson", "es": "poner otra canción", "pt-BR": "tocar outra música", "ru": "включить другую песню", "ja": "別の曲にする", "ko": "다른 곡 틀기", "zh-Hans": "换一首歌", "id": "ganti lagu lain"},
	"mở bài {song} của {artist}":        {"en": "play {song} by {artist}", "de": "{song} von {artist} spielen", "fr": "lancer {song} de {artist}", "es": "poner {song} de {artist}", "pt-BR": "tocar {song} de {artist}", "ru": "включить {song} — {artist}", "ja": "{artist}の{song}を再生", "ko": "{artist}의 {song} 재생", "zh-Hans": "播放 {artist} 的 {song}", "id": "putar {song} oleh {artist}"},
	"mở bài {song}":                     {"en": "play {song}", "de": "{song} spielen", "fr": "lancer {song}", "es": "poner {song}", "pt-BR": "tocar {song}", "ru": "включить {song}", "ja": "{song}を再生", "ko": "{song} 재생", "zh-Hans": "播放 {song}", "id": "putar {song}"},
	"mở nhạc {artist}":                  {"en": "play music by {artist}", "de": "Musik von {artist} spielen", "fr": "lancer de la musique de {artist}", "es": "poner música de {artist}", "pt-BR": "tocar música de {artist}", "ru": "включить музыку {artist}", "ja": "{artist}の曲を再生", "ko": "{artist} 음악 재생", "zh-Hans": "播放 {artist} 的音乐", "id": "putar musik {artist}"},
	"mở nhạc: {q}":                      {"en": "play music: {q}", "de": "Musik spielen: {q}", "fr": "lancer de la musique : {q}", "es": "poner música: {q}", "pt-BR": "tocar música: {q}", "ru": "включить музыку: {q}", "ja": "音楽を再生: {q}", "ko": "음악 재생: {q}", "zh-Hans": "播放音乐：{q}", "id": "putar musik: {q}"},
	"mở nhạc quen của bạn":              {"en": "play your usual music", "de": "deine übliche Musik spielen", "fr": "lancer votre musique habituelle", "es": "poner tu música de siempre", "pt-BR": "tocar sua música de sempre", "ru": "включить вашу обычную музыку", "ja": "いつもの音楽を再生", "ko": "평소 듣던 음악 재생", "zh-Hans": "播放你常听的音乐", "id": "putar musik favorit Anda"},
	"phát danh sách nhạc {name}":        {"en": "play the playlist {name}", "de": "Playlist {name} abspielen", "fr": "lire la playlist {name}", "es": "reproducir la lista {name}", "pt-BR": "tocar a playlist {name}", "ru": "включить плейлист {name}", "ja": "プレイリスト {name} を再生", "ko": "재생목록 {name} 재생", "zh-Hans": "播放歌单 {name}", "id": "putar daftar putar {name}"},
	"phát nhạc":                         {"en": "play music", "de": "Musik abspielen", "fr": "lire la musique", "es": "reproducir música", "pt-BR": "tocar música", "ru": "включить музыку", "ja": "音楽を再生", "ko": "음악 재생", "zh-Hans": "播放音乐", "id": "putar musik"},
	"dừng nhạc":                         {"en": "pause the music", "de": "Musik pausieren", "fr": "mettre la musique en pause", "es": "pausar la música", "pt-BR": "pausar a música", "ru": "поставить музыку на паузу", "ja": "音楽を一時停止", "ko": "음악 일시정지", "zh-Hans": "暂停音乐", "id": "jeda musik"},
	"phát hay dừng nhạc":                {"en": "play or pause the music", "de": "Musik abspielen oder pausieren", "fr": "lire ou mettre en pause la musique", "es": "reproducir o pausar la música", "pt-BR": "tocar ou pausar a música", "ru": "включить или приостановить музыку", "ja": "音楽を再生/一時停止", "ko": "음악 재생/일시정지", "zh-Hans": "播放或暂停音乐", "id": "putar atau jeda musik"},
	"chuyển bài tiếp theo":              {"en": "next song", "de": "nächster Song", "fr": "chanson suivante", "es": "siguiente canción", "pt-BR": "próxima música", "ru": "следующая песня", "ja": "次の曲", "ko": "다음 곡", "zh-Hans": "下一首", "id": "lagu berikutnya"},
	"quay lại bài trước":                {"en": "previous song", "de": "vorheriger Song", "fr": "chanson précédente", "es": "canción anterior", "pt-BR": "música anterior", "ru": "предыдущая песня", "ja": "前の曲", "ko": "이전 곡", "zh-Hans": "上一首", "id": "lagu sebelumnya"},
	"tăng âm lượng {n}":                 {"en": "volume up {n}", "de": "Lautstärke +{n}", "fr": "volume +{n}", "es": "subir el volumen {n}", "pt-BR": "aumentar o volume {n}", "ru": "громкость +{n}", "ja": "音量を{n}上げる", "ko": "볼륨 {n} 올리기", "zh-Hans": "音量调高 {n}", "id": "naikkan volume {n}"},
	"giảm âm lượng {n}":                 {"en": "volume down {n}", "de": "Lautstärke -{n}", "fr": "volume -{n}", "es": "bajar el volumen {n}", "pt-BR": "diminuir o volume {n}", "ru": "громкость -{n}", "ja": "音量を{n}下げる", "ko": "볼륨 {n} 내리기", "zh-Hans": "音量调低 {n}", "id": "turunkan volume {n}"},
	"đặt âm lượng {n}%":                 {"en": "set the volume to {n}%", "de": "Lautstärke auf {n} % stellen", "fr": "régler le volume à {n} %", "es": "poner el volumen al {n} %", "pt-BR": "ajustar o volume para {n}%", "ru": "громкость {n}%", "ja": "音量を{n}%にする", "ko": "볼륨 {n}%로 설정", "zh-Hans": "音量设为 {n}%", "id": "atur volume ke {n}%"},
	"bật tiếng":                         {"en": "unmute", "de": "Ton an", "fr": "réactiver le son", "es": "activar el sonido", "pt-BR": "ativar o som", "ru": "включить звук", "ja": "ミュート解除", "ko": "음소거 해제", "zh-Hans": "取消静音", "id": "nyalakan suara"},
	"tắt tiếng":                         {"en": "mute", "de": "Ton aus", "fr": "couper le son", "es": "silenciar", "pt-BR": "silenciar", "ru": "выключить звук", "ja": "ミュート", "ko": "음소거", "zh-Hans": "静音", "id": "bisukan"},
	"khóa màn hình":                     {"en": "lock the screen", "de": "Bildschirm sperren", "fr": "verrouiller l'écran", "es": "bloquear la pantalla", "pt-BR": "bloquear a tela", "ru": "заблокировать экран", "ja": "画面をロック", "ko": "화면 잠그기", "zh-Hans": "锁定屏幕", "id": "kunci layar"},
	"tắt màn hình":                      {"en": "turn the screen off", "de": "Bildschirm ausschalten", "fr": "éteindre l'écran", "es": "apagar la pantalla", "pt-BR": "desligar a tela", "ru": "выключить экран", "ja": "画面をオフ", "ko": "화면 끄기", "zh-Hans": "关闭屏幕", "id": "matikan layar"},
	"giảm độ sáng màn hình":             {"en": "dim the screen", "de": "Bildschirm dunkler", "fr": "baisser la luminosité", "es": "bajar el brillo", "pt-BR": "diminuir o brilho", "ru": "уменьшить яркость", "ja": "画面を暗くする", "ko": "화면 밝기 낮추기", "zh-Hans": "调低屏幕亮度", "id": "redupkan layar"},
	"tăng độ sáng màn hình":             {"en": "brighten the screen", "de": "Bildschirm heller", "fr": "augmenter la luminosité", "es": "subir el brillo", "pt-BR": "aumentar o brilho", "ru": "увеличить яркость", "ja": "画面を明るくする", "ko": "화면 밝기 높이기", "zh-Hans": "调高屏幕亮度", "id": "terangkan layar"},
	"cho máy ngủ":                       {"en": "put the computer to sleep", "de": "Computer in den Ruhezustand", "fr": "mettre l'ordinateur en veille", "es": "suspender el equipo", "pt-BR": "colocar o computador em repouso", "ru": "перевести компьютер в сон", "ja": "コンピュータをスリープ", "ko": "컴퓨터 잠자기", "zh-Hans": "让电脑睡眠", "id": "tidurkan komputer"},
	"chạy phím tắt {name}":              {"en": "run the shortcut {name}", "de": "Kurzbefehl {name} ausführen", "fr": "lancer le raccourci {name}", "es": "ejecutar el atajo {name}", "pt-BR": "executar o atalho {name}", "ru": "запустить быструю команду {name}", "ja": "ショートカット {name} を実行", "ko": "단축어 {name} 실행", "zh-Hans": "运行快捷指令 {name}", "id": "jalankan pintasan {name}"},
	"cho Claude chạy {c}{where}":        {"en": "let Claude run {c}{where}", "de": "Claude {c} ausführen lassen{where}", "fr": "laisser Claude lancer {c}{where}", "es": "dejar que Claude ejecute {c}{where}", "pt-BR": "deixar o Claude executar {c}{where}", "ru": "разрешить Claude выполнить {c}{where}", "ja": "Claudeに{c}の実行を許可{where}", "ko": "Claude가 {c} 실행하도록 허용{where}", "zh-Hans": "允许 Claude 运行 {c}{where}", "id": "izinkan Claude menjalankan {c}{where}"},
	"duyệt yêu cầu của Claude{where}":   {"en": "approve Claude's request{where}", "de": "Claudes Anfrage genehmigen{where}", "fr": "approuver la demande de Claude{where}", "es": "aprobar la solicitud de Claude{where}", "pt-BR": "aprovar o pedido do Claude{where}", "ru": "одобрить запрос Claude{where}", "ja": "Claudeの要求を承認{where}", "ko": "Claude 요청 승인{where}", "zh-Hans": "批准 Claude 的请求{where}", "id": "setujui permintaan Claude{where}"},
	"từ chối yêu cầu của Claude{where}": {"en": "deny Claude's request{where}", "de": "Claudes Anfrage ablehnen{where}", "fr": "refuser la demande de Claude{where}", "es": "rechazar la solicitud de Claude{where}", "pt-BR": "recusar o pedido do Claude{where}", "ru": "отклонить запрос Claude{where}", "ja": "Claudeの要求を拒否{where}", "ko": "Claude 요청 거부{where}", "zh-Hans": "拒绝 Claude 的请求{where}", "id": "tolak permintaan Claude{where}"},
	"trả lời Claude{where}: {text}":     {"en": "reply to Claude{where}: {text}", "de": "Claude antworten{where}: {text}", "fr": "répondre à Claude{where} : {text}", "es": "responder a Claude{where}: {text}", "pt-BR": "responder ao Claude{where}: {text}", "ru": "ответить Claude{where}: {text}", "ja": "Claudeに返信{where}: {text}", "ko": "Claude에게 답장{where}: {text}", "zh-Hans": "回复 Claude{where}：{text}", "id": "balas Claude{where}: {text}"},
	"dừng Claude{where}":                {"en": "stop Claude{where}", "de": "Claude stoppen{where}", "fr": "arrêter Claude{where}", "es": "detener a Claude{where}", "pt-BR": "parar o Claude{where}", "ru": "остановить Claude{where}", "ja": "Claudeを停止{where}", "ko": "Claude 중지{where}", "zh-Hans": "停止 Claude{where}", "id": "hentikan Claude{where}"},
	"mở phiên Claude{where}":            {"en": "open the Claude session{where}", "de": "Claude-Sitzung öffnen{where}", "fr": "ouvrir la session Claude{where}", "es": "abrir la sesión de Claude{where}", "pt-BR": "abrir a sessão do Claude{where}", "ru": "открыть сессию Claude{where}", "ja": "Claudeのセッションを開く{where}", "ko": "Claude 세션 열기{where}", "zh-Hans": "打开 Claude 会话{where}", "id": "buka sesi Claude{where}"},
	" ở phiên {name}":                   {"en": " in {name}", "de": " in {name}", "fr": " dans {name}", "es": " en {name}", "pt-BR": " em {name}", "ru": " в {name}", "ja": " ({name})", "ko": " ({name})", "zh-Hans": "（{name}）", "id": " di {name}"},
	"giao việc mới cho Claude: {task}":  {"en": "give Claude a new task: {task}", "de": "Claude eine neue Aufgabe geben: {task}", "fr": "confier une nouvelle tâche à Claude : {task}", "es": "dar una nueva tarea a Claude: {task}", "pt-BR": "dar uma nova tarefa ao Claude: {task}", "ru": "дать Claude новую задачу: {task}", "ja": "Claudeに新しいタスク: {task}", "ko": "Claude에게 새 작업: {task}", "zh-Hans": "给 Claude 新任务：{task}", "id": "beri Claude tugas baru: {task}"},
	"Bấm để xác nhận: {what}":           {"en": "Tap to confirm: {what}", "de": "Zum Bestätigen tippen: {what}", "fr": "Touchez pour confirmer : {what}", "es": "Toca para confirmar: {what}", "pt-BR": "Toque para confirmar: {what}", "ru": "Нажмите, чтобы подтвердить: {what}", "ja": "タップして確認: {what}", "ko": "탭하여 확인: {what}", "zh-Hans": "点按确认：{what}", "id": "Ketuk untuk konfirmasi: {what}"},
}

// fit: at most n letters, cut on a letter boundary, without an ellipsis (owner rule: no "…" on screens).
func fit(s string, n int) string {
	r := []rune(strings.Join(strings.Fields(s), " "))
	if len(r) <= n {
		return string(r)
	}
	return strings.TrimRight(string(r[:n]), " ·,;:-")
}

// stateOf: the Mac's stored state (a json column comes back as a map, or as text from older rows).
func stateOf(v any) map[string]any {
	if m, ok := v.(map[string]any); ok {
		return m
	}
	m := map[string]any{}
	if s, isText := v.(string); isText && s != "" {
		_ = json.Unmarshal([]byte(s), &m)
	}
	return m
}

// noteKnock: a device of the account the Mac does not take commands from (allow_from) tried one. desk_state.knocks
// keeps it (once a day per device, at most 10, a week long); desktop_sync hands the last day's to the Mac, which lists
// the device under Máy ra lệnh and tells the person once a day (2026-11-23: a new S3's keys were refused and the
// device could not even be ticked on the Mac, which only knew devices whose commands had reached it).
func noteKnock(row map[string]any, devID int, name, via string, now int64) {
	if row == nil || devID <= 0 || wm.Int(row["id"]) == 0 {
		return
	}
	old := stateOf(row["knocks"])
	key := fmt.Sprint(devID)
	if k := stateOf(old[key]); len(k) > 0 && now-int64(wm.Num(k["ms"])) < 24*3600*1000 {
		return // already noted today
	}
	keep := map[string]any{key: map[string]any{"name": name, "via": via, "ms": now}}
	type kn struct {
		k  string
		ms int64
	}
	var rest []kn
	for k, v := range old {
		if m := stateOf(v); len(m) > 0 && k != key && now-int64(wm.Num(m["ms"])) < 7*24*3600*1000 {
			rest = append(rest, kn{k, int64(wm.Num(m["ms"]))})
		}
	}
	sort.Slice(rest, func(i, j int) bool { return rest[i].ms > rest[j].ms })
	for i, r := range rest {
		if i >= 9 {
			break
		}
		keep[r.k] = old[r.k]
	}
	if _, err := wm.Update("desk_state", wm.Int(row["id"]), map[string]any{"knocks": keep}); err != nil {
		wm.Log("knock: ", err.Error())
	}
}

// newID: 32 hex from the time, the run and the desktop's token hash (a value only the server holds), so a nonce or a
// command id cannot be guessed; the platform offers no random source to code nodes.
func newID(parts ...any) string {
	ctx := wm.Context()
	seed := fmt.Sprint(parts...) + "|" + fmt.Sprint(time.Now().UnixNano()) + "|" + fmt.Sprint(ctx["run_id"]) + "|" +
		fmt.Sprint(ctx["node_id"]) // joined as text, not appended to parts (applint -strict)
	sum := sha256.Sum256([]byte(seed))
	return hex.EncodeToString(sum[:16])
}

func uuidOf(h string) string {
	b := []byte(h)
	b[12] = '4'
	b[16] = "89ab"[strings.IndexByte("0123456789abcdef", b[16])%4]
	return string(b[0:8]) + "-" + string(b[8:12]) + "-" + string(b[12:16]) + "-" + string(b[16:20]) + "-" + string(b[20:32])
}

// confirmWhere: where a risky command may get its yes (state.confirm_at): "either" (the Mac or the web), "mac", "web".
func confirmWhere(at string) string {
	switch at {
	case "mac":
		return "mac"
	case "s3":
		return "web" // the account side: the web (and, once its app ships, an S3)
	}
	return "either"
}

func refuse(status int, code, what string) map[string]any {
	return map[string]any{"ok": false, "status": status, "error": code, "what": what}
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	if wm.Str(in["on"]) != "1" {
		return refuse(503, "desktop_disabled", "")
	}
	uid := wm.Int(in["uid"])
	cmd := strings.TrimSpace(wm.Str(in["cmd"]))
	if uid <= 0 {
		return refuse(401, "unauthorized", "")
	}
	if _, known := deskCmds[cmd]; !known {
		return refuse(422, "unknown_command", "")
	}
	via := wm.Str(in["via"])
	if via != "voice" && via != "app" && via != "web" && via != "cli" {
		via = "voice"
	}
	fromID := wm.Int(in["from_device_id"])
	now := time.Now().UnixMilli()
	// The Sessions app's nonce (§9.3): the same key gives the same command, so a retry never types twice.
	key := strings.TrimSpace(wm.Str(in["key"]))
	keyID := ""
	if key != "" {
		sum := sha256.Sum256([]byte(fmt.Sprint("desk-key|", uid, "|", fromID, "|", key)))
		keyID = uuidOf(hex.EncodeToString(sum[:16]))
		had, herr := wm.QueryOpts("desk_cmds", map[string]any{"filters": []map[string]any{
			{"column": "cmd_id", "op": "eq", "value": keyID}, {"column": "user_id", "op": "eq", "value": uid}}, "limit": 1})
		if herr != nil {
			wm.Log("read: ", herr.Error())
			return refuse(500, "server_error", "")
		}
		if len(had) == 1 {
			h := had[0]
			res, _ := h["result"].(map[string]any)
			return map[string]any{"ok": true, "status": 200, "id": keyID, "state": wm.Str(h["state"]), "risk": wm.Str(h["risk"]),
				"exp_ms": int64(wm.Num(h["exp_ms"])), "desktop_id": wm.Int(h["desktop_id"]), "again": true,
				"text": wm.Str(res["text"]), "err": wm.Str(res["err"]), "chan": "", "push": "",
				"wait": (wm.Str(h["state"]) == "queued" || wm.Str(h["state"]) == "sent") && !fastKey(wm.Str(h["via"]), wm.Str(h["cmd"]))}
		}
	}

	res, err := wm.QueryMany([]map[string]any{
		{"table": "devices", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid},
			{"column": "revoked", "op": "eq", "value": false}}, "order_by": "id", "limit": 50, "with_total": false},
		{"table": "desk_state", "filters": []map[string]any{{"column": "user_id", "op": "eq", "value": uid}},
			"order_by": "seen_ms", "desc": true, "limit": 10, "with_total": false},
	})
	if err != nil {
		wm.Log("read: ", err.Error())
		return refuse(500, "server_error", "")
	}
	rowsOf := func(i int) []map[string]any {
		out := []map[string]any{}
		if i >= len(res) {
			return out
		}
		switch r := res[i]["rows"].(type) {
		case []map[string]any:
			return r
		case []any:
			for _, x := range r {
				if m, ok := x.(map[string]any); ok {
					out = append(out, m)
				}
			}
		}
		return out
	}
	desks := map[int]map[string]any{}
	for _, d := range rowsOf(0) {
		if wm.Str(d["kind"]) == "desktop" {
			desks[wm.Int(d["id"])] = d
		}
	}
	want := wm.Int(in["desktop_id"])
	var dev, row map[string]any
	for _, s := range rowsOf(1) { // newest first: without a desktop named, the one seen last
		id := wm.Int(s["device_id"])
		if desks[id] == nil || (want > 0 && id != want) {
			continue
		}
		dev = desks[id]
		row = s
		break
	}
	if dev == nil && want > 0 {
		dev = desks[want]
	}
	if dev == nil && want == 0 {
		for _, d := range desks {
			dev = d
		}
	}
	if dev == nil {
		return refuse(404, "no_desktop", "")
	}
	deskID := wm.Int(dev["id"])
	if row == nil || now-int64(wm.Num(row["seen_ms"])) > onlineMs {
		return refuse(409, "desktop_offline", "")
	}
	st := stateOf(row["state"])
	args, okArgs := checkArgs(cmd, in["args"], via == "app")
	what := ""
	if okArgs {
		what = cmdText(cmd, args, st)
	}
	if wm.Str(row["mode"]) != "on" && wm.Str(row["mode"]) != "" {
		return refuse(409, "desktop_view_only", what)
	}
	cmds, _ := st["commands"].(map[string]any)
	risk := wm.Str(cmds[cmd])
	if risk != "safe" && risk != "confirm" {
		return refuse(403, "command_off", what)
	}
	if allow, isList := st["allow_from"].([]any); isList && via != "web" {
		ok := false
		for _, x := range allow {
			if wm.Int(x) == fromID && fromID > 0 {
				ok = true
			}
		}
		if !ok {
			noteKnock(row, fromID, fit(wm.Str(in["from_name"]), 40), via, now)
			return refuse(403, "sender_not_allowed", what)
		}
	}
	if !okArgs {
		return refuse(422, "bad_args", "")
	}
	if cmd == "claude.approve" {
		risk = "confirm" // always a person's yes (§2.1)
	}
	if cmd == "shortcut.run" {
		l, _ := st["shortcuts"].([]any)
		for _, x := range l {
			sc, _ := x.(map[string]any)
			if sc != nil && wm.Str(sc["name"]) == wm.Str(args["name"]) && wm.Bool(sc["risky"]) {
				risk = "confirm"
			}
		}
	}
	recent, err := wm.QueryOpts("desk_cmds", map[string]any{"filters": []map[string]any{
		{"column": "desktop_id", "op": "eq", "value": deskID}, {"column": "created_ms", "op": "gte", "value": now - 60000}},
		"order_by": "created_ms", "desc": true, "limit": appPerMin + 1})
	if err != nil {
		wm.Log("read: ", err.Error())
		return refuse(500, "server_error", what)
	}
	risky, nOther := 0, 0 // the Sessions app's typing has its own budget (below)
	for _, r := range recent {
		if typing(wm.Str(r["via"]), wm.Str(r["cmd"])) {
			continue
		}
		nOther++
		if wm.Str(r["risk"]) == "confirm" {
			risky++
		}
	}
	appCmd := typing(via, cmd)
	if appCmd {
		// typing from the Sessions app: its own, larger budget (§9.3); the tap on the device is the yes
		nApp := 0
		for _, r := range recent {
			if typing(wm.Str(r["via"]), wm.Str(r["cmd"])) {
				nApp++
			}
		}
		if nApp >= appPerMin {
			return refuse(429, "rate_limited", what)
		}
	} else if nOther >= perMin || (risk == "confirm" && risky >= confirmsPerMin) {
		return refuse(429, "rate_limited", what)
	}

	confirmAt := wm.Str(st["confirm_at"]) // "s3" (the account side: an S3 or the web), "mac" or "either"
	state := "queued"
	exp := now + safeTTL
	var confirmed any
	if risk == "confirm" {
		if wm.Bool(in["confirm"]) && via == "web" && confirmAt != "mac" {
			confirmed = map[string]any{"by": "web", "where": "web", "at_ms": now} // the page asked the person first
		} else if wm.Bool(in["confirm"]) && via == "app" && fromID > 0 && confirmAt != "mac" {
			confirmed = map[string]any{"by": "s3", "where": "s3", "at_ms": now} // the person's tap on the device (§9.3)
		} else if wm.Bool(in["confirm"]) && via == "voice" && fromID > 0 && confirmAt != "mac" &&
			(wm.Str(st["confirm_by"]) == "" || wm.Str(st["confirm_by"]) == "voice") {
			// the person said yes to the dog's read-back (assist_reply holds it a turn; the Mac's "confirm by" is voice)
			confirmed = map[string]any{"by": "voice", "where": "s3", "at_ms": now}
		} else {
			state = "confirm"
			exp = now + confirmTTL
		}
	}
	fromName := fit(wm.Str(in["from_name"]), 40)
	if fromName == "" && via == "web" {
		fromName = "web"
	}
	id := uuidOf(newID(deskID, cmd, uid, wm.Str(dev["token_hash"])))
	if keyID != "" {
		id = keyID
	}
	vals := map[string]any{
		"cmd_id": id, "user_id": uid, "desktop_id": deskID, "from_device_id": fromID, "from_name": fromName, "via": via,
		"heard": fit(wm.Str(in["heard"]), 300), "cmd": cmd, "args": args, "risk": risk, "state": state,
		"nonce": newID(id, "nonce", wm.Str(dev["token_hash"])), "exp_ms": exp, "created_ms": now, "sent_ms": 0, "done_ms": 0,
	}
	if confirmed != nil {
		vals["confirmed"] = confirmed
	}
	ops := []map[string]any{{"op": "insert", "table": "desk_cmds", "values": vals}}
	if state == "confirm" {
		// The S3s show it as an "EspOS" note within a minute (the note raises the ping's rev).
		titles := map[string]any{}
		for _, l := range cmdLangs {
			titles[l] = fit(say(l, "Bấm để xác nhận: {what}", "{what}", cmdTextIn(l, cmd, args, st)), 120)
		}
		ops = append(ops, map[string]any{"op": "insert", "table": "notifications", "values": map[string]any{
			"user_id": uid, "title": fit("Bấm để xác nhận: "+what, 120), "source": "desktop", "data": map[string]any{"titles": titles}}})
	}
	if _, berr := wm.Batch(ops); berr != nil {
		wm.Log("save: ", berr.Error())
		return refuse(500, "server_error", what)
	}
	// The poke channel is rt:desk:<desktop>:<chan_key> (node pub writes the "rt:desk:" part: the platform checks a
	// channel's kind in the drawing).
	waitS := 2
	if cmd == "yt.play" || cmd == "claude.status" {
		waitS = 9 // yt.play: search, open, the check it plays; claude.status: a report from the last 2 min comes at once
	}
	// a key: the command itself goes with the poke (as JSON text; the same shape desktop_sync sends), and the S3 is
	// answered at once: the screen shows the key's effect
	push := ""
	wait := state == "queued"
	if fastKey(via, cmd) && state == "queued" {
		wait = false
		pb, _ := json.Marshal(map[string]any{"id": id, "cmd": cmd, "args": args, "risk": risk, "state": "queued", "exp_ms": exp,
			"nonce": vals["nonce"], "from": map[string]any{"device_id": fromID, "name": fromName, "via": via}, "heard": "",
			"confirmed": confirmed})
		push = string(pb)
	}
	return map[string]any{"ok": true, "status": 200, "id": id, "state": state, "risk": risk, "what": what,
		"exp_ms": exp, "desktop_id": deskID, "chan": fmt.Sprintf("%d:%s", deskID, wm.Str(row["chan_key"])), "wait": wait,
		"wait_s": waitS, "where": confirmWhere(confirmAt), "push": push}
}
