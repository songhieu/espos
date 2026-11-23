// Workflow build_dashboard, node dash (imported by tools/importer).
package dash

import (
	"encoding/json"
	"espos/internal/wm"
	"fmt"
	"math"
	"net/url"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Default time zone: Việt Nam, GMT+7. Each device may set sys.tz_min (web setting).
const defaultTzMin = 420

// tzMinutes reads sys.tz_min, a whole-hour offset (or India's +5:30) in minutes.
func tzMinutes(st map[string]string) int {
	v, err := strconv.Atoi(strings.TrimSpace(st["sys.tz_min"]))
	if err != nil || v < -720 || v > 840 {
		return defaultTzMin
	}
	return v
}

// tzName is the IANA zone Open-Meteo reports in for that fixed offset.
func tzName(min int) string {
	if min == 330 {
		return "Asia/Kolkata"
	}
	h := min / 60
	if h == 0 {
		return "GMT"
	}
	if h > 0 {
		return fmt.Sprintf("Etc/GMT-%d", h)
	}
	return fmt.Sprintf("Etc/GMT+%d", -h)
}

var weekdays = []string{"CHỦ NHẬT", "THỨ HAI", "THỨ BA", "THỨ TƯ", "THỨ NĂM", "THỨ SÁU", "THỨ BẢY"}
var can = []string{"Giáp", "Ất", "Bính", "Đinh", "Mậu", "Kỷ", "Canh", "Tân", "Nhâm", "Quý"}
var chi = []string{"Tý", "Sửu", "Dần", "Mão", "Thìn", "Tỵ", "Ngọ", "Mùi", "Thân", "Dậu", "Tuất", "Hợi"}

// ---------- the server's own words, in the device's language (2026-11-23) ----------
//
// The dashboard's words (weather, days, the default city, calendar fallbacks, news sections, Claude cards, gold names)
// were always Vietnamese; an English S3 showed "Nhiều mây". Now they follow the device's sys.lang, in the firmware's
// languages (s3desk/i18n/languages.json). Each key is the Vietnamese text sent until now, so a Vietnamese device gets
// exactly the same bytes; a language missing from an entry falls back to English, then to the key. Words the firmware
// already has (news sections, Hanoi, Today, All day) are its own translations (scratch generator gen.py).

// dashLang: the language the dashboard speaks to a device. The S3: its sys.lang (English built in, so English when
// unset; an unknown tag → English). The EspOS Bot: English or Vietnamese, as its own screens (src/bot/app.cpp; it
// starts in Vietnamese). The X4 and anything else: Vietnamese, as always.
func dashLang(sysLang, kind string) string {
	l := strings.TrimSpace(sysLang)
	switch kind {
	case "s3":
		if l == "" {
			return "en"
		}
		for _, t := range []string{"vi", "en", "de", "fr", "es", "pt-BR", "ru", "ja", "ko", "zh-Hans", "id"} {
			if strings.EqualFold(l, t) {
				return t
			}
		}
		switch strings.ToLower(strings.SplitN(strings.ReplaceAll(l, "_", "-"), "-", 2)[0]) {
		case "pt":
			return "pt-BR"
		case "zh":
			return "zh-Hans"
		case "vi", "en", "de", "fr", "es", "ru", "ja", "ko", "id":
			return strings.ToLower(l[:2])
		}
		return "en"
	case "bot":
		if l == "en" {
			return "en"
		}
		return "vi"
	}
	return "vi"
}

// tr: a word in the language (vi: the key itself).
func tr(lang, vi string) string {
	if lang == "vi" {
		return vi
	}
	if row, ok := words[vi]; ok {
		if v := row[lang]; v != "" {
			return v
		}
		if v := row["en"]; v != "" {
			return v
		}
	}
	return vi
}

// lunarYear: the lunar year's name: Vietnamese "Giáp Thìn" (as always), the stem and branch characters in Chinese and
// Japanese (甲辰) and Korean (갑진), the animal elsewhere ("Dragon").
func lunarYear(lang string, stem, branch int) string {
	switch lang {
	case "vi":
		return can[stem] + " " + chi[branch]
	case "zh-Hans", "ja":
		return string([]rune("甲乙丙丁戊己庚辛壬癸")[stem]) + string([]rune("子丑寅卯辰巳午未申酉戌亥")[branch])
	case "ko":
		return string([]rune("갑을병정무기경신임계")[stem]) + string([]rune("자축인묘진사오미신유술해")[branch])
	}
	if a, ok := animals[lang]; ok {
		return a[branch]
	}
	return animals["en"][branch]
}

// trEvents: the calendar's two words in today's list and the agenda (built, and cached per device, in Vietnamese).
func trEvents(lang string, list []any) {
	if lang == "vi" {
		return
	}
	for _, x := range list {
		if m, ok := x.(map[string]any); ok {
			if m["time"] == "Cả ngày" {
				m["time"] = tr(lang, "Cả ngày")
			}
			if m["title"] == "(Không tiêu đề)" {
				m["title"] = tr(lang, "(Không tiêu đề)")
			}
		}
	}
}

// goldNames: market_tick's gold names in the language: "Vàng SJC", "Nhẫn 9999", "Nữ trang 999" by their pattern; a
// brand ("Kim Bảo", "PNJ Phượng Hoàng") without its Vietnamese marks outside Vietnamese (the built-in fonts are
// Latin-1). Copies, filled by index (applint -strict).
func goldNames(lang string, items []any) []any {
	if lang == "vi" {
		return items
	}
	out := make([]any, len(items))
	for i, x := range items {
		m, ok := x.(map[string]any)
		if !ok {
			out[i] = x
			continue
		}
		c := map[string]any{}
		for k, v := range m {
			c[k] = v
		}
		n := wm.Str(m["n"])
		name := ""
		for _, pre := range []string{"Vàng ", "Nhẫn ", "Nữ trang "} {
			if strings.HasPrefix(n, pre) {
				x := strings.TrimPrefix(n, pre)
				if lang == "en" || lang == "ja" || lang == "ko" || lang == "zh-Hans" {
					x = strings.ReplaceAll(x, ",", ".") // "16,3K" → "16.3K"
				}
				name = strings.ReplaceAll(tr(lang, pre+"{x}"), "{x}", foldVi(x))
				break
			}
		}
		if name == "" {
			name = foldVi(n)
		}
		c["n"] = name
		out[i] = c
	}
	return out
}

// foldVi: Vietnamese letters without their marks ("Phượng Hoàng" → "Phuong Hoang").
func foldVi(s string) string {
	var b strings.Builder
	for _, r := range s {
		if f, ok := viFold[r]; ok {
			b.WriteRune(f)
		} else {
			b.WriteRune(r)
		}
	}
	return b.String()
}

var viFold = func() map[rune]rune {
	m := map[rune]rune{'đ': 'd', 'Đ': 'D'}
	for base, set := range map[rune]string{
		'a': "àáảãạăằắẳẵặâầấẩẫậ", 'e': "èéẻẽẹêềếểễệ", 'i': "ìíỉĩị", 'o': "òóỏõọôồốổỗộơờớởỡợ", 'u': "ùúủũụưừứửữự", 'y': "ỳýỷỹỵ",
		'A': "ÀÁẢÃẠĂẰẮẲẴẶÂẦẤẨẪẬ", 'E': "ÈÉẺẼẸÊỀẾỂỄỆ", 'I': "ÌÍỈĨỊ", 'O': "ÒÓỎÕỌÔỒỐỔỖỘƠỜỚỞỠỢ", 'U': "ÙÚỦŨỤƯỪỨỬỮỰ", 'Y': "ỲÝỶỸỴ"} {
		for _, r := range set {
			m[r] = base
		}
	}
	return m
}()

var words = map[string]map[string]string{
	"Trời quang":           {"en": "Clear", "de": "Klar", "fr": "Dégagé", "es": "Despejado", "pt-BR": "Céu limpo", "ru": "Ясно", "ja": "快晴", "ko": "맑음", "zh-Hans": "晴", "id": "Cerah"},
	"Ít mây":               {"en": "Mostly clear", "de": "Überwiegend klar", "fr": "Peu nuageux", "es": "Mayormente despejado", "pt-BR": "Poucas nuvens", "ru": "Малооблачно", "ja": "晴れ", "ko": "대체로 맑음", "zh-Hans": "少云", "id": "Sedikit berawan"},
	"Mây rải rác":          {"en": "Partly cloudy", "de": "Teilweise bewölkt", "fr": "Partiellement nuageux", "es": "Parcialmente nublado", "pt-BR": "Parcialmente nublado", "ru": "Переменная облачность", "ja": "晴れ時々曇り", "ko": "구름 조금", "zh-Hans": "多云", "id": "Berawan sebagian"},
	"Nhiều mây":            {"en": "Cloudy", "de": "Bewölkt", "fr": "Nuageux", "es": "Nublado", "pt-BR": "Nublado", "ru": "Пасмурно", "ja": "曇り", "ko": "흐림", "zh-Hans": "阴", "id": "Berawan"},
	"Sương mù":             {"en": "Fog", "de": "Nebel", "fr": "Brouillard", "es": "Niebla", "pt-BR": "Neblina", "ru": "Туман", "ja": "霧", "ko": "안개", "zh-Hans": "雾", "id": "Kabut"},
	"Mưa phùn":             {"en": "Drizzle", "de": "Nieselregen", "fr": "Bruine", "es": "Llovizna", "pt-BR": "Garoa", "ru": "Морось", "ja": "霧雨", "ko": "이슬비", "zh-Hans": "毛毛雨", "id": "Gerimis"},
	"Mưa nhỏ":              {"en": "Light rain", "de": "Leichter Regen", "fr": "Pluie faible", "es": "Lluvia ligera", "pt-BR": "Chuva fraca", "ru": "Небольшой дождь", "ja": "小雨", "ko": "약한 비", "zh-Hans": "小雨", "id": "Hujan ringan"},
	"Mưa vừa":              {"en": "Rain", "de": "Regen", "fr": "Pluie modérée", "es": "Lluvia moderada", "pt-BR": "Chuva moderada", "ru": "Дождь", "ja": "雨", "ko": "비", "zh-Hans": "中雨", "id": "Hujan sedang"},
	"Mưa to":               {"en": "Heavy rain", "de": "Starker Regen", "fr": "Forte pluie", "es": "Lluvia fuerte", "pt-BR": "Chuva forte", "ru": "Сильный дождь", "ja": "大雨", "ko": "폭우", "zh-Hans": "大雨", "id": "Hujan lebat"},
	"Tuyết":                {"en": "Snow", "de": "Schnee", "fr": "Neige", "es": "Nieve", "pt-BR": "Neve", "ru": "Снег", "ja": "雪", "ko": "눈", "zh-Hans": "雪", "id": "Salju"},
	"Mưa rào":              {"en": "Showers", "de": "Schauer", "fr": "Averses", "es": "Chubascos", "pt-BR": "Pancadas de chuva", "ru": "Ливень", "ja": "にわか雨", "ko": "소나기", "zh-Hans": "阵雨", "id": "Hujan sesaat"},
	"Dông":                 {"en": "Thunderstorm", "de": "Gewitter", "fr": "Orage", "es": "Tormenta", "pt-BR": "Trovoada", "ru": "Гроза", "ja": "雷雨", "ko": "뇌우", "zh-Hans": "雷阵雨", "id": "Badai petir"},
	"Hà Nội":               {"en": "Hanoi", "de": "Hanoi", "fr": "Hanoï", "es": "Hanói", "pt-BR": "Hanoi", "ru": "Ханой", "ja": "ハノイ", "ko": "하노이", "zh-Hans": "河内", "id": "Hanoi"},
	"Hôm nay":              {"en": "Today", "de": "Heute", "fr": "Aujourd'hui", "es": "Hoy", "pt-BR": "Hoje", "ru": "Сегодня", "ja": "今日", "ko": "오늘", "zh-Hans": "今天", "id": "Hari ini"},
	"CN":                   {"en": "Sun", "de": "So", "fr": "Dim", "es": "Dom", "pt-BR": "Dom", "ru": "Вс", "ja": "日", "ko": "일", "zh-Hans": "周日", "id": "Min"},
	"T2":                   {"en": "Mon", "de": "Mo", "fr": "Lun", "es": "Lun", "pt-BR": "Seg", "ru": "Пн", "ja": "月", "ko": "월", "zh-Hans": "周一", "id": "Sen"},
	"T3":                   {"en": "Tue", "de": "Di", "fr": "Mar", "es": "Mar", "pt-BR": "Ter", "ru": "Вт", "ja": "火", "ko": "화", "zh-Hans": "周二", "id": "Sel"},
	"T4":                   {"en": "Wed", "de": "Mi", "fr": "Mer", "es": "Mié", "pt-BR": "Qua", "ru": "Ср", "ja": "水", "ko": "수", "zh-Hans": "周三", "id": "Rab"},
	"T5":                   {"en": "Thu", "de": "Do", "fr": "Jeu", "es": "Jue", "pt-BR": "Qui", "ru": "Чт", "ja": "木", "ko": "목", "zh-Hans": "周四", "id": "Kam"},
	"T6":                   {"en": "Fri", "de": "Fr", "fr": "Ven", "es": "Vie", "pt-BR": "Sex", "ru": "Пт", "ja": "金", "ko": "금", "zh-Hans": "周五", "id": "Jum"},
	"T7":                   {"en": "Sat", "de": "Sa", "fr": "Sam", "es": "Sáb", "pt-BR": "Sáb", "ru": "Сб", "ja": "土", "ko": "토", "zh-Hans": "周六", "id": "Sab"},
	"CHỦ NHẬT":             {"en": "SUNDAY", "de": "SONNTAG", "fr": "DIMANCHE", "es": "DOMINGO", "pt-BR": "DOMINGO", "ru": "ВОСКРЕСЕНЬЕ", "ja": "日曜日", "ko": "일요일", "zh-Hans": "星期日", "id": "MINGGU"},
	"THỨ HAI":              {"en": "MONDAY", "de": "MONTAG", "fr": "LUNDI", "es": "LUNES", "pt-BR": "SEGUNDA-FEIRA", "ru": "ПОНЕДЕЛЬНИК", "ja": "月曜日", "ko": "월요일", "zh-Hans": "星期一", "id": "SENIN"},
	"THỨ BA":               {"en": "TUESDAY", "de": "DIENSTAG", "fr": "MARDI", "es": "MARTES", "pt-BR": "TERÇA-FEIRA", "ru": "ВТОРНИК", "ja": "火曜日", "ko": "화요일", "zh-Hans": "星期二", "id": "SELASA"},
	"THỨ TƯ":               {"en": "WEDNESDAY", "de": "MITTWOCH", "fr": "MERCREDI", "es": "MIÉRCOLES", "pt-BR": "QUARTA-FEIRA", "ru": "СРЕДА", "ja": "水曜日", "ko": "수요일", "zh-Hans": "星期三", "id": "RABU"},
	"THỨ NĂM":              {"en": "THURSDAY", "de": "DONNERSTAG", "fr": "JEUDI", "es": "JUEVES", "pt-BR": "QUINTA-FEIRA", "ru": "ЧЕТВЕРГ", "ja": "木曜日", "ko": "목요일", "zh-Hans": "星期四", "id": "KAMIS"},
	"THỨ SÁU":              {"en": "FRIDAY", "de": "FREITAG", "fr": "VENDREDI", "es": "VIERNES", "pt-BR": "SEXTA-FEIRA", "ru": "ПЯТНИЦА", "ja": "金曜日", "ko": "금요일", "zh-Hans": "星期五", "id": "JUMAT"},
	"THỨ BẢY":              {"en": "SATURDAY", "de": "SAMSTAG", "fr": "SAMEDI", "es": "SÁBADO", "pt-BR": "SÁBADO", "ru": "СУББОТА", "ja": "土曜日", "ko": "토요일", "zh-Hans": "星期六", "id": "SABTU"},
	"Cả ngày":              {"en": "All day", "de": "Ganztägig", "fr": "Toute la journée", "es": "Todo el día", "pt-BR": "Dia inteiro", "ru": "Весь день", "ja": "終日", "ko": "하루 종일", "zh-Hans": "全天", "id": "Seharian"},
	"(Không tiêu đề)":      {"en": "(No title)", "de": "(Ohne Titel)", "fr": "(Sans titre)", "es": "(Sin título)", "pt-BR": "(Sem título)", "ru": "(Без названия)", "ja": "(無題)", "ko": "(제목 없음)", "zh-Hans": "(无标题)", "id": "(Tanpa judul)"},
	"Tin mới nhất":         {"en": "Latest news", "de": "Neueste", "fr": "Dernières infos", "es": "Últimas noticias", "pt-BR": "Últimas notícias", "ru": "Последние", "ja": "最新ニュース", "ko": "최신 뉴스", "zh-Hans": "最新新闻", "id": "Berita terbaru"},
	"Tin nổi bật":          {"en": "Top stories", "de": "Top-Themen", "fr": "À la une", "es": "Destacadas", "pt-BR": "Destaques", "ru": "Главное", "ja": "トップニュース", "ko": "주요 기사", "zh-Hans": "要闻", "id": "Sorotan"},
	"Thời sự":              {"en": "Current affairs", "de": "Aktuelles", "fr": "Actualité", "es": "Actualidad", "pt-BR": "Atualidades", "ru": "Актуальное", "ja": "時事", "ko": "시사", "zh-Hans": "时事", "id": "Terkini"},
	"Thế giới":             {"en": "World", "de": "Welt", "fr": "Monde", "es": "Mundo", "pt-BR": "Mundo", "ru": "Мир", "ja": "世界", "ko": "세계", "zh-Hans": "国际", "id": "Dunia"},
	"Kinh doanh":           {"en": "Business", "de": "Wirtschaft", "fr": "Économie", "es": "Negocios", "pt-BR": "Negócios", "ru": "Бизнес", "ja": "ビジネス", "ko": "경제", "zh-Hans": "财经", "id": "Bisnis"},
	"Bất động sản":         {"en": "Real estate", "de": "Immobilien", "fr": "Immobilier", "es": "Inmobiliario", "pt-BR": "Imóveis", "ru": "Недвижимость", "ja": "不動産", "ko": "부동산", "zh-Hans": "房产", "id": "Properti"},
	"Khoa học công nghệ":   {"en": "Science & tech", "de": "Wissen & Technik", "fr": "Sciences et tech", "es": "Ciencia y tecnología", "pt-BR": "Ciência e tecnologia", "ru": "Наука и техника", "ja": "科学・技術", "ko": "과학·기술", "zh-Hans": "科技", "id": "Sains & teknologi"},
	"Sức khỏe":             {"en": "Health", "de": "Gesundheit", "fr": "Santé", "es": "Salud", "pt-BR": "Saúde", "ru": "Здоровье", "ja": "健康", "ko": "건강", "zh-Hans": "健康", "id": "Sehat"},
	"Giáo dục":             {"en": "Education", "de": "Bildung", "fr": "Éducation", "es": "Educación", "pt-BR": "Educação", "ru": "Образование", "ja": "教育", "ko": "교육", "zh-Hans": "教育", "id": "Pendidikan"},
	"Pháp luật":            {"en": "Law", "de": "Recht", "fr": "Justice", "es": "Justicia", "pt-BR": "Justiça", "ru": "Право", "ja": "法律", "ko": "법률", "zh-Hans": "法治", "id": "Hukum"},
	"Đời sống":             {"en": "Life", "de": "Leben", "fr": "Société", "es": "Vida", "pt-BR": "Família", "ru": "Жизнь", "ja": "暮らし", "ko": "생활", "zh-Hans": "生活", "id": "Gaya hidup"},
	"Du lịch":              {"en": "Travel", "de": "Reisen", "fr": "Voyage", "es": "Viajes", "pt-BR": "Viagem", "ru": "Путешествия", "ja": "旅行", "ko": "여행", "zh-Hans": "旅游", "id": "Wisata"},
	"Giải trí":             {"en": "Entertainment", "de": "Unterhaltung", "fr": "Divertissement", "es": "Entretenimiento", "pt-BR": "Entretenimento", "ru": "Развлечения", "ja": "エンタメ", "ko": "연예", "zh-Hans": "娱乐", "id": "Hiburan"},
	"Thể thao":             {"en": "Sports", "de": "Sport", "fr": "Sport", "es": "Deportes", "pt-BR": "Esportes", "ru": "Спорт", "ja": "スポーツ", "ko": "스포츠", "zh-Hans": "体育", "id": "Olahraga"},
	"Xe":                   {"en": "Cars", "de": "Auto", "fr": "Auto", "es": "Motor", "pt-BR": "Carros", "ru": "Авто", "ja": "自動車", "ko": "자동차", "zh-Hans": "汽车", "id": "Otomotif"},
	"Ý kiến":               {"en": "Opinion", "de": "Meinung", "fr": "Opinions", "es": "Opinión", "pt-BR": "Opinião", "ru": "Мнения", "ja": "オピニオン", "ko": "오피니언", "zh-Hans": "观点", "id": "Opini"},
	"Claude báo tình hình": {"en": "Claude's status report", "de": "Claudes Statusbericht", "fr": "Point de Claude", "es": "Informe de Claude", "pt-BR": "Relatório do Claude", "ru": "Отчёт Claude", "ja": "Claudeの状況報告", "ko": "Claude 상황 보고", "zh-Hans": "Claude 状态报告", "id": "Laporan Claude"},
	"Claude đang chờ bạn":  {"en": "Claude is waiting for you", "de": "Claude wartet auf dich", "fr": "Claude vous attend", "es": "Claude te espera", "pt-BR": "Claude está esperando você", "ru": "Claude ждёт вас", "ja": "Claudeが待っています", "ko": "Claude가 기다리고 있어요", "zh-Hans": "Claude 在等你", "id": "Claude menunggu Anda"},
	"Claude xin chạy lệnh": {"en": "Claude wants to run a command", "de": "Claude möchte einen Befehl ausführen", "fr": "Claude veut lancer une commande", "es": "Claude quiere ejecutar un comando", "pt-BR": "Claude quer executar um comando", "ru": "Claude хочет выполнить команду", "ja": "Claudeがコマンドの実行を求めています", "ko": "Claude가 명령 실행을 요청해요", "zh-Hans": "Claude 请求运行命令", "id": "Claude ingin menjalankan perintah"},
	"Claude xin sửa tệp":   {"en": "Claude wants to edit a file", "de": "Claude möchte eine Datei bearbeiten", "fr": "Claude veut modifier un fichier", "es": "Claude quiere editar un archivo", "pt-BR": "Claude quer editar um arquivo", "ru": "Claude хочет изменить файл", "ja": "Claudeがファイルの編集を求めています", "ko": "Claude가 파일 수정을 요청해요", "zh-Hans": "Claude 请求编辑文件", "id": "Claude ingin mengubah berkas"},
	"Claude xin quyền":     {"en": "Claude asks for permission", "de": "Claude bittet um Erlaubnis", "fr": "Claude demande une autorisation", "es": "Claude pide permiso", "pt-BR": "Claude pede permissão", "ru": "Claude просит разрешения", "ja": "Claudeが許可を求めています", "ko": "Claude가 권한을 요청해요", "zh-Hans": "Claude 请求权限", "id": "Claude meminta izin"},
	"{head} chờ bạn duyệt": {"en": "{head} is waiting for your OK", "de": "{head} wartet auf dein OK", "fr": "{head} attend votre accord", "es": "{head} espera tu aprobación", "pt-BR": "{head} aguarda sua aprovação", "ru": "{head} ждёт вашего одобрения", "ja": "{head}が承認を待っています", "ko": "{head}: 승인을 기다리고 있어요", "zh-Hans": "{head} 等你批准", "id": "{head} menunggu persetujuan Anda"},
	"{head} đang chờ bạn":  {"en": "{head} is waiting for you", "de": "{head} wartet auf dich", "fr": "{head} vous attend", "es": "{head} te espera", "pt-BR": "{head} está esperando você", "ru": "{head} ждёт вас", "ja": "{head}が待っています", "ko": "{head}: 기다리고 있어요", "zh-Hans": "{head} 在等你", "id": "{head} menunggu Anda"},
	"Bật máy này trong EspOS Desktop > Cài đặt > Máy ra lệnh": {"en": "Turn this device on in EspOS Desktop > Settings > Devices", "de": "Dieses Gerät in EspOS Desktop > Settings > Devices einschalten", "fr": "Activez cet appareil dans EspOS Desktop > Settings > Devices", "es": "Activa este dispositivo en EspOS Desktop > Settings > Devices", "pt-BR": "Ative este dispositivo no EspOS Desktop > Settings > Devices", "ru": "Включите это устройство в EspOS Desktop > Settings > Devices", "ja": "EspOS Desktop > Settings > Devices でこのデバイスをオンにしてください", "ko": "EspOS Desktop > Settings > Devices에서 이 기기를 켜세요", "zh-Hans": "在 EspOS Desktop > Settings > Devices 中开启此设备", "id": "Aktifkan perangkat ini di EspOS Desktop > Settings > Devices"},
	"Vàng {x}":     {"en": "{x} gold", "de": "Gold {x}", "fr": "Or {x}", "es": "Oro {x}", "pt-BR": "Ouro {x}", "ru": "Золото {x}", "ja": "金 {x}", "ko": "금 {x}", "zh-Hans": "黄金 {x}", "id": "Emas {x}"},
	"Nhẫn {x}":     {"en": "{x} ring", "de": "Ring {x}", "fr": "Bague {x}", "es": "Anillo {x}", "pt-BR": "Anel {x}", "ru": "Кольцо {x}", "ja": "指輪 {x}", "ko": "반지 {x}", "zh-Hans": "金戒指 {x}", "id": "Cincin {x}"},
	"Nữ trang {x}": {"en": "{x} jewelry", "de": "Schmuck {x}", "fr": "Bijoux {x}", "es": "Joyería {x}", "pt-BR": "Joias {x}", "ru": "Украшения {x}", "ja": "宝飾品 {x}", "ko": "장신구 {x}", "zh-Hans": "首饰 {x}", "id": "Perhiasan {x}"},
}

// animals: the lunar year's animal (Tý..Hợi order) where the language names the year by it; zh-Hans, ja and ko use
// the stem and branch characters instead (lunarYear).
var animals = map[string][]string{
	"en":    {"Rat", "Ox", "Tiger", "Rabbit", "Dragon", "Snake", "Horse", "Goat", "Monkey", "Rooster", "Dog", "Pig"},
	"de":    {"Ratte", "Ochse", "Tiger", "Hase", "Drache", "Schlange", "Pferd", "Ziege", "Affe", "Hahn", "Hund", "Schwein"},
	"fr":    {"Rat", "Buffle", "Tigre", "Lapin", "Dragon", "Serpent", "Cheval", "Chèvre", "Singe", "Coq", "Chien", "Cochon"},
	"es":    {"Rata", "Buey", "Tigre", "Conejo", "Dragón", "Serpiente", "Caballo", "Cabra", "Mono", "Gallo", "Perro", "Cerdo"},
	"pt-BR": {"Rato", "Boi", "Tigre", "Coelho", "Dragão", "Serpente", "Cavalo", "Cabra", "Macaco", "Galo", "Cão", "Porco"},
	"ru":    {"Крыса", "Бык", "Тигр", "Кролик", "Дракон", "Змея", "Лошадь", "Коза", "Обезьяна", "Петух", "Собака", "Свинья"},
	"id":    {"Tikus", "Kerbau", "Macan", "Kelinci", "Naga", "Ular", "Kuda", "Kambing", "Monyet", "Ayam", "Anjing", "Babi"},
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	in := wmIn
	deviceID := wm.Int(in["device_id"])
	userID := wm.Int(in["user_id"])
	if deviceID == 0 || userID == 0 {
		return wm.Fail("missing device")
	}
	now := time.Now()
	st, err := loadSettings(deviceID)
	if err != nil {
		return wm.Fail(err)
	}
	tzMin := tzMinutes(st)
	loc := time.FixedZone("local", tzMin*60)
	local := now.In(loc)
	blocks := st["dash.blocks"]
	if blocks == "" {
		blocks = "weather,lunar,calendar,notes"
	}
	on := func(b string) bool { return strings.Contains(","+blocks+",", ","+b+",") }

	ld, lm, ly, leap := solar2Lunar(local.Day(), int(local.Month()), local.Year(), 7)
	lunar := fmt.Sprintf("%d/%d", ld, lm)
	if leap {
		lunar += "N"
	}
	// The board: the colour desk (kind s3) runs apps (weather, calendar, markets, news) that
	// need more than the X4's dashboard, whatever dash.blocks says. An X4 gets what it always got.
	kind := ""
	if devs, derr := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "id", "op": "eq", "value": deviceID}}, "limit": 1}); derr == nil && len(devs) == 1 {
		kind = wm.Str(devs[0]["kind"])
	}
	lang := dashLang(st["sys.lang"], kind)
	d := map[string]any{
		"now_ms":  now.UnixMilli(),
		"tz_min":  tzMin,
		"date":    local.Format("02.01.2006"),
		"weekday": tr(lang, weekdays[int(local.Weekday())]),
		"layout":  orDefault(st["dash.layout"], "desk"),
		"next_s":  nextRefresh(st, local),
	}
	s3 := kind == "s3"
	if kind == "bot" {
		blocks = "weather,lunar" // the EspOS Bot's standby screens: weather and the date (solar and lunar)
	}
	if on("lunar") {
		d["lunar"] = lunar
		d["lunar_year"] = lunarYear(lang, (ly+6)%10, (ly+8)%12)
	}
	if on("weather") || s3 {
		w, werr := weather(st, now, local, s3, lang)
		if werr != nil {
			wm.Log("weather: ", werr.Error())
		} else {
			if kind == "bot" {
				if h, ok := w["_hum"]; ok {
					w["hum"] = h // the bot's rain screen ("ẩm 90%")
				}
			}
			delete(w, "_hum")
			d["weather"] = w
		}
	}
	if (on("calendar") || s3) && st["dash.ics_url"] != "" {
		ev, ag, eerr := calendar(deviceID, st["dash.ics_url"], now, local, s3)
		if eerr != nil {
			wm.Log("calendar: ", eerr.Error())
		} else {
			trEvents(lang, ev) // "Cả ngày", "(Không tiêu đề)": made (and cached per device) in Vietnamese
			d["events"] = ev
			if s3 {
				trEvents(lang, ag)
				d["agenda"] = ag
			}
		}
	}
	if on("notes") {
		n, nerr := notes(userID, deviceID, now, loc, lang)
		if nerr != nil {
			return wm.Fail(nerr)
		}
		d["notes"] = n
	}
	// Market widgets: only what the layouts hold, to keep the device's copy small
	// (the colour desk also gets its Markets app's fixed list).
	mk := marketNeeds(st, kind)
	if len(mk.gold) > 0 {
		g, _, gerr := marketCached("gold")
		if gerr != nil {
			return wm.Fail(gerr)
		}
		if g != nil {
			d["gold"] = map[string]any{"src": g["src"], "at": g["at"], "items": goldNames(lang, pick(g["items"], "c", mk.gold))}
		}
	}
	if len(mk.fx) > 0 {
		f, _, ferr := marketCached("fx")
		if ferr != nil {
			return wm.Fail(ferr)
		}
		if f != nil {
			d["fx"] = map[string]any{"at": f["at"], "items": pick(f["items"], "c", mk.fx)}
		}
	}
	if mk.news {
		n, nerr := newsTop(userID, s3, s3WidgetOpts(st, "news"), lang)
		if nerr != nil {
			return wm.Fail(nerr)
		}
		if kind == "bot" {
			n = map[string]any{"top": n["full"]} // whole headlines: the bot scrolls them (never "…")
		}
		delete(n, "full")
		d["news"] = n
	}
	if len(mk.coins) > 0 {
		cr, _, cerr := marketCached("crypto")
		if cerr != nil {
			return wm.Fail(cerr)
		}
		items, _ := cr["items"].(map[string]any)
		// filled by index: the live engine spreads a list appended into a []any (applint -strict)
		list := make([]any, len(mk.coins))
		n := 0
		for _, c := range mk.coins {
			if v := items[c]; v != nil {
				list[n] = v
				n++
			}
		}
		d["crypto"] = map[string]any{"at": cr["at"], "items": list[:n]}
	}
	if len(mk.tickers) > 0 {
		q, qerr := stockQuotes(mk.tickers)
		if qerr != nil {
			return wm.Fail(qerr)
		}
		d["stocks"] = q
	}
	// The colour desk's reading widgets: what the person reads on their e-readers (the X4 has it itself).
	if s3 {
		r, rerr := readingFor(userID, local)
		if rerr != nil {
			wm.Log("reading: ", rerr.Error())
		} else {
			d["reading"] = r
		}
	}
	// The colour desk's Claude widget (src/wid/claude.cpp, 2026-11-21): the Claude usage the person's EspOS Desktop
	// (their own Mac, same account) last sent, from desk_state. Only while the desktop feature is on.
	if s3 && wm.Str(in["desk_on"]) == "1" {
		c, cerr := claudeFor(userID, now, loc)
		if cerr != nil {
			wm.Log("claude: ", cerr.Error())
		} else if c != nil {
			d["claude"] = c
		}
	}
	// Stamped last: the device sets its clock from it, so the time spent building must not count.
	d["now_ms"] = time.Now().UnixMilli()
	return map[string]any{"dashboard": d}
}

