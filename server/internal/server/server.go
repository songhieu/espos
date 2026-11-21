// Package server is the HTTP side: the web app, sign-in for the one account, the /v1 API
// (each route runs its workflow), the device guard, and signed file links.
package server

import (
	"crypto/hmac"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	"espos/internal/engine"
	"espos/internal/wm"
)

// Config is what the server needs to know.
type Config struct {
	User, Password string
	PublicURL      string // optional: the address the browser and the device use
	FirmwareDir    string // install bundles for the web installer (latest.json + .bin)
	SigningKey     []byte // ties sign-in sessions to the current user name and password
}

// Route is one /v1 route (internal/backend/routes.json).
type Route struct {
	Method   string   `json:"method"`
	Path     string   `json:"path"`
	Auth     string   `json:"auth"`
	Guards   []string `json:"guards"`
	Workflow string   `json:"workflow"`
	PerMin   int      `json:"rate_limit_per_min"`
}

// CheckRoutes refuses a route table with an auth mode this server does not know.
func CheckRoutes(routes []Route) error {
	for _, r := range routes {
		if r.Auth != "public" && r.Auth != "end_user" {
			return fmt.Errorf("%s %s: unknown auth %q", r.Method, r.Path, r.Auth)
		}
	}
	return nil
}

// Server is the HTTP handler.
type Server struct {
	cfg    Config
	eng    *engine.Engine
	app    fs.FS
	mux    *http.ServeMux
	userID int
	limits *limiter
	cred   string // fingerprint of the current user name and password
}

// New builds the handler.
func New(cfg Config, eng *engine.Engine, routes []Route, app fs.FS) *Server {
	s := &Server{cfg: cfg, eng: eng, app: app, mux: http.NewServeMux(), userID: 1, limits: newLimiter()}
	mac := hmac.New(sha256.New, cfg.SigningKey)
	fmt.Fprintf(mac, "%s\n%s", cfg.User, cfg.Password)
	s.cred = hex.EncodeToString(mac.Sum(nil))
	s.mux.HandleFunc("GET /auth/config", s.authConfig)
	s.mux.HandleFunc("POST /auth/login", s.login)
	s.mux.HandleFunc("POST /auth/logout", s.logout)
	s.mux.HandleFunc("GET /auth/me", s.me)
	s.mux.HandleFunc("POST /auth/register", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusForbidden, map[string]any{"error": "this server has a single account"})
	})
	s.mux.HandleFunc("GET /files/{key...}", s.fileGet)
	s.mux.HandleFunc("PUT /files/{key...}", s.filePut)
	s.mux.HandleFunc("GET /firmware/{name}", s.firmware)
	s.mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) { w.Write([]byte("ok")) })
	for _, rt := range routes {
		pattern := rt.Method + " " + colonParams(rt.Path)
		s.mux.HandleFunc(pattern, s.route(rt))
	}
	s.mux.HandleFunc("GET /", s.appFile)
	return s
}

