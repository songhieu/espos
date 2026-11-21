// llm-server: an OpenAI-style chat server for one GGUF chat model (Gemma 4), on the llama.cpp library.
//
//   llm-server --model models/llm/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf --port 9401 --slots 2 --ctx 16384
//
//   POST /v1/chat/completions   messages, temperature, top_p, top_k, min_p, max_tokens, stop, seed, stream (SSE),
//                               response_format {"type":"json_object"} (forced by a JSON grammar)
//   GET  /v1/models             the one model
//   GET  /health, GET /info     liveness, and what the Go gateway reads at start-up
//   GET  /stats                 slots, queue, prefix-cache counters
//   POST /warm                  one forward pass on a spare sequence (keeps the weights paged in)
//
// How it works, in one screen:
//   * One llama_context, one decode thread. "--slots N" sequences share one KV buffer (kv_unified), so N requests
//     are decoded together: every step feeds one token per generating slot plus a chunk of a waiting prompt.
//   * Prefix cache: each slot remembers the tokens its sequence holds in the KV cache. A new request goes to the
//     idle slot sharing the longest prefix with it; if another slot (even a busy one) shares a longer prefix, those
//     cells are copied (llama_memory_seq_cp, no recompute). The dog assistant's fixed rules come first in its
//     prompt, so only the changing tail (data, conversation, question) is prefilled. Gemma 4 has sliding-window
//     layers: swa_full keeps their cache whole so any prefix can be reused (cheap: ~15 KB per token for E2B).
//   * Requests wait in a bounded queue; past --max-queue the server answers 429.
//   * The prompt is Gemma 4's own chat format (gemma4_prompt). Message text is tokenized with special tokens off,
//     so a "<|turn>" typed by a user stays text. llm/check_template.py compares the format with the GGUF's
//     Jinja template.
//
// Auth: with WORKER_TOKEN set (the Go gateway starts it that way) every request needs X-Worker-Token; else with
// --api-key it needs "Authorization: Bearer <key>"; else it is open (it listens on 127.0.0.1 by default).

#include "llama.h"

#include "cpp-httplib/httplib.h"
#include "nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <utility>
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

// ---------------------------------------------------------------------------------------------------------------
// Options

struct Options {
    std::string model;
    std::string alias;               // the id clients see; default: the file name without .gguf
    std::string host       = "127.0.0.1";
    int         port       = 9401;
    int         slots      = 2;      // requests decoded together
    int         ctx        = 16384;  // tokens per request (prompt + answer)
    int         batch      = 2048;   // tokens per decode step (the prompt chunk)
    int         ubatch     = 512;
    int         threads    = 4;      // CPU threads (sampling; the GPU runs the model)
    int         gpu_layers = 999;
    int         max_queue  = 16;     // waiting requests before 429
    int         http_threads = 32;
    bool        prefix_cache = true;
    bool        swa_full   = true;
    std::string api_key;
    std::string dump_dir;            // write every rendered prompt here (debugging prompts)
};

static void usage() {
    fprintf(stderr,
        "usage: llm-server --model FILE.gguf [options]\n"
        "  --alias NAME        model id shown to clients (default: the file name)\n"
        "  --host H --port P   listen address (default 127.0.0.1:9401)\n"
        "  --slots N           requests decoded at the same time (default 2)\n"
        "  --ctx N             tokens per request, prompt + answer (default 16384)\n"
        "  --batch N           prompt tokens per step (default 2048)\n"
        "  --threads N         CPU threads (default 4)\n"
        "  --gpu-layers N      layers on the GPU (default: all)\n"
        "  --max-queue N       waiting requests before answering 429 (default 16)\n"
        "  --no-prefix-cache   always prefill the whole prompt (for benchmarks)\n"
        "  --api-key KEY       require Authorization: Bearer KEY (when WORKER_TOKEN is not set)\n"
        "  --dump-prompts DIR  write each rendered prompt to DIR/<id>.txt (debugging)\n");
}

static bool parse_args(int argc, char ** argv, Options & o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", a.c_str()); exit(2); }
            return argv[++i];
        };
        if (a == "--model" || a == "-m")     o.model = next();
        else if (a == "--alias")             o.alias = next();
        else if (a == "--host")              o.host = next();
        else if (a == "--port")              o.port = std::stoi(next());
        else if (a == "--slots")             o.slots = std::max(1, std::stoi(next()));
        else if (a == "--ctx")               o.ctx = std::max(512, std::stoi(next()));
        else if (a == "--batch")             o.batch = std::max(32, std::stoi(next()));
        else if (a == "--ubatch")            o.ubatch = std::max(32, std::stoi(next()));
        else if (a == "--threads")           o.threads = std::max(1, std::stoi(next()));
        else if (a == "--gpu-layers")        o.gpu_layers = std::stoi(next());
        else if (a == "--max-queue")         o.max_queue = std::max(0, std::stoi(next()));
        else if (a == "--http-threads")      o.http_threads = std::max(4, std::stoi(next()));
        else if (a == "--no-prefix-cache")   o.prefix_cache = false;
        else if (a == "--no-swa-full")       o.swa_full = false;
        else if (a == "--api-key")           o.api_key = next();
        else if (a == "--dump-prompts")      o.dump_dir = next();
        else if (a == "--help" || a == "-h") { usage(); exit(0); }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return false; }
    }
    if (o.model.empty()) { usage(); return false; }
    if (o.alias.empty()) {
        std::string f = o.model.substr(o.model.find_last_of('/') + 1);
        if (f.size() > 5 && f.compare(f.size() - 5, 5, ".gguf") == 0) f.resize(f.size() - 5);
        o.alias = f;
    }
    o.ubatch = std::min(o.ubatch, o.batch);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Gemma 4 chat format (the GGUF's tokenizer.chat_template, text messages only):
//   <bos><|turn>system\n{system}<turn|>\n<|turn>user\n{text}<turn|>\n<|turn>model\n{text}<turn|>\n ... <|turn>model\n
// "enable_thinking" puts <|think|>\n at the top of the system turn.
// The prompt is a list of parts: markup (special tokens on) and message text (special tokens off).

struct Part {
    std::string text;
    bool        markup;
};

static std::string trim(const std::string & s) {
    size_t a = s.find_first_not_of(" \t\n\r\f\v");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(a, b - a + 1);
}

// The template's strip_thinking: drop <|channel>...<channel|> blocks from an earlier model answer.
static std::string strip_thinking(const std::string & text) {
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t close = text.find("<channel|>", pos);
        std::string part = text.substr(pos, close == std::string::npos ? std::string::npos : close - pos);
        size_t open = part.find("<|channel>");
        out += open == std::string::npos ? part : part.substr(0, open);
        if (close == std::string::npos) break;
        pos = close + strlen("<channel|>");
    }
    return trim(out);
}