// readingFor: what the person reads on their EspOS e-readers, for the colour desk's widgets (s3desk/src/wid/read.cpp):
// today's seconds, the goal (read.goal_min of their readers, 30), the streak and the best one (days in a row at the
// goal, as the web's Stats page counts them), the last 7 days' seconds (oldest first) and the 8 books read lately (title,
// author, percent when known, the last day read), the one being read first.
func readingFor(userID int, local time.Time) (map[string]any, error) {
	devs, err := wm.QueryOpts("devices", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": userID}, {"column": "revoked", "op": "eq", "value": false}},
		"order_by": "id", "limit": 20})
	if err != nil {
		return nil, err
	}
	goal := 30
	ids := []any{}
	for _, dv := range devs {
		ids = append(ids, wm.Int(dv["id"]))
	}
	if len(ids) > 0 {
		set, serr := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
			{"column": "device_id", "op": "in", "value": ids}, {"column": "skey", "op": "eq", "value": "read.goal_min"}},
			"order_by": "updated_ms", "limit": 50})
		if serr != nil {
			return nil, serr
		}
		for _, st := range set { // ascending: the last one wins
			if n, e := strconv.Atoi(wm.Str(st["svalue"])); e == nil && n >= 5 && n <= 240 {
				goal = n
			}
		}
	}
	const dayFmt = "2006-01-02"
	rows, rerr := wm.QueryOpts("reading_days", map[string]any{"filters": []map[string]any{
		{"column": "user_id", "op": "eq", "value": userID}, {"column": "day", "op": "gte", "value": local.AddDate(0, 0, -400).Format(dayFmt)}},
		"order_by": "day", "limit": 5000})
	if rerr != nil {
		return nil, rerr
	}
	type bookAgg struct {
		title, last string
		id, sec     int
	}
	days := map[string]int{}
	byKey := map[string]*bookAgg{}
	for _, r := range rows {
		day := wm.Str(r["day"])
		days[day] += wm.Int(r["seconds"])
		key := wm.Str(r["book_key"])
		if key == "" {
			continue
		}
		b := byKey[key]
		if b == nil {
			b = &bookAgg{}
			byKey[key] = b
		}
		b.sec += wm.Int(r["seconds"])
		if t := wm.Str(r["title"]); t != "" {
			b.title = t
		}
		if id := wm.Int(r["book_id"]); id > 0 {
			b.id = id
		}
		if day > b.last {
			b.last = day
		}
	}
	week := make([]any, 7)
	for i := 0; i < 7; i++ {
		week[i] = days[local.AddDate(0, 0, i-6).Format(dayFmt)]
	}
	// Streaks: days in a row at or above the goal; today counts once it is reached.
	reached := func(t time.Time) bool { return days[t.Format(dayFmt)] >= goal*60 }
	streak := 0
	t := local
	if !reached(t) {
		t = t.AddDate(0, 0, -1)
	}
	for reached(t) {
		streak++
		t = t.AddDate(0, 0, -1)
	}
	best, run := 0, 0
	for dd := local.AddDate(0, 0, -400); !dd.After(local); dd = dd.AddDate(0, 0, 1) {
		if reached(dd) {
			run++
			if run > best {
				best = run
			}
		} else {
			run = 0
		}
	}
	list := make([]*bookAgg, 0, len(byKey))
	for _, b := range byKey {
		list = append(list, b)
	}
	sort.Slice(list, func(i, j int) bool {
		if list[i].last != list[j].last {
			return list[i].last > list[j].last
		}
		return list[i].sec > list[j].sec
	})
	// Author and progress of EspOS books (a book from the reader's own card has neither).
	type bookRow struct {
		title, author string
		progress      any
	}
	known := map[int]bookRow{}
	for _, b := range list {
		if b.id > 0 {
			brows, berr := wm.QueryOpts("books", map[string]any{"filters": []map[string]any{
				{"column": "user_id", "op": "eq", "value": userID}}, "limit": 500})
			if berr != nil {
				return nil, berr
			}
			for _, br := range brows {
				known[wm.Int(br["id"])] = bookRow{wm.Str(br["title"]), wm.Str(br["author"]), br["progress"]}
			}
			break
		}
	}
	books := []any{}
	for _, b := range list {
		if len(books) == 8 {
			break
		}
		k, ok := known[b.id]
		title := b.title
		if title == "" && ok {
			title = k.title
		}
		if title == "" {
			continue
		}
		item := map[string]any{"t": clip(title, 120), "d": b.last}
		if ok && k.author != "" {
			item["a"] = clip(k.author, 80)
		}
		if ok && k.progress != nil {
			if p := wm.Int(k.progress); p >= 0 && p <= 100 {
				item["p"] = p
			}
		}
		books = append(books, item)
	}
	return map[string]any{"today": days[local.Format(dayFmt)], "goal": goal, "streak": streak, "best": best, "week": week, "books": books}, nil
}

