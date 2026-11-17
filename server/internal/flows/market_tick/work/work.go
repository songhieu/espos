// Workflow market_tick, node work (imported by tools/importer).
package work

import (
	"encoding/json"
	"fmt"
	"inkdesk/internal/wm"
	"strconv"
	"strings"
	"time"
)

// Public market data for the InkDesk widgets, cached in market_cache:
//   gold  — PNJ's price board (SJC bars, 9999 rings), million VND per tael, change vs the day before
//   index — VPS datafeed: VN-Index, VN30, HNX, UPCoM
//   stk   — VPS datafeed: the stock symbols readers put in their stocks widgets
//   crypto — CoinGecko: BTC, ETH and a few other coins, in USD and VND with the 24 h change
//   fx    — Vietcombank's rate list (asks for one request per 5 minutes at most: hourly here)
// Devices and the web only ever read this cache.

var headers = map[string]any{"User-Agent": "Mozilla/5.0 (InkDesk e-ink dashboard)", "Accept": "*/*"}

func get(u string) (any, error) {
	res, err := wm.FetchWith("GET", u, nil, headers)
	if err != nil {
		return nil, err
	}
	if s := wm.Int(res["status"]); s != 200 {
		return nil, fmt.Errorf("status %d", s)
	}
	return res["body"], nil
}

func cached(key string) (map[string]any, int64, error) {
	rows, err := wm.QueryOpts("market_cache", map[string]any{"filters": []map[string]any{{"column": "key", "op": "eq", "value": key}}, "limit": 1})
	if err != nil || len(rows) == 0 {
		return nil, 0, err
	}
	d, _ := rows[0]["data"].(map[string]any)
	return d, int64(wm.Num(rows[0]["fetched_ms"])), nil
}

func save(key string, data map[string]any) error {
	_, err := wm.Upsert("market_cache", []string{"key"}, map[string]any{"key": key, "data": data, "fetched_ms": time.Now().UnixMilli()}, nil)
	return err
}

func round(v float64, places int) float64 {
	f, _ := strconv.ParseFloat(strconv.FormatFloat(v, 'f', places, 64), 64)
	return f
}

// Short names for PNJ's codes (the device shows them in narrow widgets).
var goldNames = map[string]string{
	"SJC": "Vàng SJC", "N24K": "Nhẫn 9999", "KB": "Kim Bảo", "TL": "Phúc Lộc Tài", "PNJ": "PNJ Phượng Hoàng",
	"24K": "Nữ trang 9999", "999": "Nữ trang 999", "9920": "Nữ trang 9920", "99": "Nữ trang 99", "22K": "Vàng 22K",
	"75": "Vàng 18K", "68": "Vàng 16,3K", "65": "Vàng 15,6K", "61": "Vàng 14,6K", "58.5": "Vàng 14K",
	"41": "Vàng 10K", "37.5": "Vàng 9K", "33": "Vàng 8K",
}

func gold(day, at string) error {
	body, err := get("https://edge-api.pnj.io/ecom-frontend/v1/get-gold-price?zone=00")
	if err != nil {
		return err
	}
	m, _ := body.(map[string]any)
	list, _ := m["data"].([]any)
	if len(list) == 0 {
		return fmt.Errorf("no prices")
	}
	old, _, err := cached("gold")
	if err != nil {
		return err
	}
	// The change is against the last price of the previous day.
	prev, _ := old["prev"].(map[string]any)
	if old != nil && wm.Str(old["day"]) != day {
		prev, _ = old["last"].(map[string]any)
	}
	items := []any{}
	last := map[string]any{}
	for _, it := range list {
		p, _ := it.(map[string]any)
		code := wm.Str(p["masp"])
		name, known := goldNames[code]
		if !known || wm.Num(p["giaban"]) <= 0 {
			continue // raw-material buy-only lines
		}
		// PNJ quotes thousand VND per chỉ: /100 gives million VND per tael (lượng).
		buy, sell := round(wm.Num(p["giamua"])/100, 2), round(wm.Num(p["giaban"])/100, 2)
		q := map[string]any{"c": code, "n": name, "b": buy, "s": sell}
		if prev != nil {
			if ps := wm.Num(prev[code]); ps > 0 {
				q["d"] = round(sell-ps, 2)
			}
		}
		items = append(items, q)
		last[code] = sell
	}
	return save("gold", map[string]any{"src": "PNJ", "at": at, "day": day, "items": items, "prev": prev, "last": last})
}