// Message content: a string, or OpenAI content parts (text only; images and audio are refused).
static bool content_text(const json & c, bool model_role, std::string & out, std::string & err) {
    out.clear();
    if (c.is_null()) return true;
    if (c.is_string()) {
        out = model_role ? strip_thinking(c.get<std::string>()) : trim(c.get<std::string>());
        return true;
    }
    if (!c.is_array()) { err = "message content must be a string or an array of parts"; return false; }
    for (const auto & part : c) {
        std::string type = part.value("type", "");
        if (type == "text") {
            std::string t = part.value("text", "");
            out += model_role ? strip_thinking(t) : trim(t);
        } else if (type == "input_audio" || type == "audio") {
            err = "this model reads text only: send audio to /v1/audio/transcriptions first";
            return false;
        } else {
            err = "unsupported content part type '" + type + "' (text only)";
            return false;
        }
    }
    return true;
}

static bool gemma4_prompt(const json & messages, bool thinking, std::vector<Part> & out, std::string & err) {
    if (!messages.is_array() || messages.empty()) { err = "messages must be a non-empty array"; return false; }
    out.clear();
    size_t first = 0;
    std::string role0 = messages[0].is_object() ? messages[0].value("role", "") : "";
    if (thinking || role0 == "system" || role0 == "developer") {
        out.push_back({thinking ? "<|turn>system\n<|think|>\n" : "<|turn>system\n", true});
        if (role0 == "system" || role0 == "developer") {
            const json & c = messages[0].contains("content") ? messages[0]["content"] : json();
            std::string sys;
            if (c.is_array()) {          // the template joins system parts with a space
                for (const auto & p : c) sys += trim(p.value("text", "")) + " ";
            } else if (c.is_string()) {
                sys = trim(c.get<std::string>());
            }
            out.push_back({sys, false});
            first = 1;
        }
        out.push_back({"<turn|>\n", true});
    }
    std::string prev_role;
    for (size_t i = first; i < messages.size(); i++) {
        const json & m = messages[i];
        if (!m.is_object()) { err = "each message must be an object"; return false; }
        std::string role = m.value("role", "");
        if (role == "tool" || m.contains("tool_calls")) { err = "tool calls are not supported"; return false; }
        if (role != "user" && role != "assistant" && role != "system" && role != "developer") {
            err = "unknown role '" + role + "'";
            return false;
        }
        std::string r = role == "assistant" ? "model" : role;
        std::string text;
        if (!content_text(m.contains("content") ? m["content"] : json(), r == "model", text, err)) return false;
        // Two assistant messages in a row continue one model turn, as in the template.
        if (r == "model" && prev_role == "assistant" && !out.empty() && out.back().text == "<turn|>\n") {
            out.pop_back();
        } else {
            out.push_back({"<|turn>" + r + "\n", true});
        }
        out.push_back({text, false});
        out.push_back({"<turn|>\n", true});
        prev_role = role;
    }
    out.push_back({"<|turn>model\n", true});
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// A request, and what flows back from the decode thread to the HTTP thread.

static const char * JSON_GRAMMAR = R"GBNF(
root   ::= object
value  ::= object | array | string | number | ("true" | "false" | "null") ws
object ::= "{" ws ( string ":" ws value ("," ws string ":" ws value)* )? "}" ws
array  ::= "[" ws ( value ("," ws value)* )? "]" ws
string ::= "\"" ( [^"\\\x7F\x00-\x1F] | "\\" (["\\bfnrt] | "u" [0-9a-fA-F]{4}) )* "\"" ws
number ::= ("-"? ([0-9] | [1-9] [0-9]{0,15})) ("." [0-9]+)? ([eE] [-+]? [0-9] [1-9]{0,15})? ws
ws     ::= | " " | "\n" [ \t]{0,20}
)GBNF";

struct Task {
    // in
    std::vector<llama_token> prompt;
    int      max_tokens  = 1024;
    float    temperature = 0.7f;
    float    top_p       = 0.95f;
    int      top_k       = 64;
    float    min_p       = 0.0f;
    uint32_t seed        = 0;
    bool     json_mode   = false;
    std::vector<std::string> stop;