func orDefault(v, def string) string {
	if v == "" {
		return def
	}
	return v
}

func cut(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return string(r[:n-1]) + "…"
}

// clip keeps at most n characters, without an ellipsis (the colour desk wraps or shrinks text instead).
func clip(s string, n int) string {
	r := []rune(strings.TrimSpace(s))
	if len(r) <= n {
		return string(r)
	}
	return strings.TrimSpace(string(r[:n]))
}

func loadSettings(deviceID int) (map[string]string, error) {
	rows, err := wm.QueryOpts("device_settings", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}},
		"limit":   200,
	})
	if err != nil {
		return nil, err
	}
	st := map[string]string{}
	for _, r := range rows {
		st[wm.Str(r["skey"])] = wm.Str(r["svalue"])
	}
	return st, nil
}

func hhmm(s string) int {
	p := strings.Split(s, ":")
	if len(p) != 2 {
		return -1
	}
	h, e1 := strconv.Atoi(p[0])
	m, e2 := strconv.Atoi(p[1])
	if e1 != nil || e2 != nil {
		return -1
	}
	return h*60 + m
}

// seconds until the next data refresh, skipping the quiet window
func nextRefresh(st map[string]string, local time.Time) int {
	iv, err := strconv.Atoi(st["dash.interval_min"])
	if err != nil || iv <= 0 {
		iv = 30
	}
	next := iv * 60
	qf := hhmm(orDefault(st["dash.quiet_from"], "23:00"))
	qt := hhmm(orDefault(st["dash.quiet_to"], "06:00"))
	if qf < 0 || qt < 0 || qf == qt {
		return next
	}
	at := (local.Hour()*60 + local.Minute() + iv) % 1440
	inQuiet := func(m int) bool {
		if qf < qt {
			return m >= qf && m < qt
		}
		return m >= qf || m < qt
	}
	if inQuiet(at) {
		cur := local.Hour()*60 + local.Minute()
		wait := (qt - cur + 1440) % 1440
		if wait == 0 {
			wait = 1440
		}
		return wait * 60
	}
	return next
}