// "/v1/devices/:id/settings" → "/v1/devices/{id}/settings"
func colonParams(p string) string {
	parts := strings.Split(p, "/")
	for i, seg := range parts {
		if strings.HasPrefix(seg, ":") {
			parts[i] = "{" + seg[1:] + "}"
		}
	}
	return strings.Join(parts, "/")
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	h := w.Header()
	h.Set("X-Content-Type-Options", "nosniff")
	h.Set("X-Frame-Options", "DENY")
	h.Set("Referrer-Policy", "same-origin")
	if r.Method == http.MethodOptions {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	s.mux.ServeHTTP(w, r)
}

// base is this server's address as this request sees it. X-Forwarded-Proto only changes the
// links in the answer to the request that sent it.
func (s *Server) base(r *http.Request) string {
	if s.cfg.PublicURL != "" {
		return strings.TrimRight(s.cfg.PublicURL, "/")
	}
	proto := "http"
	if r.TLS != nil {
		proto = "https"
	}
	if p := strings.ToLower(strings.TrimSpace(strings.Split(r.Header.Get("X-Forwarded-Proto"), ",")[0])); p == "https" || p == "http" {
		proto = p
	}
	return proto + "://" + r.Host
}

func clientIP(r *http.Request) string {
	ip, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return ip
}

// route runs a route's workflow with the request as {{ input }}.
func (s *Server) route(rt Route) http.HandlerFunc {
	perMin := rt.PerMin
	if perMin <= 0 && rt.Auth == "public" && len(rt.Guards) == 0 {
		perMin = 30 // open routes (pairing, notifications) are rate-limited even when not declared
	}
	return func(w http.ResponseWriter, r *http.Request) {
		if perMin > 0 && !s.limits.allow(rt.Method+rt.Path+"|"+clientIP(r), perMin) {
			writeJSON(w, http.StatusTooManyRequests, map[string]any{"error": "too many requests, try again in a minute"})
			return
		}
		signed := rt.Auth == "public" || s.signedIn(r)
		if !signed {
			writeJSON(w, http.StatusUnauthorized, map[string]any{"error": "sign in"})
			return
		}
		_ = http.NewResponseController(w).SetReadDeadline(time.Now().Add(60 * time.Second))
		input, err := s.requestInput(w, r, rt)
		if err != nil {
			var tooBig *http.MaxBytesError
			if errors.As(err, &tooBig) {
				writeJSON(w, http.StatusRequestEntityTooLarge, map[string]any{"error": "request too large"})
				return
			}
			writeJSON(w, http.StatusBadRequest, map[string]any{"error": "could not read the request"})
			return
		}
		ctx := map[string]any{}
		if rt.Auth != "public" {
			input["user"] = map[string]any{"id": float64(s.userID), "email": s.cfg.User}
			ctx["end_user_id"] = float64(s.userID)
		}
		// Each guard sees what the ones before it returned (desk_app_auth reads require_device's), as on the hosted
		// platform; the workflow then gets them all.
		guards := map[string]any{}
		input["guard"] = guards
		for _, g := range rt.Guards {
			res, err := s.eng.Run(g, input, ctx, s.base(r))
			if err != nil {
				log.Printf("guard %s: %v", g, err)
				writeJSON(w, http.StatusInternalServerError, map[string]any{"error": "guard failed"})
				return
			}
			if res.Deny {
				status := res.Status
				if status == 0 {
					status = http.StatusForbidden
				}
				writeJSON(w, status, res.Body)
				return
			}
			guards[g] = res.Data
		}
		res, err := s.eng.Run(rt.Workflow, input, ctx, s.base(r))
		if err != nil {
			log.Printf("%s %s: %v", rt.Method, rt.Path, err)
			writeJSON(w, http.StatusInternalServerError, map[string]any{"error": "internal error"})
			return
		}
		writeResult(w, res)
	}
}

func (s *Server) requestInput(w http.ResponseWriter, r *http.Request, rt Route) (map[string]any, error) {
	params := map[string]any{}
	for _, seg := range strings.Split(rt.Path, "/") {
		if strings.HasPrefix(seg, ":") {
			params[seg[1:]] = r.PathValue(seg[1:])
		}
	}
	query := map[string]any{}
	for k, v := range r.URL.Query() {
		if len(v) > 0 {
			query[k] = v[0]
		}
	}
	headers := map[string]any{}
	for k, v := range r.Header {
		headers[strings.ToLower(k)] = strings.Join(v, ", ")
	}
	// Nodes build links back to this server from the host: give them the scheme too.
	headers["host"] = s.base(r)
	var body any
	raw64 := ""
	if r.Body != nil {
		raw, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 8<<20))
		if err != nil {
			return nil, err
		}
		ctype := strings.ToLower(r.Header.Get("Content-Type"))
		textual := ctype == "" || strings.Contains(ctype, "json") || strings.HasPrefix(ctype, "text/") || strings.Contains(ctype, "form")
		switch {
		case len(raw) > 0 && !textual:
			// A binary body (the dog assistant's recording, audio/wav): as the hosted platform hands it, base64
			raw64 = base64.StdEncoding.EncodeToString(raw)
		case len(strings.TrimSpace(string(raw))) > 0:
			if err := json.Unmarshal(raw, &body); err != nil {
				body = string(raw)
			}
		}
	}
	in := map[string]any{
		"method": r.Method, "path": r.URL.Path, "params": params, "query": query,
		"headers": headers, "body": body, "ip": clientIP(r),
	}
	if raw64 != "" {
		in["raw_body_base64"] = raw64
	}
	return in, nil
}

func writeResult(w http.ResponseWriter, res *engine.Result) {
	status := res.Status
	if status == 0 {
		status = http.StatusOK
	}
	for k, v := range res.Headers {
		w.Header().Set(k, v)
	}
	if res.Stream != nil { // a streamed answer (the dog's voice): sent as it comes, flushed piece by piece
		defer res.Stream.Close()
		w.WriteHeader(status)
		fl, _ := w.(http.Flusher)
		buf := make([]byte, 8192)
		for {
			n, err := res.Stream.Read(buf)
			if n > 0 {
				if _, werr := w.Write(buf[:n]); werr != nil {
					return
				}
				if fl != nil {
					fl.Flush()
				}
			}
			if err != nil {
				return
			}
		}
	}
	switch b := res.Body.(type) {
	case nil:
		w.WriteHeader(status)
	case string:
		if w.Header().Get("Content-Type") == "" {
			w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		}
		w.WriteHeader(status)
		io.WriteString(w, b)
	default:
		writeJSON(w, status, b)
	}
}