    // out, guarded by mu
    std::mutex              mu;
    std::condition_variable cv;
    std::deque<std::string> chunks;     // text not yet taken by the HTTP thread
    bool        finished = false;
    std::string error;
    std::string finish_reason;
    std::string reasoning;              // a <|channel>thought block, if the model wrote one
    int    n_prompt = 0, n_cached = 0, n_gen = 0, slot = -1;
    double t_queued = 0, t_started = 0, t_first = 0, t_end = 0;

    std::atomic<bool> cancelled{false};

    void push(std::string s) {
        std::lock_guard<std::mutex> lk(mu);
        chunks.push_back(std::move(s));
        cv.notify_all();
    }
    void finish(const std::string & reason, const std::string & err = "") {
        std::lock_guard<std::mutex> lk(mu);
        finish_reason = reason;
        error = err;
        finished = true;
        t_end = now_ms();
        if (t_first == 0) t_first = t_end;
        if (t_started == 0) t_started = t_end;
        cv.notify_all();
    }
};

struct Slot {
    int id = 0;                         // sequence id in the KV cache
    std::vector<llama_token> cache;     // tokens whose KV this sequence holds, in order
    std::shared_ptr<Task> task;
    bool        generating = false;
    llama_token next = -1;              // the sampled token to feed in the next step
    int         i_batch = -1;           // where this slot's logits are in the current batch
    llama_sampler * chain = nullptr;
    llama_sampler * grammar = nullptr;
    double      last_used = 0;

    // text state
    std::string text;                   // the visible answer so far
    size_t      sent = 0;               // bytes of text handed to the task
    bool        in_thought = false;
    int         json_depth = 0;
    bool        json_str = false, json_esc = false, json_open = false;

    void free_samplers() {
        if (chain)   { llama_sampler_free(chain);   chain = nullptr; }
        if (grammar) { llama_sampler_free(grammar); grammar = nullptr; }
    }
};

static size_t common_prefix(const std::vector<llama_token> & a, const std::vector<llama_token> & b) {
    size_t n = std::min(a.size(), b.size()), i = 0;
    while (i < n && a[i] == b[i]) i++;
    return i;
}

// The length of s cut back to the end of its last whole UTF-8 character.
static size_t utf8_complete(const std::string & s) {
    size_t n = s.size();
    for (size_t back = 1; back <= 4 && back <= n; back++) {
        unsigned char c = (unsigned char)s[n - back];
        if ((c & 0xC0) == 0x80) continue;   // continuation byte
        size_t need = (c & 0x80) == 0 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        return back >= need ? n : n - back;
    }
    return n;
}

// ---------------------------------------------------------------------------------------------------------------
// The engine: one model, one context, one decode thread.

class Engine {
public:
    Options opt;
    llama_model *       model = nullptr;
    llama_context *     ctx   = nullptr;
    const llama_vocab * vocab = nullptr;
    int n_vocab = 0;
    llama_token tok_channel_open = LLAMA_TOKEN_NULL, tok_channel_close = LLAMA_TOKEN_NULL;
    double   load_ms = 0;
    uint64_t model_bytes = 0, n_params = 0;

    std::atomic<uint64_t> n_requests{0}, n_prompt_tokens{0}, n_cached_tokens{0}, n_gen_tokens{0}, n_rejected{0};

    bool load() {
        double t0 = now_ms();
        llama_backend_init();
        llama_model_params mp = llama_model_default_params();
        mp.n_gpu_layers = opt.gpu_layers;
        model = llama_model_load_from_file(opt.model.c_str(), mp);
        if (!model) { logf("cannot load the model %s", opt.model.c_str()); return false; }
        vocab       = llama_model_get_vocab(model);
        n_vocab     = llama_vocab_n_tokens(vocab);
        model_bytes = llama_model_size(model);
        n_params    = llama_model_n_params(model);

        llama_context_params cp = llama_context_default_params();
        cp.n_ctx           = (uint32_t)opt.ctx * opt.slots;
        cp.n_batch         = opt.batch;
        cp.n_ubatch        = opt.ubatch;
        cp.n_seq_max       = opt.slots + 1;          // + one spare sequence for /warm
        cp.n_threads       = opt.threads;
        cp.n_threads_batch = opt.threads;
        cp.flash_attn_type = LLAMA_FLASH_ATTN_TYPE_AUTO;
        cp.swa_full        = opt.swa_full;
        cp.kv_unified      = true;                   // slots share cells: prefixes can be copied between them
        cp.no_perf         = true;
        ctx = llama_init_from_model(model, cp);
        if (!ctx) { logf("cannot create the context (%d tokens x %d slots)", opt.ctx, opt.slots); return false; }

        auto special = [&](const char * s) -> llama_token {
            llama_token t[4];
            int n = llama_tokenize(vocab, s, (int)strlen(s), t, 4, false, true);
            return n == 1 ? t[0] : LLAMA_TOKEN_NULL;
        };
        tok_channel_open  = special("<|channel>");
        tok_channel_close = special("<channel|>");

        slots.resize(opt.slots);
        slot_busy.reset(new std::atomic<bool>[opt.slots]);
        slot_cached.reset(new std::atomic<int>[opt.slots]);
        for (int i = 0; i < opt.slots; i++) { slots[i].id = i; slot_busy[i] = false; slot_cached[i] = 0; }
        batch = llama_batch_init(opt.batch, 0, 1);
        do_warm();                                   // the first forward pass compiles the Metal pipelines
        load_ms = now_ms() - t0;
        return true;
    }