// ---------- weather (Open-Meteo, no key) ----------

func wmo(code int, day bool) (string, string) {
	switch {
	case code == 0:
		if day {
			return "sun", "Trời quang"
		}
		return "moon", "Trời quang"
	case code == 1:
		if day {
			return "cloudsun", "Ít mây"
		}
		return "moon", "Ít mây"
	case code == 2:
		if day {
			return "cloudsun", "Mây rải rác"
		}
		return "cloud", "Mây rải rác"
	case code == 3:
		return "cloud", "Nhiều mây"
	case code == 45 || code == 48:
		return "fog", "Sương mù"
	case code >= 51 && code <= 57:
		return "rain", "Mưa phùn"
	case code == 61:
		return "rain", "Mưa nhỏ"
	case code == 63:
		return "rain", "Mưa vừa"
	case code == 65 || code == 66 || code == 67:
		return "rain", "Mưa to"
	case code >= 71 && code <= 77, code == 85, code == 86:
		return "snow", "Tuyết"
	case code >= 80 && code <= 82:
		return "rain", "Mưa rào"
	case code >= 95:
		return "storm", "Dông"
	}
	return "cloud", "Nhiều mây"
}

// One forecast call per place and 25 minutes, shared by every board: the X4 reads a few fields of
// it, the colour desk's Weather app the rest (humidity, feels-like, wind, UV, sun, rain chances, 6 days).
const forecastFields = "&current=temperature_2m,weather_code,is_day,relative_humidity_2m,apparent_temperature,wind_speed_10m" +
	"&hourly=temperature_2m,weather_code,is_day,precipitation_probability" +
	"&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max,sunrise,sunset"

// fullForecast: a cached answer holds the fields the colour desk reads (older ones did not).
func fullForecast(body map[string]any) bool {
	h, _ := body["hourly"].(map[string]any)
	dl, _ := body["daily"].(map[string]any)
	c, _ := body["current"].(map[string]any)
	if h == nil || dl == nil || c == nil {
		return false
	}
	_, a := h["precipitation_probability"]
	_, b := dl["sunrise"]
	_, e := c["apparent_temperature"]
	return a && b && e
}

func weather(st map[string]string, now time.Time, local time.Time, s3 bool, lang string) (map[string]any, error) {
	lat := orDefault(st["dash.lat"], "21.0245")
	lon := orDefault(st["dash.lon"], "105.8412")
	city := orDefault(st["dash.city"], tr(lang, "Hà Nội"))
	_, offSec := local.Zone()
	zone := tzName(offSec / 60)
	locKey := lat + "," + lon + "|" + zone
	var body map[string]any
	cached, err := wm.QueryOpts("weather_cache", map[string]any{
		"filters": []map[string]any{{"column": "loc", "op": "eq", "value": locKey}},
		"limit":   1,
	})
	if err != nil {
		return nil, err
	}
	if len(cached) == 1 && now.UnixMilli()-int64(wm.Num(cached[0]["fetched_ms"])) < 25*60*1000 {
		if b, ok := cached[0]["data"].(map[string]any); ok && fullForecast(b) {
			body = b
		}
	}
	if body == nil {
		u := "https://api.open-meteo.com/v1/forecast?latitude=" + url.QueryEscape(lat) + "&longitude=" + url.QueryEscape(lon) +
			forecastFields + "&timezone=" + url.QueryEscape(zone) + "&forecast_days=6"
		res, ferr := wm.Fetch("GET", u, nil)
		if ferr != nil {
			return nil, ferr
		}
		if wm.Int(res["status"]) != 200 {
			return nil, fmt.Errorf("open-meteo status %d", wm.Int(res["status"]))
		}
		body, _ = res["body"].(map[string]any)
		if body == nil {
			return nil, fmt.Errorf("open-meteo: no body")
		}
		if _, uerr := wm.Upsert("weather_cache", []string{"loc"}, map[string]any{"loc": locKey, "data": body, "fetched_ms": now.UnixMilli()}, nil); uerr != nil {
			return nil, uerr
		}
	}
	cur, _ := body["current"].(map[string]any)
	daily, _ := body["daily"].(map[string]any)
	hourly, _ := body["hourly"].(map[string]any)
	icon, desc := wmo(wm.Int(cur["weather_code"]), wm.Int(cur["is_day"]) == 1)
	w := map[string]any{
		"city": city,
		"temp": int(math.Round(wm.Num(cur["temperature_2m"]))),
		"desc": tr(lang, desc),
		"icon": icon,
	}
	if v := cur["relative_humidity_2m"]; v != nil {
		w["_hum"] = roundOf(v) // kept only for the EspOS Bot (build removes it for the others)
	}
	if daily != nil {
		w["max"] = int(math.Round(first(daily["temperature_2m_max"])))
		w["min"] = int(math.Round(first(daily["temperature_2m_min"])))
		w["rain"] = int(math.Round(first(daily["precipitation_probability_max"])))
	}
	hours := []any{}
	if hourly != nil {
		times, _ := hourly["time"].([]any)
		temps, _ := hourly["temperature_2m"].([]any)
		codes, _ := hourly["weather_code"].([]any)
		days, _ := hourly["is_day"].([]any)
		startH := local.Hour() + 3 - (local.Hour() % 3)
		for i := range times {
			ts, perr := time.ParseInLocation("2006-01-02T15:04", wm.Str(times[i]), local.Location())
			if perr != nil || ts.Before(local.Truncate(time.Hour)) {
				continue
			}
			diff := int(ts.Sub(time.Date(local.Year(), local.Month(), local.Day(), 0, 0, 0, 0, local.Location())).Hours())
			if diff < startH || (diff-startH)%3 != 0 {
				continue
			}
			if i >= len(temps) || i >= len(codes) || i >= len(days) {
				break
			}
			ic, _ := wmo(wm.Int(codes[i]), wm.Int(days[i]) == 1)
			hours = append(hours, map[string]any{"h": fmt.Sprintf("%dh", ts.Hour()), "t": int(math.Round(wm.Num(temps[i]))), "icon": ic})
			if len(hours) == 4 {
				break
			}
		}
	}
	w["hours"] = hours
	// The next three days (for the weather widget's "3 ngày" style).
	days := []any{}
	if daily != nil {
		dt, _ := daily["time"].([]any)
		maxs, _ := daily["temperature_2m_max"].([]any)
		mins, _ := daily["temperature_2m_min"].([]any)
		codes, _ := daily["weather_code"].([]any)
		for i := 1; i < len(dt) && i < len(maxs) && i < len(mins) && i < len(codes) && len(days) < 3; i++ {
			ts, perr := time.Parse("2006-01-02", wm.Str(dt[i]))
			if perr != nil {
				continue
			}
			ic, _ := wmo(wm.Int(codes[i]), true)
			days = append(days, map[string]any{"d": tr(lang, shortDays[int(ts.Weekday())]), "min": int(math.Round(wm.Num(mins[i]))),
				"max": int(math.Round(wm.Num(maxs[i]))), "icon": ic})
		}
	}
	w["days"] = days
	if s3 {
		s3Weather(w, body, local, lang)
		// The air quality is a second, optional call: never worth failing (or slowing) the dashboard.
		if time.Since(now) < 8*time.Second {
			if aqi, ok := airQuality(lat, lon, zone, now); ok {
				w["aqi"] = aqi
			}
		}
	}
	return w, nil
}

var shortDays = []string{"CN", "T2", "T3", "T4", "T5", "T6", "T7"}

func roundOf(v any) int { return int(math.Round(wm.Num(v))) }

// at is the i-th value of an Open-Meteo series (nil when missing).
func at(series any, i int) any {
	a, _ := series.([]any)
	if i < 0 || i >= len(a) {
		return nil
	}
	return a[i]
}

// clockOf turns "2026-11-16T06:05" into "06:05".
func clockOf(v any) string {
	s := wm.Str(v)
	if i := strings.Index(s, "T"); i >= 0 && len(s) >= i+6 {
		return s[i+1 : i+6]
	}
	return ""
}