func writeJSON(w http.ResponseWriter, status int, v any) {
	if w.Header().Get("Content-Type") == "" {
		w.Header().Set("Content-Type", "application/json")
	}
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(v)
}

// ---- files (signed links made by storage_sign) ----

func (s *Server) fileGet(w http.ResponseWriter, r *http.Request) {
	key := r.PathValue("key")
	if _, err := wm.CheckSigned(key, "GET", r.URL.Query()); err != nil {
		writeJSON(w, http.StatusForbidden, map[string]any{"error": err.Error()})
		return
	}
	p, err := wm.FilePath(key)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "bad file key"})
		return
	}
	w.Header().Set("Cache-Control", "private, max-age=300")
	http.ServeFile(w, r, p)
}

func (s *Server) filePut(w http.ResponseWriter, r *http.Request) {
	key := r.PathValue("key")
	limit, err := wm.CheckSigned(key, "PUT", r.URL.Query())
	if err != nil {
		writeJSON(w, http.StatusForbidden, map[string]any{"error": err.Error()})
		return
	}
	if limit <= 0 {
		limit = 100 << 20
	}
	p, err := wm.FilePath(key)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "bad file key"})
		return
	}
	_ = http.NewResponseController(w).SetReadDeadline(time.Now().Add(30 * time.Minute))
	fail := func(status int, msg string, err error) {
		if err != nil {
			log.Printf("upload %s: %v", key, err)
		}
		writeJSON(w, status, map[string]any{"error": msg})
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o700); err != nil {
		fail(http.StatusInternalServerError, "could not store the file", err)
		return
	}
	// Each upload writes its own temporary file: two uploads of one key cannot mix.
	f, err := os.CreateTemp(filepath.Dir(p), ".up-*.part")
	if err != nil {
		fail(http.StatusInternalServerError, "could not store the file", err)
		return
	}
	n, err := io.Copy(f, io.LimitReader(r.Body, limit+1))
	cerr := f.Close()
	if err == nil {
		err = cerr
	}
	switch {
	case err != nil:
		os.Remove(f.Name())
		fail(http.StatusBadRequest, "the upload did not finish", err)
		return
	case n > limit:
		os.Remove(f.Name())
		fail(http.StatusRequestEntityTooLarge, "file is too big", nil)
		return
	}
	if err := os.Rename(f.Name(), p); err != nil {
		os.Remove(f.Name())
		fail(http.StatusInternalServerError, "could not store the file", err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"key": key, "size": n})
}

// firmware serves the install bundle the web installer flashes over USB.
func (s *Server) firmware(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	if name != filepath.Base(name) || strings.HasPrefix(name, ".") {
		http.NotFound(w, r)
		return
	}
	p := filepath.Join(s.cfg.FirmwareDir, name)
	if _, err := os.Stat(p); err != nil {
		writeJSON(w, http.StatusNotFound, map[string]any{"error": "no firmware bundle on this server: see README, Firmware"})
		return
	}
	if name == "latest.json" {
		w.Header().Set("Cache-Control", "no-store")
	} else {
		w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
	}
	w.Header().Set("Access-Control-Allow-Origin", "*")
	http.ServeFile(w, r, p)
}

// ---- the web app ----

func (s *Server) appFile(w http.ResponseWriter, r *http.Request) {
	for _, api := range []string{"/v1/", "/auth/", "/files/", "/firmware/"} {
		if strings.HasPrefix(r.URL.Path, api) {
			writeJSON(w, http.StatusNotFound, map[string]any{"error": "not found"})
			return
		}
	}
	name := strings.TrimPrefix(r.URL.Path, "/")
	if name != "" && name != "index.html" {
		if f, err := s.app.Open(name); err == nil {
			st, _ := f.Stat()
			f.Close()
			if st != nil && !st.IsDir() {
				if strings.HasPrefix(name, "assets/") {
					w.Header().Set("Cache-Control", "public, max-age=31536000, immutable")
				}
				http.ServeFileFS(w, r, s.app, name)
				return
			}
		}
	}
	page, err := fs.ReadFile(s.app, "index.html")
	if err != nil {
		w.Header().Set("Content-Type", "text/plain; charset=utf-8")
		w.WriteHeader(http.StatusServiceUnavailable)
		io.WriteString(w, "The web app is not built into this binary. Run `make` (see README).\n")
		return
	}
	conf, _ := json.Marshal(map[string]any{
		"app": "espos", "version": nil, "ref": "self", "entry": "", "base": "/", "root": "app-espos",
		"store": map[string]any{"id": "1", "slug": "espos"}, "locale": "vi", "locale_default": "vi",
		"api": s.base(r), "base_path": "/", "preview": false,
	})
	html := strings.Replace(string(page), "<!--ESPOS_CONFIG-->",
		`<script type="application/json" id="app-espos-config">`+string(conf)+`</script>`, 1)
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	io.WriteString(w, html)
}