func indices(at string) error {
	body, err := get("https://bgapidatafeed.vps.com.vn/getlistindexdetail/10,11,02,03")
	if err != nil {
		return err
	}
	list, _ := body.([]any)
	names := map[string]string{"10": "VNINDEX", "11": "VN30", "02": "HNX", "03": "UPCOM"}
	items := map[string]any{}
	for _, it := range list {
		o, _ := it.(map[string]any)
		name := names[wm.Str(o["mc"])]
		p, ref := wm.Num(o["cIndex"]), wm.Num(o["oIndex"])
		if name == "" || p <= 0 {
			continue
		}
		// The feed's change fields carry no sign: take it from the reference (previous close).
		ch, pc := 0.0, 0.0
		if ref > 0 {
			ch = p - ref
			pc = ch / ref * 100
		}
		items[name] = map[string]any{"s": name, "p": round(p, 2), "c": round(ch, 2), "pc": round(pc, 2)}
	}
	if len(items) == 0 {
		return fmt.Errorf("no indices")
	}
	return save("index", map[string]any{"at": at, "items": items})
}

func attr(tag, name string) string {
	i := strings.Index(tag, name+"=\"")
	if i < 0 {
		return ""
	}
	rest := tag[i+len(name)+2:]
	j := strings.Index(rest, "\"")
	if j < 0 {
		return ""
	}
	return strings.TrimSpace(rest[:j])
}

func fx(at string) error {
	body, err := get("https://portal.vietcombank.com.vn/Usercontrols/TVPortal.TyGia/pXML.aspx?b=10")
	if err != nil {
		return err
	}
	xml := wm.Str(body)
	items := []any{}
	for _, part := range strings.Split(xml, "<Exrate ")[1:] {
		code := attr(part, "CurrencyCode")
		if code == "" {
			continue
		}
		num := func(s string) float64 {
			v, _ := strconv.ParseFloat(strings.ReplaceAll(s, ",", ""), 64)
			return v
		}
		items = append(items, map[string]any{"c": code, "b": num(attr(part, "Buy")), "s": num(attr(part, "Sell"))})
	}
	if len(items) == 0 {
		return fmt.Errorf("no rates")
	}
	return save("fx", map[string]any{"at": at, "items": items})
}

var coins = []struct{ sym, id string }{
	{"BTC", "bitcoin"}, {"ETH", "ethereum"}, {"BNB", "binancecoin"}, {"SOL", "solana"},
	{"XRP", "ripple"}, {"DOGE", "dogecoin"}, {"TON", "the-open-network"},
}

func crypto(at string) error {
	ids := make([]string, 0, len(coins))
	for _, c := range coins {
		ids = append(ids, c.id)
	}
	body, err := get("https://api.coingecko.com/api/v3/simple/price?ids=" + strings.Join(ids, ",") +
		"&vs_currencies=usd,vnd&include_24hr_change=true")
	if err != nil {
		return err
	}
	m, _ := body.(map[string]any)
	items := map[string]any{}
	for _, c := range coins {
		o, _ := m[c.id].(map[string]any)
		if o == nil || wm.Num(o["usd"]) <= 0 {
			continue
		}
		items[c.sym] = map[string]any{"s": c.sym, "p": round(wm.Num(o["usd"]), 4), "v": round(wm.Num(o["vnd"]), 0),
			"pc": round(wm.Num(o["usd_24h_change"]), 2)}
	}
	if len(items) == 0 {
		return fmt.Errorf("no coins")
	}
	return save("crypto", map[string]any{"at": at, "items": items})
}

var indexNames = map[string]bool{"VNINDEX": true, "VN30": true, "HNX": true, "UPCOM": true}

// The symbols after "stocks:" in a colour desk's widget options (only market widgets carry such tokens).
func s3Stocks(s string) []string {
	out := []string{}
	for {
		i := strings.Index(s, "stocks:")
		if i < 0 {
			return out
		}
		s = s[i+len("stocks:"):]
		n := 0
		for n < len(s) && n < 11 && (s[n] >= 'A' && s[n] <= 'Z' || s[n] >= 'a' && s[n] <= 'z' || s[n] >= '0' && s[n] <= '9') {
			n++
		}
		if n > 0 && n <= 10 {
			out = append(out, s[:n])
		}
		s = s[n:]
	}
}