    std::vector<llama_token> tokenize(const std::vector<Part> & parts) const {
        std::vector<llama_token> out;
        if (llama_vocab_get_add_bos(vocab)) out.push_back(llama_vocab_bos(vocab));
        for (const auto & p : parts) {
            if (p.text.empty()) continue;
            int n = -llama_tokenize(vocab, p.text.data(), (int)p.text.size(), nullptr, 0, false, p.markup);
            if (n <= 0) continue;
            size_t at = out.size();
            out.resize(at + n);
            llama_tokenize(vocab, p.text.data(), (int)p.text.size(), out.data() + at, n, false, p.markup);
        }
        return out;
    }

    std::string piece(llama_token t) const {
        char buf[256];
        int n = llama_token_to_piece(vocab, t, buf, sizeof buf, 0, false);
        if (n >= 0) return std::string(buf, n);
        std::string s(-n, '\0');
        llama_token_to_piece(vocab, t, s.data(), (int)s.size(), 0, false);
        return s;
    }

    // false: the queue is full
    bool submit(const std::shared_ptr<Task> & t) {
        std::lock_guard<std::mutex> lk(qmu);
        int idle = 0;
        for (int i = 0; i < opt.slots; i++) idle += !slot_busy[i];
        if ((int)queue.size() >= opt.max_queue + idle) { n_rejected++; return false; }
        t->t_queued = now_ms();
        queue.push_back(t);
        qcv.notify_all();
        return true;
    }

    bool warm(int timeout_ms) {
        std::unique_lock<std::mutex> lk(qmu);
        uint64_t want = warm_done + 1;
        warm_wanted = true;
        qcv.notify_all();
        return qcv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&] { return warm_done >= want; });
    }

    json stats() {
        json s = json::array();
        for (int i = 0; i < opt.slots; i++) s.push_back({{"id", i}, {"busy", slot_busy[i].load()}, {"cached_tokens", slot_cached[i].load()}});
        size_t waiting;
        { std::lock_guard<std::mutex> lk(qmu); waiting = queue.size(); }
        return {{"model", opt.alias}, {"slots", s}, {"waiting", waiting},
                {"requests", n_requests.load()}, {"rejected", n_rejected.load()},
                {"prompt_tokens", n_prompt_tokens.load()}, {"cached_tokens", n_cached_tokens.load()},
                {"generated_tokens", n_gen_tokens.load()}, {"prefix_cache", opt.prefix_cache}};
    }

    void start() { worker = std::thread([this] { loop(); }); }
    void stop() {
        { std::lock_guard<std::mutex> lk(qmu); stopping = true; qcv.notify_all(); }
        if (worker.joinable()) worker.join();
    }

