package engine

import (
	"bytes"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"
	"time"

	"espos/internal/wm"
)

// Outbound requests from a flow: the http_request node (the hosted platform's, used by the dog assistant), and the
// tts node, which the importer puts in place of the hosted speech service so a self-hosted server can use its own
// (README: "The dog assistant"). A streamed answer is passed through to a respond node with stream: true.

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

// ---- the tts node: the dog's voice from the speech service set in the environment ----
//
//	ESPOS_TTS_URL    the service: an OpenAI-style base (https://api.openai.com/v1, or any server with
//	                 POST /audio/speech), or, with ESPOS_TTS_API=espos, the full URL of a hosted-style /v1/tts
//	ESPOS_TTS_API    openai (default) or espos
//	ESPOS_TTS_KEY    its key (sent as "Authorization: Bearer"), if it needs one
//	ESPOS_TTS_MODEL  openai: gpt-4o-mini-tts (default); espos: vieneu-turbo
//	ESPOS_TTS_VOICE  openai: alloy (default); espos: the dog's own voice unless this is set
//	ESPOS_TTS_FORMAT openai only: pcm (default, 16-bit mono at ESPOS_TTS_RATE) or wav
//	ESPOS_TTS_RATE   openai pcm only: its sample rate, 24000 (OpenAI's)
//
// The device asks for 16-bit mono PCM at its own rate (16000 on the S3); an OpenAI-style answer is resampled to it as
// it streams. Without ESPOS_TTS_URL the node fails, and the flows answer with text only.

// TTSConfigured says whether a speech service is set.
func TTSConfigured() bool { return wm.Env("ESPOS_TTS_URL", "") != "" }

func ttsStream(text, voice string, rate int) (io.ReadCloser, error) {
	base := wm.Env("ESPOS_TTS_URL", "")
	if base == "" {
		return nil, errors.New("no speech service set (ESPOS_TTS_URL)")
	}
	if rate <= 0 {
		rate = 16000
	}
	key := os.Getenv("ESPOS_TTS_KEY")
	api := strings.ToLower(wm.Env("ESPOS_TTS_API", "openai"))
	var url string
	var body map[string]any
	inRate, wav := rate, false
	switch api {
	case "espos":
		url = base
		if v := wm.Env("ESPOS_TTS_VOICE", ""); v != "" {
			voice = v
		}
		body = map[string]any{"model": wm.Env("ESPOS_TTS_MODEL", "vieneu-turbo"), "voice": voice, "text": text,
			"format": "pcm", "sample_rate": rate, "stream": true}
	case "openai":
		url = strings.TrimRight(base, "/")
		if !strings.HasSuffix(url, "/audio/speech") {
			url += "/audio/speech"
		}
		format := strings.ToLower(wm.Env("ESPOS_TTS_FORMAT", "pcm"))
		wav = format == "wav"
		inRate = int(wm.Num(wm.Env("ESPOS_TTS_RATE", "24000")))
		body = map[string]any{"model": wm.Env("ESPOS_TTS_MODEL", "gpt-4o-mini-tts"), "voice": wm.Env("ESPOS_TTS_VOICE", "alloy"),
			"input": text, "response_format": format}
	default:
		return nil, fmt.Errorf("ESPOS_TTS_API %q: use openai or espos", api)
	}
	b, _ := json.Marshal(body)
	req, err := http.NewRequest("POST", url, bytes.NewReader(b))
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", wm.UserAgent)
	req.Header.Set("Content-Type", "application/json")
	if key != "" {
		req.Header.Set("Authorization", "Bearer "+key)
	}
	client := &http.Client{Timeout: 60 * time.Second}
	res, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	if res.StatusCode >= 300 {
		msg, _ := io.ReadAll(io.LimitReader(res.Body, 2048))
		res.Body.Close()
		return nil, fmt.Errorf("speech service answered %d: %s", res.StatusCode, strings.TrimSpace(string(msg)))
	}
	if api == "espos" {
		return res.Body, nil
	}
	src := io.ReadCloser(res.Body)
	if wav {
		r, err := skipWav(res.Body)
		if err != nil {
			res.Body.Close()
			return nil, err
		}
		src, inRate = r, r.rate
	}
	if inRate == rate {
		return src, nil
	}
	return &resampler{src: src, in: inRate, out: rate}, nil
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