// s3Weather adds the colour desk's Weather app data: now (humidity, feels-like, wind), today
// (UV, sunrise, sunset), 8 steps of 3 hours from the current hour and 6 days from today.
// Its "hours" and "days" replace the X4-sized ones.
func s3Weather(w map[string]any, body map[string]any, local time.Time, lang string) {
	cur, _ := body["current"].(map[string]any)
	daily, _ := body["daily"].(map[string]any)
	hourly, _ := body["hourly"].(map[string]any)
	loc := local.Location()
	if cur != nil {
		if v := cur["relative_humidity_2m"]; v != nil {
			w["hum"] = roundOf(v)
		}
		if v := cur["apparent_temperature"]; v != nil {
			w["feels"] = roundOf(v)
		}
		if v := cur["wind_speed_10m"]; v != nil {
			w["wind"] = roundOf(v)
		}
	}
	hours := []any{}
	if hourly != nil {
		times, _ := hourly["time"].([]any)
		idx := map[string]int{}
		for i, t := range times {
			idx[wm.Str(t)] = i
		}
		start := time.Date(local.Year(), local.Month(), local.Day(), local.Hour(), 0, 0, 0, loc)
		for k := 0; k < 8; k++ {
			ts := start.Add(time.Duration(3*k) * time.Hour)
			i, ok := idx[ts.Format("2006-01-02T15:04")]
			if !ok || at(hourly["temperature_2m"], i) == nil {
				continue
			}
			ic, _ := wmo(wm.Int(at(hourly["weather_code"], i)), wm.Int(at(hourly["is_day"], i)) == 1)
			hours = append(hours, map[string]any{"h": fmt.Sprintf("%dh", ts.Hour()), "t": roundOf(at(hourly["temperature_2m"], i)),
				"icon": ic, "rain": roundOf(at(hourly["precipitation_probability"], i))})
		}
	}
	w["hours"] = hours
	days := []any{}
	if daily != nil {
		dt, _ := daily["time"].([]any)
		today := local.Format("2006-01-02")
		for i := range dt {
			ds := wm.Str(dt[i])
			if ds < today || at(daily["temperature_2m_max"], i) == nil || len(days) == 6 {
				continue // a cached answer from yesterday still starts with yesterday
			}
			ts, perr := time.Parse("2006-01-02", ds)
			if perr != nil {
				continue
			}
			label := tr(lang, shortDays[int(ts.Weekday())])
			if ds == today {
				label = tr(lang, "Hôm nay")
				w["uv"] = roundOf(at(daily["uv_index_max"], i))
				w["sunrise"] = clockOf(at(daily["sunrise"], i))
				w["sunset"] = clockOf(at(daily["sunset"], i))
			}
			ic, _ := wmo(wm.Int(at(daily["weather_code"], i)), true)
			days = append(days, map[string]any{"d": label, "date": ts.Format("02/01"),
				"min": roundOf(at(daily["temperature_2m_min"], i)), "max": roundOf(at(daily["temperature_2m_max"], i)),
				"icon": ic, "rain": roundOf(at(daily["precipitation_probability_max"], i))})
		}
	}
	w["days"] = days
}

// airQuality is the current US AQI at the place, cached like the forecast (a failure for 10 minutes).
func airQuality(lat, lon, zone string, now time.Time) (int, bool) {
	key := "aqi|" + lat + "," + lon
	rows, err := wm.QueryOpts("weather_cache", map[string]any{
		"filters": []map[string]any{{"column": "loc", "op": "eq", "value": key}}, "limit": 1})
	if err != nil {
		wm.Log("aqi cache: ", err.Error())
		return 0, false
	}
	if len(rows) == 1 {
		d, _ := rows[0]["data"].(map[string]any)
		age := now.UnixMilli() - int64(wm.Num(rows[0]["fetched_ms"]))
		good := d != nil && d["ok"] == true
		if (good && age < 25*60*1000) || (!good && age < 10*60*1000) {
			return wm.Int(d["aqi"]), good
		}
	}
	rec := map[string]any{"ok": false}
	u := "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + url.QueryEscape(lat) + "&longitude=" + url.QueryEscape(lon) +
		"&current=us_aqi&timezone=" + url.QueryEscape(zone)
	res, ferr := wm.Fetch("GET", u, nil)
	if ferr != nil {
		wm.Log("aqi: ", ferr.Error())
	} else if wm.Int(res["status"]) == 200 {
		body, _ := res["body"].(map[string]any)
		cur, _ := body["current"].(map[string]any)
		if cur != nil && cur["us_aqi"] != nil {
			rec = map[string]any{"ok": true, "aqi": roundOf(cur["us_aqi"])}
		}
	}
	if _, uerr := wm.Upsert("weather_cache", []string{"loc"}, map[string]any{"loc": key, "data": rec, "fetched_ms": now.UnixMilli()}, nil); uerr != nil {
		wm.Log("aqi cache: ", uerr.Error())
	}
	return wm.Int(rec["aqi"]), rec["ok"] == true
}

// ---------- markets (gold, stocks, exchange rates) ----------

type needs struct {
	news    bool
	gold    []string
	fx      []string
	tickers []string
	coins   []string
}

// The codes a widget asks for (its option), or `def`, de-duplicated across widgets into `into`.
func addCodes(into *[]string, seen map[string]bool, prefix string, it []any, def string, max int) {
	opt := ""
	if len(it) > 6 {
		opt = wm.Str(it[6])
	}
	if strings.TrimSpace(opt) == "" {
		opt = def
	}
	addList(into, seen, prefix, opt, max)
}

// addList adds the comma-separated codes not seen yet, up to max in all.
func addList(into *[]string, seen map[string]bool, prefix, opt string, max int) {
	for _, c := range strings.Split(opt, ",") {
		c = strings.ToUpper(strings.TrimSpace(c))
		if c == "" || len(c) > 10 || seen[prefix+c] || len(*into) >= max {
			continue
		}
		seen[prefix+c] = true
		*into = append(*into, c)
	}
}

// The bot's default standby rotation: the same as botDefaults["bot.screens"] in device_sync (scripts/bot-keys.go.txt).
const botScreensDefault = `[{"id":"clock_bold","s":20},{"id":"date_page","s":8},{"id":"weather","s":8},{"id":"eyes","s":20},{"id":"aquarium","s":15}]`

var indexNames = map[string]bool{"VNINDEX": true, "VN30": true, "HNX": true, "UPCOM": true}

// Which market widgets this device's layouts hold, and the stock symbols they ask for.
// The colour desk's built-in pages (widgets/... in its firmware), used while a page has no
// layout saved: only their market widgets matter here.
var s3Builtin = map[string]string{
	"s3.p2": `{"v":1,"i":[["gold",0,0,2,2,"","SJC,N24K"],["stocks",2,0,2,2,"","VNINDEX,VN30,HNX"],["news",0,5,4,2,"",""]]}`,
	"s3.l1": `{"v":1,"i":[["gold",4,2,2,2,"","SJC,N24K"]]}`,
	"s3.l2": `{"v":1,"i":[["stocks",3,0,3,2,"","VNINDEX,VN30,HNX"],["news",3,2,3,2,"",""]]}`,
}

func marketNeeds(st map[string]string, kind string) needs {
	if kind == "s3" {
		for k, v := range s3Builtin {
			if st[k] == "" {
				st = copyWith(st, k, v)
			}
		}
	}
	var n needs
	seen := map[string]bool{}
	if kind == "bot" {
		// The EspOS Bot's standby screens (bot.screens, else its default): "gold" shows SJC and the USD rate,
		// "newspaper" a headline. Nothing else of the markets.
		scr := st["bot.screens"]
		if scr == "" {
			scr = botScreensDefault
		}
		var list []struct {
			ID string `json:"id"`
		}
		_ = json.Unmarshal([]byte(scr), &list)
		for _, it := range list {
			switch it.ID {
			case "gold":
				addList(&n.gold, seen, "gold:", "SJC", 1)
				addList(&n.fx, seen, "fx:", "USD", 1)
			case "newspaper":
				n.news = true
			}
		}
		return n
	}
	for _, key := range []string{"home.w", "sleep.wl", "sleep.wp", "s3.p1", "s3.p2", "s3.l1", "s3.l2"} {
		if st[key] == "" {
			continue
		}
		var doc struct {
			I [][]any `json:"i"`
		}
		if json.Unmarshal([]byte(st[key]), &doc) != nil {
			continue
		}
		for _, it := range doc.I {
			if len(it) == 0 {
				continue
			}
			switch wm.Str(it[0]) {
			case "gold":
				addCodes(&n.gold, seen, "gold:", it, "SJC,N24K", 8)
			case "fx":
				addCodes(&n.fx, seen, "fx:", it, "USD,EUR,JPY,CNY", 10)
			case "news":
				n.news = true
			case "crypto":
				addCodes(&n.coins, seen, "coin:", it, "BTC,ETH", 6)
			case "stocks":
				addCodes(&n.tickers, seen, "stk:", it, "VNINDEX,VN30,HNX", 8)
			}
		}
	}
	if kind == "s3" {
		// What its market widgets pick (home pages and saver pages: "gold:SJC,stocks:FPT,..."), first.
		for _, opt := range s3MarketOpts(st) {
			for _, tok := range strings.Split(opt, ",") {
				k, c, ok := strings.Cut(strings.TrimSpace(tok), ":")
				if !ok {
					continue
				}
				switch k {
				case "gold":
					addList(&n.gold, seen, "gold:", c, 16)
				case "stocks":
					addList(&n.tickers, seen, "stk:", c, 24)
				case "fx":
					addList(&n.fx, seen, "fx:", c, 20)
				case "crypto":
					addList(&n.coins, seen, "coin:", c, 12)
				}
			}
		}
		// The colour desk's Markets and News apps: always, after what its widgets pick.
		n.news = true
		addList(&n.gold, seen, "gold:", "SJC,N24K", 18)
		addList(&n.tickers, seen, "stk:", "VNINDEX,VN30,HNX,UPCOM", 28)
		addList(&n.fx, seen, "fx:", "USD,EUR,JPY,CNY,GBP,AUD,SGD,KRW,THB", 29)
		addList(&n.coins, seen, "coin:", "BTC,ETH,BNB,SOL", 16)
	}
	return n
}

// The options of the colour desk's market widgets: on its home pages (s3.home v2 and the landscape s3.home_l v1:
// ["w", kind, x, y, size, style, opt, open]) and on its saver pages (s3.saver_pages: {t:"w", k, op}). An empty
// option is the firmware's built-in mix, which the always-lists cover.
func s3MarketOpts(st map[string]string) []string {
	out := s3WidgetOpts(st, "markets")
	// The one-market widgets (gold, stocks, fx, crypto) hold the same "kind:code" tokens.
	for _, k := range []string{"gold", "stocks", "fx", "crypto"} {
		out = append(out, s3WidgetOpts(st, k)...)
	}
	// The Markets app's own list (s3.markets, set on the web or the device), after the widgets' picks.
	if v := strings.TrimSpace(st["s3.markets"]); v != "" {
		out = append(out, v)
	}
	return out
}

// The non-empty options of the colour desk's widgets of one kind, on its home pages (portrait s3.home and
// landscape s3.home_l: the same entries) and its saver pages.
func s3WidgetOpts(st map[string]string, kind string) []string {
	out := []string{}
	for _, key := range []string{"s3.home", "s3.home_l"} {
		s := st[key]
		if s == "" {
			continue
		}
		var h struct {
			Pages [][][]any `json:"pages"`
		}
		if json.Unmarshal([]byte(s), &h) == nil {
			for _, p := range h.Pages {
				for _, it := range p {
					if len(it) > 6 && wm.Str(it[0]) == "w" && wm.Str(it[1]) == kind && wm.Str(it[6]) != "" {
						out = append(out, wm.Str(it[6]))
					}
				}
			}
		}
	}
	if s := st["s3.saver_pages"]; s != "" {
		type page struct {
			E []map[string]any `json:"e"`
		}
		var d struct {
			P  []page `json:"p"`
			L  []page `json:"l"`
			Np *page  `json:"np"`
			Nl *page  `json:"nl"`
		}
		if json.Unmarshal([]byte(s), &d) == nil {
			pages := append(append([]page{}, d.P...), d.L...)
			if d.Np != nil {
				pages = append(pages, *d.Np)
			}
			if d.Nl != nil {
				pages = append(pages, *d.Nl)
			}
			for _, p := range pages {
				for _, e := range p.E {
					if wm.Str(e["t"]) == "w" && wm.Str(e["k"]) == kind && wm.Str(e["op"]) != "" {
						out = append(out, wm.Str(e["op"]))
					}
				}
			}
		}
	}
	return out
}

func copyWith(st map[string]string, k, v string) map[string]string {
	out := make(map[string]string, len(st)+1)
	for a, b := range st {
		out[a] = b
	}
	out[k] = v
	return out
}

// The list entries whose `field` is one of `codes`, in the codes' order.
func pick(list any, field string, codes []string) []any {
	byCode := map[string]any{}
	arr, _ := list.([]any)
	for _, it := range arr {
		if m, ok := it.(map[string]any); ok {
			byCode[strings.ToUpper(wm.Str(m[field]))] = m
		}
	}
	out := make([]any, len(codes)) // filled by index (applint -strict)
	n := 0
	for _, c := range codes {
		if v := byCode[c]; v != nil {
			out[n] = v
			n++
		}
	}
	return out[:n]
}