private:
    std::vector<Slot> slots;                      // touched by the decode thread only
    std::unique_ptr<std::atomic<bool>[]> slot_busy;   // what other threads may read
    std::unique_ptr<std::atomic<int>[]>  slot_cached;
    llama_batch batch{};
    std::mutex qmu;
    std::condition_variable qcv;
    std::deque<std::shared_ptr<Task>> queue;
    bool stopping = false, warm_wanted = false;
    uint64_t warm_done = 0;
    std::thread worker;
    std::vector<llama_token_data> cand;

    void batch_add(llama_token t, llama_pos pos, llama_seq_id seq, bool logits) {
        int i = batch.n_tokens++;
        batch.token[i]     = t;
        batch.pos[i]       = pos;
        batch.n_seq_id[i]  = 1;
        batch.seq_id[i][0] = seq;
        batch.logits[i]    = logits;
    }

    // The idle slot sharing the longest prefix with the prompt, if that prefix is worth keeping (64+ tokens).
    // Otherwise the idle slot whose cache is cheapest to lose: the emptiest, then the least recently used. (Picking
    // the "longest" prefix of 3 tokens, just the chat markup, would throw away another prompt's whole cache.)
    Slot * pick_slot(const std::vector<llama_token> & prompt) {
        Slot * best = nullptr, * spare = nullptr;
        size_t best_n = 0;
        for (auto & s : slots) {
            if (s.task) continue;
            size_t n = opt.prefix_cache ? common_prefix(s.cache, prompt) : 0;
            if (n > best_n) { best = &s; best_n = n; }
            if (!spare || s.cache.size() < spare->cache.size() ||
                (s.cache.size() == spare->cache.size() && s.last_used < spare->last_used)) spare = &s;
        }
        return best && best_n >= 64 ? best : spare;
    }

    void begin(Slot & s, std::shared_ptr<Task> t) {
        llama_memory_t mem = llama_get_memory(ctx);
        const auto & p = t->prompt;
        size_t keep = opt.prefix_cache ? common_prefix(s.cache, p) : 0;
        Slot * donor = nullptr;
        if (opt.prefix_cache) {
            for (auto & o : slots) {   // another slot shares a longer prefix: copy its cells
                if (&o == &s) continue;
                size_t n = common_prefix(o.cache, p);
                if (n > keep + 16) { keep = n; donor = &o; }
            }
        }
        if (keep >= p.size()) keep = p.size() - 1;   // the last prompt token is always decoded: its logits start the answer
        if (donor) {
            llama_memory_seq_rm(mem, s.id, -1, -1);
            llama_memory_seq_cp(mem, donor->id, s.id, 0, (llama_pos)keep);
            s.cache.assign(donor->cache.begin(), donor->cache.begin() + keep);
        } else {
            if (!llama_memory_seq_rm(mem, s.id, (llama_pos)keep, -1)) {
                llama_memory_seq_rm(mem, s.id, -1, -1);
                keep = 0;
            }
            s.cache.resize(keep);
        }

        s.task = std::move(t);
        Task & task = *s.task;
        task.slot      = s.id;
        task.n_prompt  = (int)p.size();
        task.n_cached  = (int)keep;
        task.t_started = now_ms();
        s.generating = false;
        s.next       = -1;
        s.i_batch    = -1;
        s.text.clear();
        s.sent       = 0;
        s.in_thought = false;
        s.json_depth = 0;
        s.json_str = s.json_esc = s.json_open = false;

        s.free_samplers();
        s.chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
        if (task.temperature <= 0.0f) {
            llama_sampler_chain_add(s.chain, llama_sampler_init_greedy());
        } else {
            if (task.top_k > 0)    llama_sampler_chain_add(s.chain, llama_sampler_init_top_k(task.top_k));
            if (task.top_p < 1.0f) llama_sampler_chain_add(s.chain, llama_sampler_init_top_p(task.top_p, 1));
            if (task.min_p > 0.0f) llama_sampler_chain_add(s.chain, llama_sampler_init_min_p(task.min_p, 1));
            llama_sampler_chain_add(s.chain, llama_sampler_init_temp(task.temperature));
            llama_sampler_chain_add(s.chain, llama_sampler_init_dist(task.seed));
        }
        if (task.json_mode) s.grammar = llama_sampler_init_grammar(vocab, JSON_GRAMMAR, "root");

        slot_busy[s.id] = true;
        n_requests++;
        n_prompt_tokens += p.size();
        n_cached_tokens += keep;
    }

    // Sample with the chain. With a grammar, check the pick first and constrain the whole vocabulary only when the
    // pick breaks the grammar (Gemma has 262k tokens: running the grammar over all of them costs milliseconds).
    llama_token sample(Slot & s, int idx) {
        const float * logits = llama_get_logits_ith(ctx, idx);
        cand.resize(n_vocab);
        auto fill = [&] {
            for (int i = 0; i < n_vocab; i++) cand[i] = llama_token_data{i, logits[i], 0.0f};
            return llama_token_data_array{cand.data(), cand.size(), -1, false};
        };
        llama_token_data_array arr = fill();
        llama_sampler_apply(s.chain, &arr);
        llama_token id = arr.data[arr.selected].id;
        if (s.grammar) {
            llama_token_data one{id, 1.0f, 0.0f};
            llama_token_data_array single{&one, 1, -1, false};
            llama_sampler_apply(s.grammar, &single);
            if (std::isinf(one.logit) && one.logit < 0) {
                arr = fill();
                llama_sampler_apply(s.grammar, &arr);
                llama_sampler_apply(s.chain, &arr);
                id = arr.data[arr.selected].id;
            }
            llama_sampler_accept(s.grammar, id);
        }
        llama_sampler_accept(s.chain, id);
        return id;
    }

    // Hand finished text to the task, holding back what could be the start of a stop string.
    void flush_text(Slot & s, bool all) {
        Task & t = *s.task;
        size_t hold = 0;
        if (!all) for (auto & st : t.stop) hold = std::max(hold, st.empty() ? 0 : st.size() - 1);
        size_t upto = s.text.size() > hold ? s.text.size() - hold : 0;
        if (upto <= s.sent) return;
        upto = s.sent + utf8_complete(s.text.substr(s.sent, upto - s.sent));
        if (upto > s.sent) {
            t.push(s.text.substr(s.sent, upto - s.sent));
            s.sent = upto;
        }
    }

    void end(Slot & s, const std::string & reason, const std::string & err = "") {
        if (err.empty()) flush_text(s, true);
        n_gen_tokens += s.task->n_gen;
        s.task->finish(reason, err);
        s.task.reset();
        s.free_samplers();
        s.generating = false;
        s.last_used = now_ms();
        slot_busy[s.id] = false;
        slot_cached[s.id] = (int)s.cache.size();
    }

    // One sampled token. false: the answer is over.
    bool on_token(Slot & s, llama_token id) {
        Task & t = *s.task;
        t.n_gen++;
        if (t.n_gen == 1) t.t_first = now_ms();
        if (llama_vocab_is_eog(vocab, id)) { end(s, "stop"); return false; }

        if (id == tok_channel_open) {
            s.in_thought = true;
        } else if (id == tok_channel_close) {
            s.in_thought = false;
        } else if (s.in_thought) {
            std::lock_guard<std::mutex> lk(t.mu);
            t.reasoning += piece(id);
        } else {
            size_t from = s.text.size();
            s.text += piece(id);
            for (auto & st : t.stop) {         // a stop string: cut there and finish
                if (st.empty()) continue;
                size_t look = from >= st.size() ? from - st.size() + 1 : 0;
                size_t at = s.text.find(st, look);
                if (at != std::string::npos) {
                    s.text.resize(at);
                    end(s, "stop");
                    return false;
                }
            }
            if (t.json_mode) {                 // stop as soon as the top-level object closes
                for (size_t i = from; i < s.text.size(); i++) {
                    char c = s.text[i];
                    if (s.json_str) {
                        if (s.json_esc) s.json_esc = false;
                        else if (c == '\\') s.json_esc = true;
                        else if (c == '"') s.json_str = false;
                    } else if (c == '"') {
                        s.json_str = true;
                    } else if (c == '{' || c == '[') {
                        s.json_depth++;
                        s.json_open = true;
                    } else if ((c == '}' || c == ']') && --s.json_depth == 0 && s.json_open) {
                        s.text.resize(i + 1);
                        end(s, "stop");
                        return false;
                    }
                }
            }
            flush_text(s, false);
        }
        if (t.n_gen >= t.max_tokens || (int)s.cache.size() + 1 >= opt.ctx) { end(s, "length"); return false; }
        s.next = id;
        return true;
    }

    void do_warm() {
        llama_seq_id seq = opt.slots;    // the spare sequence
        batch.n_tokens = 0;
        batch_add(llama_vocab_bos(vocab), 0, seq, true);
        llama_decode(ctx, batch);
        llama_memory_seq_rm(llama_get_memory(ctx), seq, -1, -1);
    }

    void loop() {
        while (true) {
            // 1. hand queued requests to idle slots
            {
                std::unique_lock<std::mutex> lk(qmu);
                auto active = [&] { for (auto & s : slots) if (s.task) return true; return false; };
                qcv.wait(lk, [&] { return stopping || !queue.empty() || active() || warm_wanted; });
                if (stopping) break;
                while (!queue.empty()) {
                    Slot * s = pick_slot(queue.front()->prompt);
                    if (!s) break;
                    auto t = queue.front();
                    queue.pop_front();
                    if (t->cancelled) { t->finish("cancelled"); continue; }
                    begin(*s, t);
                }
                if (warm_wanted && !active()) {
                    do_warm();
                    warm_wanted = false;
                    warm_done++;
                    qcv.notify_all();
                    continue;
                }
            }

            // 2. drop requests whose client went away
            for (auto & s : slots) if (s.task && s.task->cancelled) end(s, "cancelled");

            // 3. one step: a token for every generating slot, then prompt chunks
            batch.n_tokens = 0;
            for (auto & s : slots) {
                if (!s.task || !s.generating) continue;
                s.i_batch = batch.n_tokens;
                batch_add(s.next, (llama_pos)s.cache.size(), s.id, true);
                s.cache.push_back(s.next);
            }
            for (auto & s : slots) {
                if (!s.task || s.generating) continue;
                const auto & p = s.task->prompt;
                while (s.cache.size() < p.size() && batch.n_tokens < opt.batch) {
                    size_t i = s.cache.size();
                    bool last = i + 1 == p.size();
                    if (last) s.i_batch = batch.n_tokens;
                    batch_add(p[i], (llama_pos)i, s.id, last);
                    s.cache.push_back(p[i]);
                }
            }
            if (batch.n_tokens == 0) continue;

            int rc = llama_decode(ctx, batch);
            if (rc == 1) {   // no room in the KV cache: forget the idle slots' prefixes and try once more
                llama_memory_t mem = llama_get_memory(ctx);
                for (auto & s : slots) {
                    if (s.task || s.cache.empty()) continue;
                    llama_memory_seq_rm(mem, s.id, -1, -1);
                    s.cache.clear();
                    slot_cached[s.id] = 0;
                }
                rc = llama_decode(ctx, batch);
            }
            if (rc != 0) {
                llama_memory_t mem = llama_get_memory(ctx);
                for (auto & s : slots) {
                    if (!s.task) continue;
                    llama_memory_seq_rm(mem, s.id, -1, -1);
                    s.cache.clear();
                    end(s, "error", "decode failed (" + std::to_string(rc) + "): the context is full or out of memory");
                }
                continue;
            }

            // 4. sample for every slot that got logits
            for (auto & s : slots) {
                if (!s.task || s.i_batch < 0) continue;
                int idx = s.i_batch;
                s.i_batch = -1;
                s.generating = true;
                on_token(s, sample(s, idx));
            }
        }
        for (auto & s : slots) if (s.task) end(s, "error", "the server is stopping");
        std::lock_guard<std::mutex> lk(qmu);
        for (auto & t : queue) t->finish("error", "the server is stopping");
        queue.clear();
    }
};

