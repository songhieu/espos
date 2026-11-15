package wm

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io/fs"
	"mime"
	"net/url"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Files live under one directory, one file per key ("books/u1/123-name.epub").

var (
	filesRoot string
	signKey   []byte
)

// OpenStorage sets the files directory and the key that signs file links.
func OpenStorage(root string, secret []byte) error {
	if err := os.MkdirAll(root, 0o700); err != nil {
		return err
	}
	filesRoot = root
	signKey = secret
	return nil
}

// FilePath maps a storage key onto its file, refusing keys that would leave the root.
func FilePath(key string) (string, error) {
	clean := path.Clean("/" + key)
	if key == "" || strings.Contains(key, "\\") || clean == "/" {
		return "", fmt.Errorf("bad storage key %q", key)
	}
	return filepath.Join(filesRoot, filepath.FromSlash(strings.TrimPrefix(clean, "/"))), nil
}

// StorageList lists the files whose keys start with prefix, after the key `after`.
func StorageList(prefix, after string, limit int) (map[string]any, error) {
	if limit <= 0 {
		limit = 100
	}
	type item struct {
		key  string
		size int64
		mod  time.Time
	}
	items := []item{}
	err := filepath.WalkDir(filesRoot, func(p string, d fs.DirEntry, err error) error {
		if err != nil {
			if errors.Is(err, fs.ErrNotExist) {
				return nil
			}
			return err
		}
		if d.IsDir() {
			return nil
		}
		rel, _ := filepath.Rel(filesRoot, p)
		key := filepath.ToSlash(rel)
		if strings.HasSuffix(key, ".part") || !strings.HasPrefix(key, prefix) || (after != "" && key <= after) {
			return nil
		}
		info, ierr := d.Info()
		if ierr != nil {
			return nil
		}
		items = append(items, item{key, info.Size(), info.ModTime()})
		return nil
	})
	if err != nil {
		return nil, err
	}
	sort.Slice(items, func(i, j int) bool { return items[i].key < items[j].key })
	next := ""
	if len(items) > limit {
		items = items[:limit]
		next = items[limit-1].key
	}
	files := make([]any, 0, len(items))
	for _, it := range items {
		files = append(files, map[string]any{
			"key": it.key, "size": float64(it.size), "mime": mimeOf(it.key),
			"updated_at": it.mod.UTC().Format(time.RFC3339),
		})
	}
	return map[string]any{"files": files, "next": next}, nil
}

func mimeOf(key string) string {
	if t := mime.TypeByExtension(path.Ext(key)); t != "" {
		return t
	}
	return "application/octet-stream"
}

// StoragePut writes a file.
func StoragePut(key string, data []byte) error {
	p, err := FilePath(key)
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Dir(p), 0o700); err != nil {
		return err
	}
	f, err := os.CreateTemp(filepath.Dir(p), ".up-*.part")
	if err != nil {
		return err
	}
	_, werr := f.Write(data)
	cerr := f.Close()
	if werr == nil {
		werr = cerr
	}
	if werr != nil {
		os.Remove(f.Name())
		return werr
	}
	return os.Rename(f.Name(), p)
}

// StorageGet reads a file and its media type.
func StorageGet(key string) ([]byte, string, error) {
	p, err := FilePath(key)
	if err != nil {
		return nil, "", err
	}
	b, err := os.ReadFile(p)
	if err != nil {
		return nil, "", err
	}
	return b, mimeOf(key), nil
}

// StorageDelete removes a file (an error when there is none, as on the hosted platform).
func StorageDelete(key string) error {
	p, err := FilePath(key)
	if err != nil {
		return err
	}
	return os.Remove(p)
}

// FileBytes reads a stored file.
func FileBytes(key string) ([]byte, error) {
	b, _, err := StorageGet(key)
	return b, err
}

func signature(key, method string, exp int64, maxBytes int64) string {
	mac := hmac.New(sha256.New, signKey)
	fmt.Fprintf(mac, "%s\n%s\n%d\n%d", method, key, exp, maxBytes)
	return hex.EncodeToString(mac.Sum(nil))
}

// SignURL makes a link that reads (GET) or writes (PUT) one file until it expires.
func SignURL(base, key, method string, ttl time.Duration, maxBytes int64) string {
	if method == "" {
		method = "GET"
	}
	exp := time.Now().Add(ttl).Unix()
	q := url.Values{}
	q.Set("e", strconv.FormatInt(exp, 10))
	q.Set("m", method)
	if maxBytes > 0 {
		q.Set("x", strconv.FormatInt(maxBytes, 10))
	}
	q.Set("s", signature(key, method, exp, maxBytes))
	return strings.TrimRight(base, "/") + "/files/" + escapeKey(key) + "?" + q.Encode()
}

func escapeKey(key string) string {
	parts := strings.Split(key, "/")
	for i, p := range parts {
		parts[i] = url.PathEscape(p)
	}
	return strings.Join(parts, "/")
}

// CheckSigned verifies a signed link; it returns the byte limit for a PUT.
func CheckSigned(key, method string, q url.Values) (int64, error) {
	exp, _ := strconv.ParseInt(q.Get("e"), 10, 64)
	maxBytes, _ := strconv.ParseInt(q.Get("x"), 10, 64)
	if q.Get("m") != method {
		return 0, errors.New("wrong method for this link")
	}
	if exp < time.Now().Unix() {
		return 0, errors.New("link expired")
	}
	want := signature(key, method, exp, maxBytes)
	if !hmac.Equal([]byte(want), []byte(q.Get("s"))) {
		return 0, errors.New("bad signature")
	}
	return maxBytes, nil
}