// ---- sign-in: one account, from the server's configuration ----

const sessionDays = 60

func tokenHash(t string) string {
	h := sha256.Sum256([]byte(t))
	return fmt.Sprintf("%x", h)
}

func (s *Server) authConfig(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]any{"password": true, "providers": []any{}})
}

func (s *Server) login(w http.ResponseWriter, r *http.Request) {
	var in struct {
		Email    string `json:"email"`
		Password string `json:"password"`
	}
	ip := clientIP(r)
	if wait := s.limits.loginWait(ip); wait > 0 {
		w.Header().Set("Retry-After", fmt.Sprint(int(wait.Seconds())+1))
		writeJSON(w, http.StatusTooManyRequests, map[string]any{"error": fmt.Sprintf("Sai nhiều lần. Thử lại sau %d giây.", int(wait.Seconds())+1)})
		return
	}
	if err := json.NewDecoder(io.LimitReader(r.Body, 1<<16)).Decode(&in); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "bad request"})
		return
	}
	same := func(a, b string) bool {
		ha, hb := sha256.Sum256([]byte(a)), sha256.Sum256([]byte(b))
		return subtle.ConstantTimeCompare(ha[:], hb[:]) == 1
	}
	userOK := same(strings.TrimSpace(in.Email), s.cfg.User)
	passOK := same(in.Password, s.cfg.Password)
	if !userOK || !passOK {
		s.limits.loginFailed(ip)
		writeJSON(w, http.StatusUnauthorized, map[string]any{"error": "Sai tên đăng nhập hoặc mật khẩu"})
		return
	}
	s.limits.loginOK(ip)
	tok := randomToken()
	if _, err := wm.Insert("sessions", map[string]any{
		"token_hash": tokenHash(tok), "expires": time.Now().Add(sessionDays * 24 * time.Hour).Unix(), "cred": s.cred,
	}); err != nil {
		log.Printf("login: %v", err)
		writeJSON(w, http.StatusInternalServerError, map[string]any{"error": "could not sign in"})
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"token": tok, "user": s.user()})
}

func (s *Server) user() map[string]any { return map[string]any{"id": s.userID, "email": s.cfg.User} }

func bearer(r *http.Request) string {
	h := r.Header.Get("Authorization")
	if strings.HasPrefix(h, "Bearer ") {
		return strings.TrimSpace(h[7:])
	}
	return ""
}

func (s *Server) session(r *http.Request) map[string]any {
	tok := bearer(r)
	if tok == "" {
		return nil
	}
	rows, err := wm.QueryOpts("sessions", map[string]any{
		"filters": []map[string]any{{"column": "token_hash", "op": "eq", "value": tokenHash(tok)}}, "limit": 1,
	})
	if err != nil || len(rows) != 1 {
		return nil
	}
	// Sessions end when they expire, and when the user name or password changes.
	if int64(wm.Num(rows[0]["expires"])) < time.Now().Unix() || wm.Str(rows[0]["cred"]) != s.cred {
		_ = wm.Delete("sessions", rows[0]["id"])
		return nil
	}
	return rows[0]
}

func (s *Server) signedIn(r *http.Request) bool { return s.session(r) != nil }

func (s *Server) me(w http.ResponseWriter, r *http.Request) {
	if !s.signedIn(r) {
		writeJSON(w, http.StatusUnauthorized, map[string]any{"error": "sign in"})
		return
	}
	writeJSON(w, http.StatusOK, s.user())
}

func (s *Server) logout(w http.ResponseWriter, r *http.Request) {
	if sess := s.session(r); sess != nil {
		_ = wm.Delete("sessions", sess["id"])
	}
	writeJSON(w, http.StatusOK, map[string]any{"ok": true})
}