// ---------------------------------------------------------------------------------------------------------------
// HTTP

static Engine g;
static std::string g_worker_token;

static std::string dump(const json & j) { return j.dump(-1, ' ', false, json::error_handler_t::replace); }

static void send_json(httplib::Response & res, int status, const json & body) {
    res.status = status;
    res.set_content(dump(body), "application/json; charset=utf-8");
}

static void send_error(httplib::Response & res, int status, const std::string & msg, const char * type = "invalid_request_error") {
    send_json(res, status, {{"error", {{"message", msg}, {"type", type}, {"code", status}}}});
}

static std::string random_id() {
    static std::mutex mu;
    static std::mt19937_64 rng(std::random_device{}());
    static const char * hex = "0123456789abcdef";
    std::lock_guard<std::mutex> lk(mu);
    std::string s = "chatcmpl-";
    uint64_t v = rng();
    for (int i = 0; i < 16; i++) { s += hex[v & 15]; v >>= 4; }
    return s;
}

static double round1(double v) { return std::round(v * 10) / 10; }

// Call with t.mu held or after t.finished.
static json timings_of(const Task & t) {
    double prefill = t.t_first - t.t_started, gen = t.t_end - t.t_first;
    int n_pre = t.n_prompt - t.n_cached;
    return {
        {"queue_ms", std::round(t.t_started - t.t_queued)},
        {"prompt_n", n_pre}, {"cache_n", t.n_cached},
        {"prompt_ms", std::round(prefill)},
        {"prompt_per_second", prefill > 0 ? round1(n_pre * 1000.0 / prefill) : 0.0},
        {"predicted_n", t.n_gen},
        {"predicted_ms", std::round(gen)},
        {"predicted_per_second", gen > 0 && t.n_gen > 1 ? round1((t.n_gen - 1) * 1000.0 / gen) : 0.0},
        {"ttft_ms", std::round(t.t_first - t.t_queued)},
        {"total_ms", std::round(t.t_end - t.t_queued)},
        {"slot", t.slot},
    };
}

