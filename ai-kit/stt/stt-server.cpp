// stt-server: OpenAI-style speech-to-text on the whisper.cpp library (Metal on Apple Silicon).
//
//   stt-server --model models/stt/ggml-large-v3-turbo-q5_0.bin --vad-model models/stt/ggml-silero-v6.2.0.bin \
//              --port 9501 --language vi
//
//   POST /v1/audio/transcriptions   multipart: file (WAV), model (ignored), language, prompt,
//                                   response_format json | text | verbose_json, temperature
//                                   (a bare WAV body works too)
//   GET  /health, GET /info         liveness, and what the Go gateway reads at start-up
//   POST /warm                      transcribe half a second of silence (keeps the weights paged in)
//
// WAV in: 8/16/24/32-bit PCM or 32-bit float, any rate, mono or stereo (mixed down, resampled to 16 kHz).
// "--slots N" whisper states share one loaded model, so N recordings are transcribed at the same time.
// With --vad-model, Silero VAD drops the silence first: faster, and whisper does not invent words in silence.
// Whisper still sometimes "hears" a YouTube outro in noise ("Hãy subscribe cho kênh ..."): such a transcript
// comes back as "" (HALLUCINATIONS below).
//
// Auth: like llm-server. WORKER_TOKEN set → X-Worker-Token; else --api-key → Bearer; else open (127.0.0.1).

#include "whisper.h"

#include "httplib.h"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::ordered_json;

static double now_ms() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

static void logf(const char * fmt, ...) {
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf);
    fflush(stderr);
}

struct Options {
    std::string model;
    std::string vad_model;
    std::string alias    = "whisper";
    std::string host     = "127.0.0.1";
    int         port     = 9501;
    int         slots    = 1;
    int         threads  = 4;
    int         beam     = 1;       // 1 = greedy (fast); 5 = beam search (slower, a little more accurate)
    std::string language = "vi";    // "auto" detects
    std::string prompt;             // a default initial prompt (a style hint)
    int         audio_ctx = -1;     // encoder frames (20 ms each, 1500 = 30 s); -1 = fit the clip, 0 = always 1500
    bool        gpu      = true;
    std::string api_key;
};

static void usage() {
    fprintf(stderr,
        "usage: stt-server --model ggml-*.bin [options]\n"
        "  --vad-model FILE    Silero VAD (ggml-silero-*.bin): skip silence\n"
        "  --alias NAME        model id shown to clients (default: whisper)\n"
        "  --host H --port P   listen address (default 127.0.0.1:9501)\n"
        "  --slots N           recordings transcribed at the same time (default 1)\n"
        "  --threads N         CPU threads per recording (default 4)\n"
        "  --language L        default language, ISO 639-1, or auto (default vi)\n"
        "  --beam N            beam size; 1 = greedy (default 1)\n"
        "  --prompt TEXT       default initial prompt\n"
        "  --audio-ctx N       encoder frames (50 per second): -1 fits the clip, at least 768 (default), 0 = 30 s\n"
        "  --no-gpu            CPU only\n"
        "  --api-key KEY       require Authorization: Bearer KEY (when WORKER_TOKEN is not set)\n");
}

static bool parse_args(int argc, char ** argv, Options & o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--model" || a == "-m")     o.model = next();
        else if (a == "--vad-model")         o.vad_model = next();
        else if (a == "--alias")             o.alias = next();
        else if (a == "--host")              o.host = next();
        else if (a == "--port")              o.port = std::stoi(next());
        else if (a == "--slots")             o.slots = std::max(1, std::stoi(next()));
        else if (a == "--threads")           o.threads = std::max(1, std::stoi(next()));
        else if (a == "--language")          o.language = next();
        else if (a == "--beam")              o.beam = std::max(1, std::stoi(next()));
        else if (a == "--prompt")            o.prompt = next();
        else if (a == "--audio-ctx")         o.audio_ctx = std::stoi(next());
        else if (a == "--no-gpu")            o.gpu = false;
        else if (a == "--api-key")           o.api_key = next();
        else if (a == "--help" || a == "-h") { usage(); exit(0); }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return false; }
    }
    if (o.model.empty()) { usage(); return false; }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// WAV → 16 kHz mono float