// The symbols in every stocks widget of every reader's layouts (indices come from "index").
func wantedSymbols() ([]string, error) {
	rows, err := wm.QueryOpts("device_settings", map[string]any{"filters": []map[string]any{
		{"column": "skey", "op": "in", "value": []any{"home.w", "sleep.wl", "sleep.wp", "s3.home", "s3.saver_pages", "s3.markets"}}}, "limit": 4000})
	if err != nil {
		return nil, err
	}
	seen := map[string]bool{}
	out := []string{}
	add := func(t string) {
		t = strings.ToUpper(strings.TrimSpace(t))
		if t == "" || len(t) > 10 || indexNames[t] || seen[t] || len(out) >= 300 {
			return
		}
		seen[t] = true
		out = append(out, t)
	}
	for _, r := range rows {
		// The colour desk's market widgets list "stocks:FPT" tokens among others (home and saver pages), and so
		// does its Markets app's list (s3.markets).
		if k := wm.Str(r["skey"]); k == "s3.home" || k == "s3.saver_pages" || k == "s3.markets" {
			for _, t := range s3Stocks(wm.Str(r["svalue"])) {
				add(t)
			}
			continue
		}
		var doc struct {
			I [][]any `json:"i"`
		}
		if json.Unmarshal([]byte(wm.Str(r["svalue"])), &doc) != nil {
			continue
		}
		for _, it := range doc.I {
			if len(it) < 7 || wm.Str(it[0]) != "stocks" {
				continue
			}
			for _, t := range strings.Split(wm.Str(it[6]), ",") {
				add(t)
			}
		}
	}
	return out, nil
}

func stocks(at string) error {
	syms, err := wantedSymbols()
	if err != nil || len(syms) == 0 {
		return err
	}
	items := map[string]any{}
	for i := 0; i < len(syms); i += 30 {
		end := i + 30
		if end > len(syms) {
			end = len(syms)
		}
		body, gerr := get("https://bgapidatafeed.vps.com.vn/getliststockdata/" + strings.Join(syms[i:end], ","))
		if gerr != nil {
			return gerr
		}
		list, _ := body.([]any)
		for _, it := range list {
			o, _ := it.(map[string]any)
			sym := strings.ToUpper(wm.Str(o["sym"]))
			last, ref := wm.Num(o["lastPrice"]), wm.Num(o["r"])
			if sym == "" || ref <= 0 {
				continue
			}
			if last <= 0 {
				last = ref // before the first trade of the day
			}
			// The feed's change fields carry no sign: take it from the reference price.
			ch := last - ref
			items[sym] = map[string]any{"s": sym, "p": round(last, 2), "c": round(ch, 2), "pc": round(ch/ref*100, 2)}
		}
	}
	return save("stk", map[string]any{"at": at, "items": items})
}

// A symbol in some layout that the cache does not have yet.
func newSymbols() bool {
	syms, err := wantedSymbols()
	if err != nil {
		return false
	}
	stk, _, _ := cached("stk")
	items, _ := stk["items"].(map[string]any)
	for _, s := range syms {
		if items[s] == nil {
			return true
		}
	}
	return false
}

func Run(wmIn, wmCtx map[string]any) map[string]any {
	now := time.Now().UTC()
	local := now.Add(7 * time.Hour) // Vietnam: where the markets are
	day, at := local.Format("2006-01-02"), local.Format("15:04")
	out := map[string]any{}
	if err := gold(day, at); err != nil {
		out["gold"] = err.Error()
		wm.Log("gold: ", err.Error())
	}
	// Indices move only in trading hours (Mon-Fri, 9:00-15:00): refresh then, or when missing.
	_, idxMs, _ := cached("index")
	wd := local.Weekday()
	trading := wd != time.Saturday && wd != time.Sunday && local.Hour() >= 9 && local.Hour() < 16
	_, stkMs, _ := cached("stk")
	if trading || idxMs == 0 {
		if err := indices(at); err != nil {
			out["index"] = err.Error()
			wm.Log("index: ", err.Error())
		}
	}
	// Symbols added to a widget show up within 15 minutes, even out of trading hours.
	if trading || stkMs == 0 || now.UnixMilli()-stkMs > 60*60*1000 || newSymbols() {
		if err := stocks(at); err != nil {
			out["stk"] = err.Error()
			wm.Log("stk: ", err.Error())
		}
	}
	if err := crypto(at); err != nil {
		out["crypto"] = err.Error()
		wm.Log("crypto: ", err.Error())
	}
	_, fxMs, _ := cached("fx")
	if now.UnixMilli()-fxMs > 55*60*1000 {
		if err := fx(at); err != nil {
			out["fx"] = err.Error()
			wm.Log("fx: ", err.Error())
		}
	}
	return map[string]any{"ok": true, "errors": out}
}