func marketCached(key string) (map[string]any, int64, error) {
	rows, err := wm.QueryOpts("market_cache", map[string]any{"filters": []map[string]any{{"column": "key", "op": "eq", "value": key}}, "limit": 1})
	if err != nil || len(rows) == 0 {
		return nil, 0, err
	}
	d, _ := rows[0]["data"].(map[string]any)
	return d, int64(wm.Num(rows[0]["fetched_ms"])), nil
}

// Quotes for the symbols, in order, from the 15-minute cache (market_tick): never fetched here.
func stockQuotes(tickers []string) (map[string]any, error) {
	idx, _, err := marketCached("index")
	if err != nil {
		return nil, err
	}
	stk, _, err := marketCached("stk")
	if err != nil {
		return nil, err
	}
	idxItems, _ := idx["items"].(map[string]any)
	stkItems, _ := stk["items"].(map[string]any)
	out := make([]any, len(tickers)) // filled by index (applint -strict)
	n := 0
	for _, t := range tickers {
		var v any
		if indexNames[t] {
			v = idxItems[t]
		} else {
			v = stkItems[t]
		}
		if v != nil {
			out[n] = v
			n++
		}
	}
	out = out[:n]
	at := wm.Str(idx["at"])
	if s := wm.Str(stk["at"]); s > at {
		at = s
	}
	return map[string]any{"at": at, "items": out}, nil
}

// VnExpress sections (news worker's catalogue): slug → the name shown.
var sectionNames = map[string]string{
	"tin-moi-nhat": "Tin mới nhất", "tin-noi-bat": "Tin nổi bật", "thoi-su": "Thời sự", "the-gioi": "Thế giới",
	"kinh-doanh": "Kinh doanh", "bat-dong-san": "Bất động sản", "khoa-hoc-cong-nghe": "Khoa học công nghệ",
	"suc-khoe": "Sức khỏe", "giao-duc": "Giáo dục", "phap-luat": "Pháp luật", "gia-dinh": "Đời sống",
	"du-lich": "Du lịch", "giai-tri": "Giải trí", "the-thao": "Thể thao", "oto-xe-may": "Xe", "y-kien": "Ý kiến",
}

// The newest headlines of the reader's news sections (from the shared 15-minute news cache).
// The colour desk also gets up to 12 stories with their cache id, section, time and picture
// flag (its News app), and the sections the reader follows; plus the sections its news widgets
// pick (`picked`, their slugs), with a few stories of each.
func newsTop(userID int, s3 bool, picked []string, lang string) (map[string]any, error) {
	sections := []string{"tin-moi-nhat", "thoi-su", "the-gioi"}
	if s3 {
		// The web's (and the news worker's) default choice.
		sections = []string{"tin-moi-nhat", "thoi-su", "the-gioi", "kinh-doanh", "khoa-hoc-cong-nghe", "suc-khoe"}
	}
	prefs, err := wm.QueryOpts("news_prefs", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}}, "limit": 1})
	if err != nil {
		return nil, err
	}
	if len(prefs) == 1 && strings.TrimSpace(wm.Str(prefs[0]["sections"])) != "" {
		sections = strings.Split(wm.Str(prefs[0]["sections"]), ",")
	}
	if s3 {
		return newsS3(sections, picked, lang)
	}
	rows, err := wm.QueryOpts("news_cache", map[string]any{"limit": 40})
	if err != nil {
		return nil, err
	}
	bySlug := map[string][]any{}
	for _, r := range rows {
		list, _ := r["items"].([]any)
		bySlug[wm.Str(r["slug"])] = list
	}
	top := []any{}
	full := []any{}  // the same headlines uncut, for the EspOS Bot
	items := []any{} // with the lead, for the colour desk (tap a headline to read it)
	// The first story of each section, then the second ones, up to three headlines.
	for depth := 0; depth < 3 && len(top) < 3; depth++ {
		for _, s := range sections {
			list := bySlug[strings.TrimSpace(s)]
			if depth < len(list) && len(top) < 3 {
				if m, ok := list[depth].(map[string]any); ok && wm.Str(m["title"]) != "" {
					top = append(top, cut(wm.Str(m["title"]), 120))
					full = append(full, strings.TrimSpace(wm.Str(m["title"])))
					items = append(items, map[string]any{"title": cut(wm.Str(m["title"]), 120), "lead": cut(wm.Str(m["lead"]), 400)})
				}
			}
		}
	}
	return map[string]any{"top": top, "items": items, "full": full}, nil
}

// newsS3: the first story of each followed section, then the second ones…: 3 titles in `top`,
// up to 12 distinct stories in `items`; then, for each section a news widget picks, its first
// stories until it has 4 (up to 24 stories in all). `sections` lists both, followed ones first.
func newsS3(sections []string, picked []string, lang string) (map[string]any, error) {
	rows, err := wm.QueryOpts("news_cache", map[string]any{"limit": 40})
	if err != nil {
		return nil, err
	}
	bySlug := map[string][]any{}
	for _, r := range rows {
		list, _ := r["items"].([]any)
		bySlug[wm.Str(r["slug"])] = list
	}
	followed := []any{}
	slugs := []string{}
	for _, s := range sections {
		s = strings.TrimSpace(s)
		if name, ok := sectionNames[s]; ok {
			followed = append(followed, map[string]any{"slug": s, "name": tr(lang, name)})
			slugs = append(slugs, s)
		}
	}
	top := []any{}
	items := []any{}
	seen := map[string]bool{}
	soon := time.Now().Add(2 * time.Hour).UnixMilli()
	// One story of a section, once.
	add := func(s string, m map[string]any) bool {
		if wm.Str(m["title"]) == "" || seen[wm.Str(m["id"])] {
			return false
		}
		seen[wm.Str(m["id"])] = true
		if len(top) < 3 {
			top = append(top, clip(wm.Str(m["title"]), 120))
		}
		it := map[string]any{"id": wm.Str(m["id"]), "title": clip(wm.Str(m["title"]), 120),
			"lead": clip(wm.Str(m["lead"]), 400), "sec": tr(lang, sectionNames[s]), "pub_ms": int64(wm.Num(m["pub_ms"])), "img": m["img"] == true}
		// The thumbnail's signed storage address, only while it has 2 hours left (the dashboard is kept that long).
		if u := wm.Str(m["thumb"]); u != "" && int64(wm.Num(m["thumb_exp"])) >= soon {
			it["thumb"] = u
		}
		items = append(items, it)
		return true
	}
	for depth := 0; depth < 12 && len(items) < 12; depth++ {
		for _, s := range slugs {
			list := bySlug[s]
			if depth >= len(list) || len(items) == 12 {
				continue
			}
			if m, ok := list[depth].(map[string]any); ok {
				add(s, m)
			}
		}
	}
	// The news widgets' sections: listed, and 4 stories each (those already in count).
	listed := map[string]bool{}
	for _, s := range slugs {
		listed[s] = true
	}
	for _, p := range picked {
		s := strings.TrimSpace(p)
		name, ok := sectionNames[s]
		if !ok {
			continue
		}
		name = tr(lang, name)
		if !listed[s] {
			listed[s] = true
			followed = append(followed, map[string]any{"slug": s, "name": name})
		}
		have := 0
		for _, it := range items {
			if wm.Str(it.(map[string]any)["sec"]) == name {
				have++
			}
		}
		for _, v := range bySlug[s] {
			if have >= 4 || len(items) >= 24 {
				break
			}
			if m, ok := v.(map[string]any); ok && add(s, m) {
				have++
			}
		}
	}
	return map[string]any{"top": top, "items": items, "sections": followed}, nil
}

func first(v any) float64 {
	a, _ := v.([]any)
	if len(a) == 0 {
		return 0
	}
	return wm.Num(a[0])
}

// ---------- calendar (secret iCal address) ----------

type event struct {
	min   int
	time  string
	title string
}

// icsLinks splits dash.ics_url: up to 5 links (Google, iCloud, Outlook… any iCal feed),
// separated by spaces or new lines; webcal:// is the same feed over https.
func icsLinks(v string) []string {
	var out []string
	for _, u := range strings.Fields(v) {
		if strings.HasPrefix(u, "webcal://") {
			u = "https://" + strings.TrimPrefix(u, "webcal://")
		}
		if strings.HasPrefix(u, "https://") && len(out) < 5 {
			out = append(out, u)
		}
	}
	return out
}

// calendar merges today's events from every linked calendar (cached 15 minutes per device).
// For the colour desk it also lists today and the next 6 days (its Calendar app); its cache row
// then holds {"events": [...], "agenda": [...]} instead of today's list.
func calendar(deviceID int, icsURL string, now time.Time, local time.Time, s3 bool) ([]any, []any, error) {
	day := local.Format("2006-01-02")
	cached, err := wm.QueryOpts("ics_cache", map[string]any{
		"filters": []map[string]any{{"column": "device_id", "op": "eq", "value": deviceID}},
		"limit":   1,
	})
	if err != nil {
		return nil, nil, err
	}
	if len(cached) == 1 && wm.Str(cached[0]["day"]) == day && now.UnixMilli()-int64(wm.Num(cached[0]["fetched_ms"])) < 15*60*1000 {
		if list, ok := cached[0]["data"].([]any); ok && !s3 {
			return list, nil, nil
		}
		if both, ok := cached[0]["data"].(map[string]any); ok {
			ev, okE := both["events"].([]any)
			ag, okA := both["agenda"].([]any)
			if okE && (okA || !s3) {
				return ev, ag, nil
			}
		}
	}
	links := icsLinks(icsURL)
	if len(links) == 0 {
		return nil, nil, fmt.Errorf("ics url must be https or webcal")
	}
	var evs []event
	var all []vevent
	var lastErr error
	got := 0
	for _, u := range links {
		res, ferr := wm.Fetch("GET", u, nil)
		if ferr != nil {
			lastErr = ferr
			continue
		}
		if wm.Int(res["status"]) != 200 {
			lastErr = fmt.Errorf("ics status %d", wm.Int(res["status"]))
			continue
		}
		got++
		vs := parseVEvents(wm.Str(res["body"]))
		evs = append(evs, eventsOn(vs, local)...)
		if s3 {
			all = append(all, vs...)
		}
	}
	if got == 0 {
		return nil, nil, lastErr
	}
	sort.SliceStable(evs, func(i, j int) bool { return evs[i].min < evs[j].min })
	out := []any{}
	for _, e := range evs {
		out = append(out, map[string]any{"time": e.time, "title": cut(e.title, 60)})
		if len(out) == 6 {
			break
		}
	}
	var data any = out
	var agenda []any
	if s3 {
		agenda = agendaOf(all, local)
		data = map[string]any{"events": out, "agenda": agenda}
	}
	if _, uerr := wm.Upsert("ics_cache", []string{"device_id"}, map[string]any{"device_id": deviceID, "day": day, "data": data, "fetched_ms": now.UnixMilli()}, nil); uerr != nil {
		return nil, nil, uerr
	}
	return out, agenda, nil
}

// agendaOf lists the events of today and the next 6 days, by day then time (all-day first),
// at most 30: {date "YYYY-MM-DD", time "HH:MM" or "" for all day, title}.
func agendaOf(vs []vevent, local time.Time) []any {
	loc := local.Location()
	out := []any{}
	for k := 0; k < 7 && len(out) < 30; k++ {
		day := time.Date(local.Year(), local.Month(), local.Day()+k, 0, 0, 0, 0, loc)
		for _, e := range eventsOnDay(vs, day, loc) {
			t := e.time
			if e.min < 0 {
				t = ""
			}
			out = append(out, map[string]any{"date": day.Format("2006-01-02"), "time": t, "title": clip(e.title, 80)})
			if len(out) == 30 {
				break
			}
		}
	}
	return out
}

