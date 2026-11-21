package engine

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"strings"

	"espos/internal/wm"
)

// Outbound requests from a flow: the http_request node (the hosted platform's, used by the dog assistant), and the
// tts node (tts.go), which the importer puts in place of the hosted speech service so a self-hosted server can use
// its own (README: "The dog assistant"). A streamed answer is passed through to a respond node with stream: true.

var streamClient = &http.Client{Timeout: 0} // a streamed body may take a while; the caller's context ends it

const streamKey = "\x00stream" // where a node leaves the body a stream: true respond sends on

// httpRequest runs an http_request node: {method, url, headers, body, stream}. Without stream it leaves {status,
// headers, body} for the next nodes; with stream the body stays open for the respond node.
func httpRequest(cfg map[string]any, vars map[string]any) (map[string]any, io.ReadCloser, error) {
	method := strings.ToUpper(wm.Str(eval(cfg["method"], vars)))
	if method == "" {
		method = "GET"
	}
	url := wm.FillSecrets(wm.Str(eval(cfg["url"], vars)))
	headers, _ := eval(cfg["headers"], vars).(map[string]any)
	body := eval(cfg["body"], vars)
	if !wm.Bool(cfg["stream"]) {
		out, err := wm.FetchWith(method, url, body, headers)
		return out, nil, err
	}
	var rd io.Reader
	if body != nil {
		if s, ok := body.(string); ok {
			rd = strings.NewReader(s)
		} else {
			b, err := json.Marshal(body)
			if err != nil {
				return nil, nil, err
			}
			rd = bytes.NewReader(b)
		}
	}
	req, err := http.NewRequest(method, url, rd)
	if err != nil {
		return nil, nil, err
	}
	req.Header.Set("User-Agent", wm.UserAgent)
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	for k, v := range headers {
		req.Header.Set(k, wm.FillSecrets(wm.Str(v)))
	}
	res, err := streamClient.Do(req)
	if err != nil {
		return nil, nil, err
	}
	out := map[string]any{"status": float64(res.StatusCode)}
	if res.StatusCode >= 300 {
		msg, _ := io.ReadAll(io.LimitReader(res.Body, 2048))
		res.Body.Close()
		return out, nil, fmt.Errorf("%s answered %d: %s", url, res.StatusCode, strings.TrimSpace(string(msg)))
	}
	return out, res.Body, nil
}

// wavBody is a WAV file's PCM after its header (16-bit mono expected).
type wavBody struct {
	io.ReadCloser
	rate int
}

func skipWav(rc io.ReadCloser) (*wavBody, error) {
	var head [12]byte
	if _, err := io.ReadFull(rc, head[:]); err != nil || string(head[0:4]) != "RIFF" || string(head[8:12]) != "WAVE" {
		return nil, errors.New("speech service: not a WAV answer")
	}
	rate := 24000
	for {
		var ch [8]byte
		if _, err := io.ReadFull(rc, ch[:]); err != nil {
			return nil, errors.New("speech service: WAV without data")
		}
		size := binary.LittleEndian.Uint32(ch[4:8])
		switch string(ch[0:4]) {
		case "fmt ":
			f := make([]byte, size)
			if _, err := io.ReadFull(rc, f); err != nil || size < 16 {
				return nil, errors.New("speech service: bad WAV format block")
			}
			if ch := binary.LittleEndian.Uint16(f[2:4]); ch != 1 || binary.LittleEndian.Uint16(f[14:16]) != 16 {
				return nil, errors.New("speech service: the WAV must be 16-bit mono")
			}
			rate = int(binary.LittleEndian.Uint32(f[4:8]))
		case "data":
			return &wavBody{ReadCloser: rc, rate: rate}, nil
		default:
			if _, err := io.CopyN(io.Discard, rc, int64(size)); err != nil {
				return nil, err
			}
		}
	}
}

// resampler turns 16-bit mono PCM at `in` Hz into `out` Hz as it streams (linear interpolation; speech only).
type resampler struct {
	src     io.ReadCloser
	in, out int
	buf     []int16 // input samples not yet passed
	pos     float64 // the next output sample's place in buf
	odd     []byte  // a half sample left from the last read
	eof     bool
	pending []byte
	scratch [4096]byte
}

func (r *resampler) Read(p []byte) (int, error) {
	for len(r.pending) == 0 {
		if r.eof && len(r.buf) < 2 {
			return 0, io.EOF
		}
		if !r.eof {
			n, err := r.src.Read(r.scratch[:])
			data := append(r.odd, r.scratch[:n]...)
			r.odd = nil
			if len(data)%2 == 1 {
				r.odd = []byte{data[len(data)-1]}
				data = data[:len(data)-1]
			}
			for i := 0; i+1 < len(data); i += 2 {
				r.buf = append(r.buf, int16(binary.LittleEndian.Uint16(data[i:])))
			}
			if err == io.EOF {
				r.eof = true
			} else if err != nil {
				return 0, err
			}
		}
		step := float64(r.in) / float64(r.out)
		var out []byte
		for r.pos+1 < float64(len(r.buf)) {
			i := int(r.pos)
			f := r.pos - float64(i)
			v := float64(r.buf[i])*(1-f) + float64(r.buf[i+1])*f
			out = binary.LittleEndian.AppendUint16(out, uint16(int16(v)))
			r.pos += step
		}
		if drop := int(r.pos); drop > 0 && drop <= len(r.buf) {
			r.buf = r.buf[drop:]
			r.pos -= float64(drop)
		}
		if r.eof && len(out) == 0 {
			r.buf = nil
		}
		r.pending = out
	}
	n := copy(p, r.pending)
	r.pending = r.pending[n:]
	return n, nil
}

func (r *resampler) Close() error { return r.src.Close() }