static bool decode_wav(const std::string & data, std::vector<float> & out, int & rate_in, std::string & err) {
    auto u16 = [&](size_t p) { return (uint32_t)(uint8_t)data[p] | (uint32_t)(uint8_t)data[p + 1] << 8; };
    auto u32 = [&](size_t p) { return u16(p) | u16(p + 2) << 16; };
    if (data.size() < 12 || data.compare(0, 4, "RIFF") != 0 || data.compare(8, 4, "WAVE") != 0) {
        err = "not a WAV file (RIFF/WAVE): send 16-bit PCM WAV";
        return false;
    }
    int fmt = 0, channels = 0, bits = 0;
    rate_in = 0;
    size_t pos = 12, data_at = 0, data_len = 0;
    while (pos + 8 <= data.size()) {
        std::string id = data.substr(pos, 4);
        size_t len = u32(pos + 4), body = pos + 8;
        if (id == "fmt " && body + 16 <= data.size()) {
            fmt      = (int)u16(body);
            channels = (int)u16(body + 2);
            rate_in  = (int)u32(body + 4);
            bits     = (int)u16(body + 14);
            if (fmt == 0xFFFE && len >= 26 && body + 26 <= data.size()) fmt = (int)u16(body + 24);   // WAVE_FORMAT_EXTENSIBLE
        } else if (id == "data") {
            data_at  = body;
            data_len = (len == 0 || len == 0xFFFFFFFF || body + len > data.size()) ? data.size() - body : len;   // a streamed WAV
            break;
        }
        pos = body + len + (len & 1);
    }
    if (!data_at || channels < 1 || rate_in < 4000 || rate_in > 192000) { err = "a WAV without a usable fmt/data chunk"; return false; }
    bool flt = fmt == 3 && bits == 32;
    if (!flt && !(fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))) {
        err = "unsupported WAV encoding (format " + std::to_string(fmt) + ", " + std::to_string(bits) + " bit)";
        return false;
    }
    size_t bps = bits / 8, frames = data_len / (bps * channels);
    std::vector<float> mono(frames);
    const uint8_t * p = (const uint8_t *)data.data() + data_at;
    for (size_t f = 0; f < frames; f++) {
        float sum = 0;
        for (int c = 0; c < channels; c++, p += bps) {
            float v;
            if (flt)             { float x; memcpy(&x, p, 4); v = x; }
            else if (bits == 8)  v = (p[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t)(p[0] | p[1] << 8) / 32768.0f;
            else if (bits == 24) v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.0f;
            else                 v = (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24) / 2147483648.0f;
            sum += v;
        }
        mono[f] = sum / channels;
    }
    const int rate = WHISPER_SAMPLE_RATE;
    if (rate_in == rate) { out.swap(mono); return true; }
    // Resample: down = the average over each output step (a crude low-pass), up = linear.
    double step = (double)rate_in / rate;
    size_t n = (size_t)(frames / step);
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        double x = i * step;
        size_t a = (size_t)x;
        if (step > 1.0) {
            size_t b = std::min(frames, (size_t)(x + step));
            double s = 0;
            for (size_t k = a; k < b; k++) s += mono[k];
            out[i] = b > a ? (float)(s / (b - a)) : mono[std::min(a, frames - 1)];
        } else {
            double t = x - a;
            float va = mono[std::min(a, frames - 1)], vb = mono[std::min(a + 1, frames - 1)];
            out[i] = (float)(va + (vb - va) * t);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Whisper's well-known inventions in near-silence: outros of the Vietnamese YouTube videos it learned from.

static const char * HALLUCINATIONS[] = {
    "ghiền mì gõ", "subscribe", "đăng ký kênh", "đăng kí kênh", "la la school", "like và share", "like share",
    "nhấn like", "bấm like", "cảm ơn các bạn đã theo dõi", "hẹn gặp lại các bạn trong những video tiếp theo",
    "chúc các bạn xem video vui vẻ",
};

static std::string fold(const std::string & s) {   // ASCII lower case, and the capitals these phrases start with
    std::string o = s;
    for (auto & c : o) c = (char)tolower((unsigned char)c);
    const char * pairs[][2] = {{"Đ", "đ"}, {"Ă", "ă"}, {"Â", "â"}, {"Ê", "ê"}, {"Ô", "ô"}, {"Ơ", "ơ"}, {"Ư", "ư"}, {"Ả", "ả"}, {"Ở", "ở"}};
    for (auto & pr : pairs) {
        size_t at;
        while ((at = o.find(pr[0])) != std::string::npos) o.replace(at, strlen(pr[0]), pr[1]);
    }
    return o;
}

static bool hallucination(const std::string & text) {
    std::string l = fold(text);
    for (const char * h : HALLUCINATIONS) if (l.find(h) != std::string::npos) return true;
    return false;
}

static std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------------------------------------------------------

static Options g_opt;
static whisper_context * g_ctx = nullptr;
static std::vector<whisper_state *> g_states;
static std::vector<bool> g_busy;
static std::mutex g_mu;
static std::condition_variable g_cv;
static std::string g_worker_token;
static std::atomic<uint64_t> g_requests{0};
static double g_audio_s = 0, g_proc_ms = 0;

// One whisper state for the length of a request; waits for a free one.
struct StateLease {
    int i = -1;
    StateLease() {
        std::unique_lock<std::mutex> lk(g_mu);
        g_cv.wait(lk, [] { for (bool b : g_busy) if (!b) return true; return false; });
        for (size_t k = 0; k < g_busy.size(); k++) if (!g_busy[k]) { i = (int)k; g_busy[k] = true; break; }
    }
    ~StateLease() {
        std::lock_guard<std::mutex> lk(g_mu);
        g_busy[i] = false;
        g_cv.notify_one();
    }
    whisper_state * get() const { return g_states[i]; }
};

struct Result {
    std::string text, language;
    json   segments = json::array();
    bool   filtered = false, failed = false;
    double wait_ms = 0, ms = 0;
};

static Result transcribe(const std::vector<float> & pcm, const std::string & language, const std::string & prompt, float temperature) {
    Result r;
    double t0 = now_ms();
    StateLease lease;
    r.wait_ms = now_ms() - t0;
    whisper_full_params p = whisper_full_default_params(g_opt.beam > 1 ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    p.n_threads        = g_opt.threads;
    p.print_progress   = false;
    p.print_realtime   = false;
    p.print_special    = false;
    p.print_timestamps = false;
    p.no_context       = true;
    p.suppress_nst     = true;
    p.language         = language.empty() ? "auto" : language.c_str();
    p.detect_language  = false;
    p.temperature      = temperature;
    p.beam_search.beam_size = g_opt.beam;
    // Whisper's encoder always works on 30 s; a 3 s question does not need that. A window fitted to the clip makes
    // the encoder about 2x faster on the short questions an assistant hears. Measured (bench/speech_bench.py, 42
    // Vietnamese questions): 768 frames (15 s) = the same error rate as 30 s; 512 and less = more errors.
    double dur = pcm.size() / (double)WHISPER_SAMPLE_RATE;
    if (g_opt.audio_ctx > 0) p.audio_ctx = g_opt.audio_ctx;
    else if (g_opt.audio_ctx < 0 && dur < 28.0) p.audio_ctx = std::min(1500, std::max(768, (int)(dur * 50) + 128));
    std::string pr = prompt.empty() ? g_opt.prompt : prompt;
    if (!pr.empty()) p.initial_prompt = pr.c_str();
    if (!g_opt.vad_model.empty()) {
        p.vad = true;
        p.vad_model_path = g_opt.vad_model.c_str();
        p.vad_params = whisper_vad_default_params();
    }
    whisper_state * st = lease.get();
    if (whisper_full_with_state(g_ctx, st, p, pcm.data(), (int)pcm.size()) != 0) {
        r.failed = true;
        r.ms = now_ms() - t0;
        return r;
    }
    int n = whisper_full_n_segments_from_state(st);
    for (int i = 0; i < n; i++) {
        std::string s = whisper_full_get_segment_text_from_state(st, i);
        r.text += s;
        r.segments.push_back({{"id", i}, {"start", whisper_full_get_segment_t0_from_state(st, i) / 100.0},
                              {"end", whisper_full_get_segment_t1_from_state(st, i) / 100.0}, {"text", trim(s)},
                              {"no_speech_prob", whisper_full_get_segment_no_speech_prob_from_state(st, i)}});
    }
    r.text = trim(r.text);
    int lang = whisper_full_lang_id_from_state(st);
    r.language = lang >= 0 ? whisper_lang_str(lang) : language;
    if (!r.text.empty() && hallucination(r.text)) {
        r.filtered = true;
        r.text.clear();
    }
    r.ms = now_ms() - t0;
    return r;
}

static void send_json(httplib::Response & res, int status, const json & body) {
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, json::error_handler_t::replace), "application/json; charset=utf-8");
}

static void send_error(httplib::Response & res, int status, const std::string & msg, const char * type = "invalid_request_error") {
    send_json(res, status, {{"error", {{"message", msg}, {"type", type}, {"code", status}}}});
}

static void handle_transcribe(const httplib::Request & req, httplib::Response & res) {
    double t0 = now_ms();
    std::string audio;
    if (req.is_multipart_form_data()) {
        if (!req.has_file("file")) return send_error(res, 400, "missing the 'file' field (a WAV recording)");
        audio = req.get_file_value("file").content;
    } else {
        audio = req.body;
    }
    auto field = [&](const char * k, const std::string & def) {
        return req.has_file(k) ? req.get_file_value(k).content : req.has_param(k) ? req.get_param_value(k) : def;
    };
    std::string language = field("language", g_opt.language);
    std::string prompt   = field("prompt", "");
    std::string format   = field("response_format", "json");
    float temperature = 0.0f;
    try { temperature = std::stof(field("temperature", "0")); } catch (...) {}
    if (format != "json" && format != "text" && format != "verbose_json")
        return send_error(res, 400, "response_format must be json, text or verbose_json");

    std::vector<float> pcm;
    int rate_in = 0;
    std::string err;
    if (!decode_wav(audio, pcm, rate_in, err)) return send_error(res, 400, err);
    double audio_s = pcm.size() / (double)WHISPER_SAMPLE_RATE;
    if (audio_s < 0.1) return send_error(res, 400, "the recording is shorter than 0.1 s");
    if (audio_s > 600) return send_error(res, 400, "the recording is longer than 10 minutes");

    Result r = transcribe(pcm, language, prompt, temperature);
    if (r.failed) return send_error(res, 500, "transcription failed", "server_error");
    double total = now_ms() - t0;
    g_requests++;
    { std::lock_guard<std::mutex> lk(g_mu); g_audio_s += audio_s; g_proc_ms += r.ms; }
    logf("stt audio=%.2fs rate=%d lang=%s wait=%.0fms process=%.0fms total=%.0fms rtf=%.3f%s chars=%zu",
         audio_s, rate_in, r.language.c_str(), r.wait_ms, r.ms - r.wait_ms, total, (r.ms - r.wait_ms) / 1000 / audio_s,
         r.filtered ? " filtered" : "", r.text.size());

    res.set_header("X-Process-Ms", std::to_string((int)(r.ms - r.wait_ms)));
    res.set_header("X-Audio-Seconds", std::to_string(audio_s));
    if (format == "text") {
        res.status = 200;
        res.set_content(r.text, "text/plain; charset=utf-8");
        return;
    }
    json out = {{"text", r.text}};
    if (format == "verbose_json") {
        out["task"]     = "transcribe";
        out["language"] = r.language;
        out["duration"] = audio_s;
        out["segments"] = r.segments;
        out["filtered"] = r.filtered;
        out["timings"]  = {{"wait_ms", std::round(r.wait_ms)}, {"process_ms", std::round(r.ms - r.wait_ms)},
                           {"total_ms", std::round(total)}, {"rtf", (r.ms - r.wait_ms) / 1000 / audio_s}};
    }
    send_json(res, 200, out);
}

int main(int argc, char ** argv) {
    if (!parse_args(argc, argv, g_opt)) return 2;
    if (const char * tok = getenv("WORKER_TOKEN")) g_worker_token = tok;
    whisper_log_set([](ggml_log_level level, const char * text, void *) {
        if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) fputs(text, stderr);
    }, nullptr);

    double t0 = now_ms();
    whisper_context_params cp = whisper_context_default_params();
    cp.use_gpu    = g_opt.gpu;
    cp.flash_attn = g_opt.gpu;
    g_ctx = whisper_init_from_file_with_params_no_state(g_opt.model.c_str(), cp);
    if (!g_ctx) { logf("cannot load the model %s", g_opt.model.c_str()); return 1; }
    for (int i = 0; i < g_opt.slots; i++) {
        whisper_state * st = whisper_init_state(g_ctx);
        if (!st) { logf("cannot create whisper state %d", i); return 1; }
        g_states.push_back(st);
        g_busy.push_back(false);
    }
    std::vector<float> tone(WHISPER_SAMPLE_RATE);   // compile the GPU pipelines once per state
    for (size_t i = 0; i < tone.size(); i++) tone[i] = 0.1f * sinf(i * 0.05f);
    for (int i = 0; i < g_opt.slots; i++) transcribe(tone, g_opt.language, "", 0.0f);
    logf("loaded %s (%s) in %.1f s: %d slot(s), language %s, beam %d, VAD %s",
         g_opt.model.c_str(), g_opt.alias.c_str(), (now_ms() - t0) / 1000, g_opt.slots, g_opt.language.c_str(), g_opt.beam,
         g_opt.vad_model.empty() ? "off" : "on");

    httplib::Server svr;
    int threads = std::max(8, g_opt.slots * 4);
    svr.new_task_queue = [threads] { return new httplib::ThreadPool(threads); };
    svr.set_read_timeout(60, 0);
    svr.set_write_timeout(60, 0);
    svr.set_payload_max_length(50 * 1024 * 1024);

    svr.set_pre_routing_handler([](const httplib::Request & req, httplib::Response & res) {
        if (req.path == "/health") return httplib::Server::HandlerResponse::Unhandled;
        bool ok = true;
        if (!g_worker_token.empty()) ok = req.get_header_value("X-Worker-Token") == g_worker_token;
        else if (!g_opt.api_key.empty()) ok = req.get_header_value("Authorization") == "Bearer " + g_opt.api_key;
        if (ok) return httplib::Server::HandlerResponse::Unhandled;
        send_error(res, 401, "missing or invalid token", "authentication_error");
        return httplib::Server::HandlerResponse::Handled;
    });
    svr.Get("/health", [](const httplib::Request &, httplib::Response & res) { send_json(res, 200, {{"status", "ok"}}); });
    svr.Get("/info", [](const httplib::Request &, httplib::Response & res) {
        send_json(res, 200, {{"engine", "stt"}, {"model", g_opt.alias}, {"default_voice", ""}, {"voices", json::array()},
                             {"slots", g_opt.slots}, {"language", g_opt.language}, {"vad", !g_opt.vad_model.empty()}});
    });
    svr.Get("/stats", [](const httplib::Request &, httplib::Response & res) {
        std::lock_guard<std::mutex> lk(g_mu);
        send_json(res, 200, {{"requests", g_requests.load()}, {"audio_seconds", g_audio_s}, {"process_ms", g_proc_ms}});
    });
    svr.Post("/v1/audio/transcriptions", handle_transcribe);
    svr.Post("/audio/transcriptions", handle_transcribe);
    svr.Post("/warm", [](const httplib::Request &, httplib::Response & res) {
        std::vector<float> s(WHISPER_SAMPLE_RATE / 2, 0.0f);
        Result r = transcribe(s, g_opt.language, "", 0.0f);
        send_json(res, 200, {{"ok", !r.failed}, {"ms", std::round(r.ms)}});
    });

    logf("listening on http://%s:%d", g_opt.host.c_str(), g_opt.port);
    if (!svr.listen(g_opt.host, g_opt.port)) { logf("cannot listen on %s:%d", g_opt.host.c_str(), g_opt.port); return 1; }
    for (auto * st : g_states) whisper_free_state(st);
    whisper_free(g_ctx);
    return 0;
}