// icsTime reads a DTSTART-style value; floating and TZID times are taken as Vietnam local time.
func icsTime(params, val string, loc *time.Location) (time.Time, bool, bool) {
	val = strings.TrimSpace(val)
	if strings.Contains(params, "VALUE=DATE") || len(val) == 8 {
		t, err := time.ParseInLocation("20060102", val[:minInt(8, len(val))], loc)
		return t, true, err == nil
	}
	if strings.HasSuffix(val, "Z") {
		t, err := time.Parse("20060102T150405Z", val)
		return t.In(loc), false, err == nil
	}
	t, err := time.ParseInLocation("20060102T150405", val, loc)
	return t, false, err == nil
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// vevent is one VEVENT of a feed: its properties (name → [params, value]) and EXDATE days.
type vevent struct {
	props   map[string][2]string
	exdates []string
}

// eventsOn: today's events of a feed, by time.
func eventsOn(vs []vevent, local time.Time) []event {
	loc := local.Location()
	return eventsOnDay(vs, time.Date(local.Year(), local.Month(), local.Day(), 0, 0, 0, 0, loc), loc)
}

// eventsOnDay: the events of a feed that happen on one day (local midnight), by time.
func eventsOnDay(vs []vevent, day time.Time, loc *time.Location) []event {
	var out []event
	for _, v := range vs {
		if e, ok := occursToday(v.props, v.exdates, day, loc); ok {
			out = append(out, e)
		}
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].min < out[j].min })
	return out
}

func parseVEvents(raw string) []vevent {
	raw = strings.ReplaceAll(raw, "\r\n", "\n")
	raw = strings.ReplaceAll(raw, "\n ", "")
	raw = strings.ReplaceAll(raw, "\n\t", "")
	var out []vevent
	inEv := false
	var props map[string][2]string
	var exdates []string
	for _, line := range strings.Split(raw, "\n") {
		switch {
		case line == "BEGIN:VEVENT":
			inEv = true
			props = map[string][2]string{}
			exdates = nil
		case line == "END:VEVENT":
			inEv = false
			out = append(out, vevent{props: props, exdates: exdates})
		case inEv:
			idx := strings.Index(line, ":")
			if idx < 0 {
				continue
			}
			left, val := line[:idx], line[idx+1:]
			name := left
			params := ""
			if p := strings.Index(left, ";"); p >= 0 {
				name = left[:p]
				params = left[p+1:]
			}
			if name == "EXDATE" {
				for _, x := range strings.Split(val, ",") {
					if len(x) >= 8 {
						exdates = append(exdates, x[:8])
					}
				}
				continue
			}
			props[name] = [2]string{params, val}
		}
	}
	return out
}

func occursToday(p map[string][2]string, exdates []string, today time.Time, loc *time.Location) (event, bool) {
	ds, ok := p["DTSTART"]
	if !ok {
		return event{}, false
	}
	if p["STATUS"][1] == "CANCELLED" {
		return event{}, false
	}
	start, allDay, good := icsTime(ds[0], ds[1], loc)
	if !good {
		return event{}, false
	}
	title := strings.ReplaceAll(strings.ReplaceAll(p["SUMMARY"][1], "\\,", ","), "\\n", " ")
	if title == "" {
		title = "(Không tiêu đề)"
	}
	sday := time.Date(start.Year(), start.Month(), start.Day(), 0, 0, 0, 0, loc)
	todayKey := today.Format("20060102")
	for _, x := range exdates {
		if x == todayKey {
			return event{}, false
		}
	}
	match := false
	if rr, has := p["RRULE"]; has {
		match = ruleHits(rr[1], sday, today, loc)
	} else if allDay {
		end := sday.AddDate(0, 0, 1)
		if de, hasEnd := p["DTEND"]; hasEnd {
			if t, _, ok2 := icsTime(de[0], de[1], loc); ok2 {
				end = t
			}
		}
		match = !today.Before(sday) && today.Before(end)
	} else {
		match = sday.Equal(today)
	}
	if !match {
		return event{}, false
	}
	if allDay {
		return event{min: -1, time: "Cả ngày", title: title}, true
	}
	return event{min: start.Hour()*60 + start.Minute(), time: start.Format("15:04"), title: title}, true
}

var byday = map[string]time.Weekday{"SU": 0, "MO": 1, "TU": 2, "WE": 3, "TH": 4, "FR": 5, "SA": 6}

func ruleHits(rule string, sday, today time.Time, loc *time.Location) bool {
	if today.Before(sday) {
		return false
	}
	parts := map[string]string{}
	for _, kv := range strings.Split(rule, ";") {
		if i := strings.Index(kv, "="); i > 0 {
			parts[kv[:i]] = kv[i+1:]
		}
	}
	if u := parts["UNTIL"]; len(u) >= 8 {
		if until, err := time.ParseInLocation("20060102", u[:8], loc); err == nil && today.After(until) {
			return false
		}
	}
	interval := 1
	if v, err := strconv.Atoi(parts["INTERVAL"]); err == nil && v > 0 {
		interval = v
	}
	days := int(math.Round(today.Sub(sday).Hours() / 24))
	switch parts["FREQ"] {
	case "DAILY":
		return days%interval == 0
	case "WEEKLY":
		wd := []time.Weekday{sday.Weekday()}
		if b := parts["BYDAY"]; b != "" {
			wd = nil
			for _, x := range strings.Split(b, ",") {
				x = strings.TrimSpace(x)
				if len(x) >= 2 {
					if w, ok := byday[x[len(x)-2:]]; ok {
						wd = append(wd, w)
					}
				}
			}
		}
		hit := false
		for _, w := range wd {
			if w == today.Weekday() {
				hit = true
			}
		}
		weekStart := sday.AddDate(0, 0, -int(sday.Weekday()))
		weeks := int(math.Round(today.Sub(weekStart).Hours()/24)) / 7
		return hit && weeks%interval == 0
	case "MONTHLY":
		months := (today.Year()-sday.Year())*12 + int(today.Month()) - int(sday.Month())
		return today.Day() == sday.Day() && months%interval == 0
	case "YEARLY":
		return today.Day() == sday.Day() && today.Month() == sday.Month() && (today.Year()-sday.Year())%interval == 0
	}
	return false
}

// ---------- notes ----------

// popTitle: a Claude pop-up's title in the device's language (words; the OS translates the buttons).
func popTitle(kind, tool, lang string) string {
	switch {
	case kind == "claude_status":
		return tr(lang, "Claude báo tình hình")
	case kind == "claude_wait":
		return tr(lang, "Claude đang chờ bạn")
	case tool == "Bash":
		return tr(lang, "Claude xin chạy lệnh")
	case tool == "Edit" || tool == "Write" || tool == "MultiEdit" || tool == "NotebookEdit":
		return tr(lang, "Claude xin sửa tệp")
	}
	return tr(lang, "Claude xin quyền")
}

// claudePop: the pop-up card of a Claude note (desktop-protocol §9.6): what it shows, and the buttons the OS posts to the
// app's own /send (it adds the nonce and the device credential). Approve takes a second tap when the request's
// command is risky (desktop_sync marks it from the shared list).
func claudePop(kind string, data map[string]any, lang string) (map[string]any, []any) {
	pop := map[string]any{"head": wm.Str(data["head"]), "title": popTitle(kind, wm.Str(data["tool"]), lang)}
	if t := wm.Str(data["mono"]); t != "" {
		pop["mono"] = t
	}
	if t := plainMD(wm.Str(data["text"])); t != "" {
		pop["text"] = t // Claude writes markdown; the S3 shows plain text (the command in mono stays as typed)
	}
	// what the S3 may speak for it, once (its announcement rules: one per 2 minutes, merged, none at night)
	if head := wm.Str(data["head"]); head != "" && kind != "claude_status" {
		say := strings.ReplaceAll(tr(lang, "{head} đang chờ bạn"), "{head}", head)
		if kind == "claude_perm" {
			say = strings.ReplaceAll(tr(lang, "{head} chờ bạn duyệt"), "{head}", head)
		}
		if r := []rune(say); len(r) > 80 {
			say = string(r[:80])
		}
		pop["say"] = say
	}
	acts := []any{}
	if kind == "claude_perm" && wm.Str(data["req"]) != "" {
		body := func(k string) map[string]any {
			return map[string]any{"mac": wm.Int(data["mac"]), "s": wm.Str(data["s"]), "kind": k, "req": wm.Str(data["req"])}
		}
		acts = append(acts, map[string]any{"id": "deny", "post": "/send", "body": body("deny")},
			map[string]any{"id": "approve", "post": "/send", "body": body("approve"), "risky": data["risky"] == true})
	}
	return pop, acts
}

// macAllows: whether the Mac (desktop id) takes commands from this device, by its allow_from (none = every device of
// the account), as desk_queue checks it (403 sender_not_allowed). The person's desktop states are read on first use;
// an unknown Mac or a read error counts as allowed (desk_queue still decides).
func macAllows(macs *map[int]map[string]any, userID, mac, deviceID int) bool {
	if *macs == nil {
		*macs = map[int]map[string]any{}
		rows, err := wm.QueryOpts("desk_state", map[string]any{"filters": []map[string]any{{"column": "user_id", "op": "eq", "value": userID}}, "limit": 20})
		if err != nil {
			wm.Log("desk_state: ", err.Error())
		}
		for _, r := range rows {
			st, _ := r["state"].(map[string]any)
			if raw, isStr := r["state"].(string); isStr && raw != "" {
				_ = json.Unmarshal([]byte(raw), &st)
			}
			if st != nil {
				(*macs)[wm.Int(r["device_id"])] = st
			}
		}
	}
	st := (*macs)[mac]
	if st == nil {
		return true
	}
	allow, isList := st["allow_from"].([]any)
	if !isList {
		return true
	}
	for _, x := range allow {
		if deviceID > 0 && wm.Int(x) == deviceID {
			return true
		}
	}
	return false
}

// plainMD: markdown as plain text for the S3's cards (Claude's status reports and last lines came out as
// "**Tài khoản:**", `Active`, "- item", 2026-11-23). Marks only, never words: headings, emphasis (** and __),
// inline code, links and images ([x](y) → x), quotes and code fences go; list items become "• ". A single * or _ stays
// (file_names, 2*3).
var (
	mdLink    = regexp.MustCompile(`!?\[([^\]\n]*)\]\([^)\n]*\)`)
	mdHeading = regexp.MustCompile(`^#{1,6}\s+`)
	mdBullet  = regexp.MustCompile(`^(\s*)[-*+]\s+`)
	mdQuote   = regexp.MustCompile(`^>\s?`)
	mdRule    = regexp.MustCompile(`^\s*(?:(?:-\s*){3,}|(?:\*\s*){3,}|(?:_\s*){3,})$`) // RE2: no back-references
)

func plainMD(s string) string {
	if s == "" {
		return s
	}
	lines := strings.Split(strings.ReplaceAll(s, "\r\n", "\n"), "\n")
	out := make([]string, 0, len(lines))
	for _, t := range lines {
		if strings.HasPrefix(strings.TrimSpace(t), "```") || mdRule.MatchString(t) {
			continue // a code fence or a horizontal rule: no words
		}
		t = mdQuote.ReplaceAllString(t, "")
		t = mdHeading.ReplaceAllString(t, "")
		t = mdBullet.ReplaceAllString(t, "$1• ")
		t = mdLink.ReplaceAllString(t, "$1")
		t = strings.NewReplacer("**", "", "__", "", "`", "").Replace(t)
		out = append(out, t)
	}
	return strings.Trim(strings.Join(out, "\n"), "\n") // the words as they were, spaces too
}

// notAllowedHint: where to let this device answer the Mac's requests, in the device's language (">" not "›": the
// S3's built-in fonts are Latin-1).
func notAllowedHint(lang string) string {
	return tr(lang, "Bật máy này trong EspOS Desktop > Cài đặt > Máy ra lệnh")
}