static json usage_of(const Task & t) {
    return {{"prompt_tokens", t.n_prompt}, {"completion_tokens", t.n_gen}, {"total_tokens", t.n_prompt + t.n_gen},
            {"prompt_tokens_details", {{"cached_tokens", t.n_cached}}}};
}

static void log_done(const std::string & id, const Task & t, bool stream) {
    json tm = timings_of(t);
    logf("chat %s%s slot=%d prompt=%d cached=%d gen=%d queue=%.0fms ttft=%.0fms prefill=%.0ftok/s decode=%.1ftok/s total=%.0fms %s%s",
         id.c_str(), stream ? " stream" : "", t.slot, t.n_prompt, t.n_cached, t.n_gen, tm["queue_ms"].get<double>(),
         tm["ttft_ms"].get<double>(), tm["prompt_per_second"].get<double>(), tm["predicted_per_second"].get<double>(),
         tm["total_ms"].get<double>(), t.finish_reason.c_str(), t.json_mode ? " json" : "");
}

static void handle_chat(const httplib::Request & req, httplib::Response & res) {
    json body;
    try {
        body = json::parse(req.body);
    } catch (const std::exception & e) {
        return send_error(res, 400, std::string("invalid JSON: ") + e.what());
    }
    if (!body.is_object()) return send_error(res, 400, "the body must be a JSON object");

    bool thinking = false;
    if (body.contains("chat_template_kwargs") && body["chat_template_kwargs"].is_object())
        thinking = body["chat_template_kwargs"].value("enable_thinking", false);

    std::vector<Part> parts;
    std::string err;
    if (!gemma4_prompt(body.contains("messages") ? body["messages"] : json(), thinking, parts, err)) return send_error(res, 400, err);

    auto t = std::make_shared<Task>();
    t->prompt = g.tokenize(parts);
    int limit = g.opt.ctx - 1;
    if ((int)t->prompt.size() >= limit)
        return send_error(res, 400, "the prompt is " + std::to_string(t->prompt.size()) + " tokens; this server takes at most " +
                                        std::to_string(limit) + " (--ctx)");
    bool stream = false, include_usage = false;
    try {
        int mt = body.contains("max_completion_tokens") ? body["max_completion_tokens"].get<int>() : body.value("max_tokens", 1024);
        t->max_tokens  = std::max(1, std::min(mt, limit - (int)t->prompt.size()));
        t->temperature = body.value("temperature", 0.7f);
        t->top_p       = body.value("top_p", 0.95f);
        t->top_k       = body.value("top_k", 64);
        t->min_p       = body.value("min_p", 0.0f);
        t->seed        = body.contains("seed") && body["seed"].is_number() ? body["seed"].get<uint32_t>() : (uint32_t)std::random_device{}();
        if (body.contains("stop")) {
            if (body["stop"].is_string()) t->stop.push_back(body["stop"].get<std::string>());
            else if (body["stop"].is_array()) for (auto & s : body["stop"]) if (s.is_string()) t->stop.push_back(s.get<std::string>());
        }
        if (body.contains("response_format") && body["response_format"].is_object()) {
            std::string type = body["response_format"].value("type", "text");
            if (type == "json_object" || type == "json_schema") t->json_mode = true;   // any JSON object; a schema is not enforced
            else if (type != "text") return send_error(res, 400, "response_format.type must be text, json_object or json_schema");
        }
        stream = body.value("stream", false);
        include_usage = body.contains("stream_options") && body["stream_options"].is_object() &&
                        body["stream_options"].value("include_usage", false);
    } catch (const std::exception & e) {
        return send_error(res, 400, std::string("bad parameter: ") + e.what());
    }

    std::string id = random_id();
    long created = (long)time(nullptr);
    if (!g.opt.dump_dir.empty()) {
        std::string text;
        for (auto & p : parts) text += p.text;
        if (FILE * f = fopen((g.opt.dump_dir + "/" + id + ".txt").c_str(), "wb")) {
            fwrite(text.data(), 1, text.size(), f);
            fclose(f);
        }
    }
    std::string model = g.opt.alias;

    if (!g.submit(t)) {
        res.set_header("Retry-After", "2");
        return send_error(res, 429, "the model is busy: too many requests waiting", "server_busy");
    }

    if (!stream) {
        std::unique_lock<std::mutex> lk(t->mu);
        t->cv.wait(lk, [&] { return t->finished; });
        if (!t->error.empty()) return send_error(res, 500, t->error, "server_error");
        std::string text;
        for (auto & c : t->chunks) text += c;
        json msg = {{"role", "assistant"}, {"content", text}};
        if (!t->reasoning.empty()) msg["reasoning_content"] = t->reasoning;
        log_done(id, *t, false);
        return send_json(res, 200, {
            {"id", id}, {"object", "chat.completion"}, {"created", created}, {"model", model},
            {"choices", json::array({{{"index", 0}, {"message", msg}, {"finish_reason", t->finish_reason}}})},
            {"usage", usage_of(*t)}, {"timings", timings_of(*t)},
        });
    }

    // Server-sent events, OpenAI style: a role chunk, content chunks, a last chunk with finish_reason, [DONE].
    auto chunk = [id, created, model](const json & delta, const json & finish) {
        return json{{"id", id}, {"object", "chat.completion.chunk"}, {"created", created}, {"model", model},
                    {"choices", json::array({{{"index", 0}, {"delta", delta}, {"finish_reason", finish}}})}};
    };
    auto first = std::make_shared<bool>(true);
    res.set_header("Cache-Control", "no-store");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider("text/event-stream",
        [t, chunk, first, include_usage, id](size_t, httplib::DataSink & sink) {
            std::unique_lock<std::mutex> lk(t->mu);
            t->cv.wait(lk, [&] { return t->finished || !t->chunks.empty(); });
            std::string out;
            if (*first) { out += "data: " + dump(chunk({{"role", "assistant"}, {"content", ""}}, nullptr)) + "\n\n"; *first = false; }
            while (!t->chunks.empty()) {
                out += "data: " + dump(chunk({{"content", t->chunks.front()}}, nullptr)) + "\n\n";
                t->chunks.pop_front();
            }
            bool done = t->finished;
            if (done) {
                if (!t->error.empty()) {
                    out += "data: " + dump({{"error", {{"message", t->error}, {"type", "server_error"}}}}) + "\n\n";
                } else {
                    json last = chunk(json::object(), t->finish_reason);
                    last["timings"] = timings_of(*t);
                    if (include_usage) last["usage"] = usage_of(*t);
                    out += "data: " + dump(last) + "\n\n";
                }
                out += "data: [DONE]\n\n";
                log_done(id, *t, true);
            }
            lk.unlock();
            if (!sink.write(out.data(), out.size())) { t->cancelled = true; return false; }
            if (done) sink.done();
            return true;
        },
        [t](bool) {
            std::lock_guard<std::mutex> lk(t->mu);
            if (!t->finished) t->cancelled = true;
        });
}