// notes: the newest 2 plain notes of 24 h (what the dashboard always showed), and every live note of an app (a Claude
// permission request, a waiting session, a status report: up to 10), which the S3 turns into that app's
// notifications and pop-up cards (s3desk src/popnote.cpp: app, arg, ref, kind, exp_ms, pop, acts). An app note never
// pushes a plain one out, nor the other way round.
func notes(userID, deviceID int, now time.Time, loc *time.Location, lang string) ([]any, error) {
	rows, err := wm.QueryOpts("notifications", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": userID}, {"column": "created_at", "op": "gte", "value": now.Add(-24 * time.Hour).UTC().Format(time.RFC3339)}},
		"order_by": "created_at", "desc": true, "limit": 40,
	})
	if err != nil {
		return nil, err
	}
	out := []any{}
	plain, apps := 0, 0
	var macs map[int]map[string]any // the person's desktops' states, read once, only for a card with buttons
	for _, r := range rows {
		k := wm.Str(r["kind"])
		exp := int64(wm.Num(r["exp_ms"]))
		if exp > 0 && exp < now.UnixMilli() {
			continue // past its time (desktop-protocol §9.6)
		}
		if (k == "" && plain >= 2) || (k != "" && apps >= 10) {
			continue
		}
		t := ""
		if ts, perr := time.Parse(time.RFC3339, wm.Str(r["created_at"])); perr == nil {
			t = ts.In(loc).Format("15:04")
		}
		n := map[string]any{"title": cut(wm.Str(r["title"]), 60), "time": t}
		if k == "" {
			plain++
			// a note made for every device of the account in each language (desk_queue's "Bấm để xác nhận: …" keeps
			// data.titles): this device's; the stored (Vietnamese) title otherwise
			if lang != "vi" {
				data, _ := r["data"].(map[string]any)
				if str, isText := r["data"].(string); isText && str != "" {
					json.Unmarshal([]byte(str), &data)
				}
				if ts, _ := data["titles"].(map[string]any); ts != nil {
					if v := wm.Str(ts[lang]); v != "" {
						n["title"] = cut(v, 60)
					} else if v := wm.Str(ts["en"]); v != "" {
						n["title"] = cut(v, 60)
					}
				}
			}
		} else {
			apps++
			n["kind"] = k
			n["app"] = wm.Str(r["app"])
			n["arg"] = wm.Str(r["arg"])
			n["ref"] = wm.Str(r["ref"]) // the device's key for it: one notification per request, cancelled when it goes
			n["exp_ms"] = exp
			data, _ := r["data"].(map[string]any)
			if str, isText := r["data"].(string); isText && str != "" {
				json.Unmarshal([]byte(str), &data) // a json column read back as text
			}
			// desktop_sync stores these titles in Vietnamese ("Claude (web) đang chờ bạn", "Claude báo tình hình các
			// phiên"); a permission request's "Claude (espos): Bash git push" needs no words
			if t := wm.Str(r["title"]); lang != "vi" && k == "claude_wait" && strings.HasPrefix(t, "Claude (") && strings.HasSuffix(t, ") đang chờ bạn") {
				n["title"] = cut(strings.ReplaceAll(tr(lang, "{head} đang chờ bạn"), "{head}", strings.TrimSuffix(t, " đang chờ bạn")), 60)
			} else if lang != "vi" && k == "claude_status" {
				n["title"] = tr(lang, "Claude báo tình hình")
			}
			if data != nil && (k == "claude_perm" || k == "claude_wait" || k == "claude_status") {
				pop, acts := claudePop(k, data, lang)
				if len(acts) > 0 && !macAllows(&macs, userID, wm.Int(data["mac"]), deviceID) {
					// the Mac takes no commands from this device (its allow_from): Approve / Deny would come back
					// 403 sender_not_allowed, so the card only tells, and says where to turn this device on
					acts = []any{}
					pop["text"] = notAllowedHint(lang)
				}
				n["pop"] = pop
				n["acts"] = acts
			}
		}
		out = append(out, n)
	}
	return out, nil
}

// ---------- âm lịch (thuật toán Hồ Ngọc Đức) ----------

func jdFromDate(dd, mm, yy int) int {
	a := (14 - mm) / 12
	y := yy + 4800 - a
	m := mm + 12*a - 3
	jd := dd + (153*m+2)/5 + 365*y + y/4 - y/100 + y/400 - 32045
	if jd < 2299161 {
		jd = dd + (153*m+2)/5 + 365*y + y/4 - 32083
	}
	return jd
}

func newMoon(k int) float64 {
	kf := float64(k)
	T := kf / 1236.85
	T2 := T * T
	T3 := T2 * T
	dr := math.Pi / 180
	jd1 := 2415020.75933 + 29.53058868*kf + 0.0001178*T2 - 0.000000155*T3
	jd1 = jd1 + 0.00033*math.Sin((166.56+132.87*T-0.009173*T2)*dr)
	M := 359.2242 + 29.10535608*kf - 0.0000333*T2 - 0.00000347*T3
	Mpr := 306.0253 + 385.81691806*kf + 0.0107306*T2 + 0.00001236*T3
	F := 21.2964 + 390.67050646*kf - 0.0016528*T2 - 0.00000239*T3
	c1 := (0.1734-0.000393*T)*math.Sin(M*dr) + 0.0021*math.Sin(2*dr*M)
	c1 = c1 - 0.4068*math.Sin(Mpr*dr) + 0.0161*math.Sin(dr*2*Mpr)
	c1 = c1 - 0.0004*math.Sin(dr*3*Mpr)
	c1 = c1 + 0.0104*math.Sin(dr*2*F) - 0.0051*math.Sin(dr*(M+Mpr))
	c1 = c1 - 0.0074*math.Sin(dr*(M-Mpr)) + 0.0004*math.Sin(dr*(2*F+M))
	c1 = c1 - 0.0004*math.Sin(dr*(2*F-M)) - 0.0006*math.Sin(dr*(2*F+Mpr))
	c1 = c1 + 0.0010*math.Sin(dr*(2*F-Mpr)) + 0.0005*math.Sin(dr*(2*Mpr+M))
	var deltat float64
	if T < -11 {
		deltat = 0.001 + 0.000839*T + 0.0002261*T2 - 0.00000845*T3 - 0.000000081*T*T3
	} else {
		deltat = -0.000278 + 0.000265*T + 0.000262*T2
	}
	return jd1 + c1 - deltat
}

func newMoonDay(k int, tz float64) int {
	return int(math.Floor(newMoon(k) + 0.5 + tz/24))
}

func sunLong(jdn float64) float64 {
	T := (jdn - 2451545.0) / 36525
	T2 := T * T
	dr := math.Pi / 180
	M := 357.52910 + 35999.05030*T - 0.0001559*T2 - 0.00000048*T*T2
	L0 := 280.46645 + 36000.76983*T + 0.0003032*T2
	DL := (1.914600 - 0.004817*T - 0.000014*T2) * math.Sin(dr*M)
	DL = DL + (0.019993-0.000101*T)*math.Sin(dr*2*M) + 0.000290*math.Sin(dr*3*M)
	L := (L0 + DL) * dr
	L = L - math.Pi*2*math.Floor(L/(math.Pi*2))
	return L
}

func sunLongitude(dayNumber int, tz float64) int {
	return int(math.Floor(sunLong(float64(dayNumber)-0.5-tz/24) / math.Pi * 6))
}

func lunarMonth11(yy int, tz float64) int {
	off := jdFromDate(31, 12, yy) - 2415021
	k := int(math.Floor(float64(off) / 29.530588853))
	nm := newMoonDay(k, tz)
	if sunLongitude(nm, tz) >= 9 {
		nm = newMoonDay(k-1, tz)
	}
	return nm
}

func leapMonthOffset(a11 int, tz float64) int {
	k := int(math.Floor((float64(a11)-2415021.076998695)/29.530588853 + 0.5))
	i := 1
	arc := sunLongitude(newMoonDay(k+i, tz), tz)
	last := arc
	for {
		last = arc
		i++
		arc = sunLongitude(newMoonDay(k+i, tz), tz)
		if arc == last || i >= 14 {
			break
		}
	}
	return i - 1
}

func solar2Lunar(dd, mm, yy int, tz float64) (int, int, int, bool) {
	dayNumber := jdFromDate(dd, mm, yy)
	k := int(math.Floor((float64(dayNumber) - 2415021.076998695) / 29.530588853))
	monthStart := newMoonDay(k+1, tz)
	if monthStart > dayNumber {
		monthStart = newMoonDay(k, tz)
	}
	a11 := lunarMonth11(yy, tz)
	b11 := a11
	lunarYear := 0
	if a11 >= monthStart {
		lunarYear = yy
		a11 = lunarMonth11(yy-1, tz)
	} else {
		lunarYear = yy + 1
		b11 = lunarMonth11(yy+1, tz)
	}
	lunarDay := dayNumber - monthStart + 1
	diff := int(math.Floor(float64(monthStart-a11) / 29))
	leap := false
	lunarMonth := diff + 11
	if b11-a11 > 365 {
		lmd := leapMonthOffset(a11, tz)
		if diff >= lmd {
			lunarMonth = diff + 10
			if diff == lmd {
				leap = true
			}
		}
	}
	if lunarMonth > 12 {
		lunarMonth -= 12
	}
	if lunarMonth >= 11 && diff < 4 {
		lunarYear--
	}
	return lunarDay, lunarMonth, lunarYear, leap
}

// claudeFor: the newest EspOS Desktop of the person (desk_state) as the S3's Claude widget reads it
// (s3desk/docs/desktop-protocol.md §2.1 usage): online, the Mac's name and mode, the 5-hour and weekly windows (only
// when the Mac's status line helper sent them), tokens and the API-price estimate today, the last 7 days' tokens,
// sessions open and waiting (for approval or a reply). nil when the person has no desktop.
func claudeFor(userID int, now time.Time, loc *time.Location) (map[string]any, error) {
	rows, err := wm.QueryOpts("desk_state", map[string]any{
		"filters":  []map[string]any{{"column": "user_id", "op": "eq", "value": userID}},
		"order_by": "seen_ms", "desc": true, "limit": 1,
	})
	if err != nil || len(rows) == 0 {
		return nil, err
	}
	r := rows[0]
	st, _ := r["state"].(map[string]any)
	if raw, isStr := r["state"].(string); isStr && raw != "" {
		_ = json.Unmarshal([]byte(raw), &st)
	}
	seen := int64(wm.Num(r["seen_ms"]))
	mode := wm.Str(r["mode"])
	out := map[string]any{"online": seen > 0 && now.UnixMilli()-seen <= 180000 && mode != "off", "seen_ms": seen, "mode": mode,
		"name": cut(wm.Str(st["name"]), 40)}
	// Times in the person's own zone, ready to print (device and web preview show the same text): "16:20", the
	// weekday (0 = Sunday) and how many days from today.
	today := now.In(loc)
	t0 := time.Date(today.Year(), today.Month(), today.Day(), 0, 0, 0, 0, loc)
	when := func(sec int64) map[string]any {
		t := time.Unix(sec, 0).In(loc)
		d0 := time.Date(t.Year(), t.Month(), t.Day(), 0, 0, 0, 0, loc)
		return map[string]any{"hm": t.Format("15:04"), "wd": int(t.Weekday()), "dd": int(math.Round(d0.Sub(t0).Hours() / 24)), "dm": t.Format("02/01")}
	}
	if seen > 0 {
		out["seen"] = when(seen / 1000)
	}
	waiting, open := 0, 0
	if ss, ok := st["sessions"].([]any); ok {
		for _, x := range ss {
			if m, isMap := x.(map[string]any); isMap {
				open++
				if s := wm.Str(m["state"]); s == "perm" || s == "input" {
					waiting++
				}
			}
		}
	}
	out["waiting"] = waiting
	out["sessions"] = open
	u, _ := st["usage"].(map[string]any)
	if u == nil {
		return out, nil
	}
	win := func(k string) map[string]any {
		w, _ := u[k].(map[string]any)
		if w == nil || w["pct"] == nil {
			return nil
		}
		resets := int64(wm.Num(w["resets_s"]))
		if resets > 0 && resets < now.Unix() {
			return nil // the window has reset since: the old figure no longer holds
		}
		o := map[string]any{"pct": math.Round(math.Max(0, math.Min(100, wm.Num(w["pct"])))), "resets_s": resets}
		if resets > 0 {
			o["at"] = when(resets)
		}
		return o
	}
	if w := win("five_hour"); w != nil {
		out["five"] = w
	}
	if w := win("seven_day"); w != nil {
		out["week"] = w
	}
	if a := int64(wm.Num(u["as_of_ms"])); a > 0 {
		out["as_of_ms"] = a
	}
	if t, ok := u["today"].(map[string]any); ok {
		out["tokens"] = int64(wm.Num(t["tokens"]))
		out["usd"] = math.Round(wm.Num(t["usd"])*100) / 100
	}
	days := []any{}
	if ds, ok := u["days"].([]any); ok {
		for _, x := range ds {
			if m, isMap := x.(map[string]any); isMap {
				days = append(days, int64(wm.Num(m["tokens"])))
			}
		}
	}
	if len(days) > 7 {
		days = days[len(days)-7:]
	}
	if len(days) > 0 {
		out["days"] = days
	}
	if n := wm.Int(u["sessions_open"]); n > open {
		out["sessions"] = n
	}
	return out, nil
}