int main(int argc, char ** argv) {
    if (!parse_args(argc, argv, g.opt)) return 2;
    if (const char * tok = getenv("WORKER_TOKEN")) g_worker_token = tok;
    llama_log_set([](ggml_log_level level, const char * text, void *) {
        if (level == GGML_LOG_LEVEL_WARN || level == GGML_LOG_LEVEL_ERROR) fputs(text, stderr);
    }, nullptr);

    logf("loading %s ...", g.opt.model.c_str());
    if (!g.load()) return 1;
    logf("loaded %s: %.2f GB, %.2fB params, %d slots x %d tokens, prefix cache %s, %.1f s",
         g.opt.alias.c_str(), g.model_bytes / 1e9, g.n_params / 1e9, g.opt.slots, g.opt.ctx,
         g.opt.prefix_cache ? "on" : "off", g.load_ms / 1000);
    g.start();

    httplib::Server svr;
    int http_threads = g.opt.http_threads;
    svr.new_task_queue = [http_threads] { return new httplib::ThreadPool(http_threads); };
    svr.set_read_timeout(60, 0);
    svr.set_write_timeout(600, 0);
    svr.set_payload_max_length(16 * 1024 * 1024);

    svr.set_pre_routing_handler([](const httplib::Request & req, httplib::Response & res) {
        if (req.path == "/health") return httplib::Server::HandlerResponse::Unhandled;
        bool ok = true;
        if (!g_worker_token.empty()) {
            ok = req.get_header_value("X-Worker-Token") == g_worker_token;
        } else if (!g.opt.api_key.empty()) {
            ok = req.get_header_value("Authorization") == "Bearer " + g.opt.api_key;
        }
        if (ok) return httplib::Server::HandlerResponse::Unhandled;
        send_error(res, 401, "missing or invalid token", "authentication_error");
        return httplib::Server::HandlerResponse::Handled;
    });

    svr.Get("/health", [](const httplib::Request &, httplib::Response & res) { send_json(res, 200, {{"status", "ok"}}); });
    svr.Get("/info", [](const httplib::Request &, httplib::Response & res) {
        send_json(res, 200, {{"engine", "llm"}, {"model", g.opt.alias}, {"default_voice", ""}, {"voices", json::array()},
                             {"slots", g.opt.slots}, {"ctx", g.opt.ctx}, {"size_bytes", g.model_bytes}, {"params", g.n_params}});
    });
    svr.Get("/stats", [](const httplib::Request &, httplib::Response & res) { send_json(res, 200, g.stats()); });
    svr.Get("/v1/models", [](const httplib::Request &, httplib::Response & res) {
        send_json(res, 200, {{"object", "list"}, {"data", json::array({{{"id", g.opt.alias}, {"object", "model"},
                  {"created", (long)time(nullptr)}, {"owned_by", "local"}, {"context_length", g.opt.ctx}}})}});
    });
    svr.Post("/v1/chat/completions", handle_chat);
    svr.Post("/chat/completions", handle_chat);
    svr.Post("/warm", [](const httplib::Request &, httplib::Response & res) {
        double t0 = now_ms();
        bool ok = g.warm(30000);
        send_json(res, ok ? 200 : 503, {{"ok", ok}, {"ms", std::round(now_ms() - t0)}});
    });

    logf("listening on http://%s:%d (model %s)", g.opt.host.c_str(), g.opt.port, g.opt.alias.c_str());
    if (!svr.listen(g.opt.host, g.opt.port)) {
        logf("cannot listen on %s:%d", g.opt.host.c_str(), g.opt.port);
        g.stop();
        return 1;
    }
    g.stop();
    return 0;
}
