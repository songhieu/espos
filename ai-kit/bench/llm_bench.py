#!/usr/bin/env python3
"""Raw speed of bin/llm-server (Python 3 standard library only).

Starts its own llm-server on a spare port for each configuration, so nothing else should use the GPU meanwhile.

    python3 llm_bench.py --model ../models/llm/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf --label e2b

Measures, per configuration:
  * memory: the process's physical footprint after loading and after the tests (macOS `footprint`);
  * cold prompts of ~1k, ~4k and ~8k tokens: time to first token and prefill tokens/s;
  * decoding: 256 tokens of free text and a JSON answer (grammar on): tokens/s;
  * the prefix cache: the dog assistant's real prompt (fixtures/prompt_full.json, prompt_short.json) cold, then
    again with a different question (the shared prefix cached), and with --no-prefix-cache;
  * 1, 2 and 4 requests at once: each its own ~1k-token prompt, or the dog prompt with its own question.
Results: results/llm-<label>.json and a table on stdout.
"""
import argparse, json, os, re, statistics, subprocess, sys, threading, time, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 9411


def post(path, body, timeout=600):
    req = urllib.request.Request("http://127.0.0.1:%d%s" % (PORT, path), json.dumps(body).encode(), {"Content-Type": "application/json"})
    t0 = time.time()
    r = json.load(urllib.request.urlopen(req, timeout=timeout))
    r["_wall"] = time.time() - t0
    return r


def footprint_mb(pid):
    try:
        out = subprocess.run(["footprint", "-p", str(pid)], capture_output=True, text=True, timeout=30).stdout
        m = re.search(r"phys_footprint:\s*([\d.]+)\s*([KMG]B)", out) or re.search(r"Footprint:\s*([\d.]+)\s*([KMG]B)", out)
        if m:
            v = float(m.group(1))
            return round(v / 1024 if m.group(2) == "KB" else v * 1024 if m.group(2) == "GB" else v)
    except Exception:
        pass
    out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return round(int(out) / 1024) if out else None


class Server:
    def __init__(self, binary, model, args):
        self.args = [binary, "--model", model, "--port", str(PORT)] + args
        self.p = subprocess.Popen(self.args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        t0 = time.time()
        while time.time() - t0 < 180:
            try:
                urllib.request.urlopen("http://127.0.0.1:%d/health" % PORT, timeout=2)
                break
            except Exception:
                if self.p.poll() is not None:
                    sys.exit("llm-server exited: " + (self.p.stderr.read() or "")[-2000:])
                time.sleep(0.5)
        self.load_s = round(time.time() - t0, 1)

    def stop(self):
        self.p.terminate()
        try:
            self.p.wait(10)
        except Exception:
            self.p.kill()


FILLER = open(os.path.join(HERE, "fixtures", "filler_vi.txt")).read() if os.path.exists(os.path.join(HERE, "fixtures", "filler_vi.txt")) else None


def filler(n_chars, tag):
    """Vietnamese text of about n_chars characters, made unique by tag (so nothing is cached)."""
    base = FILLER or json.load(open(os.path.join(HERE, "fixtures", "prompt_full.json")))["messages"][0]["content"]
    s = (base * (n_chars // len(base) + 1))[:n_chars]
    return "[%s] %s" % (tag, s)


def chat(system, user, max_tokens, json_mode=False, temperature=0.7):
    body = {"messages": [{"role": "system", "content": system}, {"role": "user", "content": user}],
            "max_tokens": max_tokens, "temperature": temperature}
    if json_mode:
        body["response_format"] = {"type": "json_object"}
    return post("/v1/chat/completions", body)


def row(r):
    t = r["timings"]
    return {"prompt": r["usage"]["prompt_tokens"], "cached": r["usage"]["prompt_tokens_details"]["cached_tokens"],
            "out": r["usage"]["completion_tokens"], "ttft_s": round(t["ttft_ms"] / 1000, 2), "prefill_tps": t["prompt_per_second"],
            "decode_tps": t["predicted_per_second"], "total_s": round(t["total_ms"] / 1000, 2), "wall_s": round(r["_wall"], 2)}


def parallel(n, make):
    out = [None] * n
    def run(i):
        out[i] = make(i)
    ths = [threading.Thread(target=run, args=(i,)) for i in range(n)]
    t0 = time.time()
    for t in ths:
        t.start()
    for t in ths:
        t.join()
    wall = time.time() - t0
    rows = [row(r) for r in out]
    gen = sum(r["out"] for r in rows)
    return {"n": n, "wall_s": round(wall, 2), "ttft_s": [r["ttft_s"] for r in rows], "total_s": [r["total_s"] for r in rows],
            "median_ttft_s": round(statistics.median(r["ttft_s"] for r in rows), 2),
            "max_total_s": max(r["total_s"] for r in rows), "tokens_out": gen, "aggregate_decode_tps": round(gen / wall, 1),
            "per_request_decode_tps": round(statistics.median(r["decode_tps"] for r in rows), 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=os.path.join(HERE, "..", "bin", "llm-server"))
    ap.add_argument("--model", required=True)
    ap.add_argument("--label", default="run")
    ap.add_argument("--ctx", type=int, default=16384)
    ap.add_argument("--quick", action="store_true", help="fewer repeats")
    ap.add_argument("--parts", default="123", help="1 = one slot, 2 = no prefix cache, 3 = concurrency (others kept from the last run)")
    a = ap.parse_args()

    dog_full = json.load(open(os.path.join(HERE, "fixtures", "prompt_full.json")))["messages"]
    dog_short = json.load(open(os.path.join(HERE, "fixtures", "prompt_short.json")))["messages"]
    questions = ["Mấy giờ rồi?", "Thời tiết hôm nay thế nào?", "Đặt báo thức 6 giờ sáng mai.", "Đọc báo sáng nay đi.",
                 "Tao mệt quá.", "Giá vàng bao nhiêu?", "Nhắc tao mua sữa.", "Mày đang làm gì đó?"]
    res = {"label": a.label, "model": os.path.basename(a.model), "model_bytes": os.path.getsize(a.model),
           "when": time.strftime("%Y-%m-%d %H:%M"), "loadavg_start": os.getloadavg(), "load": {}, "cold": [], "decode": {}, "dog": {}, "concurrency": {}}

    prev = os.path.join(HERE, "results", "llm-%s.json" % a.label)
    if a.parts != "123" and os.path.exists(prev):
        res.update({k: v for k, v in json.load(open(prev)).items() if k not in ("loadavg_start", "when")})
    if "1" in a.parts:
        one_slot(a, res, dog_full, dog_short, questions)
    if "2" in a.parts:
        no_cache(a, res, dog_full, dog_short, questions)
    if "3" in a.parts:
        concurrency(a, res, dog_short, questions)
    res["loadavg_end"] = os.getloadavg()
    os.makedirs(os.path.join(HERE, "results"), exist_ok=True)
    json.dump(res, open(prev, "w"), ensure_ascii=False, indent=1)
    print("wrote", prev, file=sys.stderr)


def one_slot(a, res, dog_full, dog_short, questions):
    """1. one slot: cold prompts, decoding, the prefix cache"""
    srv = Server(a.bin, a.model, ["--slots", "1", "--ctx", str(a.ctx)])
    res["load"]["slots1_s"] = srv.load_s
    res["load"]["slots1_footprint_mb"] = footprint_mb(srv.p.pid)
    chat("Xin chào", "Chào", 8)  # first request warms the prompt kernels
    for n_chars in (3000, 12000, 24000):
        r = row(chat(filler(n_chars, "cold%d" % n_chars), "Tóm tắt một câu.", 16))
        r["chars"] = n_chars
        res["cold"].append(r)
        print("cold %6d chars: %5d tokens, ttft %5.2fs, prefill %6.1f tok/s" % (n_chars, r["prompt"], r["ttft_s"], r["prefill_tps"]), file=sys.stderr)
    for kind, js in (("text", False), ("json", True)):
        rs = [row(chat("Bạn là trợ lý. Trả lời dài, chi tiết." + (" Trả về JSON có khóa reply." if js else ""),
                       "Kể chuyện về một chú chó tên Boop đi lạc trong thành phố. %d" % i, 256, json_mode=js, temperature=0.8))
              for i in range(1 if a.quick else 3)]
        res["decode"][kind] = {"out": [r["out"] for r in rs], "decode_tps": round(statistics.median(r["decode_tps"] for r in rs), 1)}
        print("decode %s: %.1f tok/s (%s tokens)" % (kind, res["decode"][kind]["decode_tps"], res["decode"][kind]["out"]), file=sys.stderr)
    for name, msgs in (("full", dog_full), ("short", dog_short)):
        sys_p = msgs[0]["content"]
        cold = row(chat(sys_p, msgs[1]["content"], 700, json_mode=True))
        warm = [row(chat(sys_p, q, 700, json_mode=True)) for q in questions[:2 if a.quick else 4]]
        res["dog"][name] = {"cold": cold, "warm": warm,
                            "warm_median_total_s": round(statistics.median(w["total_s"] for w in warm), 2),
                            "warm_median_ttft_s": round(statistics.median(w["ttft_s"] for w in warm), 2)}
        print("dog %-5s cold: %d tokens ttft %.2fs total %.2fs | cached: ttft %.2fs total %.2fs (out %s)" % (
            name, cold["prompt"], cold["ttft_s"], cold["total_s"], res["dog"][name]["warm_median_ttft_s"],
            res["dog"][name]["warm_median_total_s"], [w["out"] for w in warm]), file=sys.stderr)
    res["load"]["slots1_footprint_after_mb"] = footprint_mb(srv.p.pid)
    srv.stop()


def no_cache(a, res, dog_full, dog_short, questions):
    """2. the dog prompts without the prefix cache"""
    srv = Server(a.bin, a.model, ["--slots", "1", "--ctx", str(a.ctx), "--no-prefix-cache"])
    chat("Xin chào", "Chào", 8)
    for name, msgs in (("full", dog_full), ("short", dog_short)):
        rs = [row(chat(msgs[0]["content"], q, 700, json_mode=True)) for q in questions[:2]]
        res["dog"][name]["no_cache"] = rs
        res["dog"][name]["no_cache_median_total_s"] = round(statistics.median(r["total_s"] for r in rs), 2)
        print("dog %-5s no cache: ttft %s total %s" % (name, [r["ttft_s"] for r in rs], [r["total_s"] for r in rs]), file=sys.stderr)
    srv.stop()


def concurrency(a, res, dog_short, questions):
    """3. 1, 2, 4 requests at once (4 slots)"""
    srv = Server(a.bin, a.model, ["--slots", "4", "--ctx", str(a.ctx)])
    res["load"]["slots4_footprint_mb"] = footprint_mb(srv.p.pid)
    chat("Xin chào", "Chào", 8)
    sys_short = dog_short[0]["content"]
    chat(sys_short, "Chào", 32, json_mode=True)   # the dog's prefix in the cache, as on a running server
    story = "Kể một câu chuyện thật dài, nhiều chi tiết, về chú chó tên Boop đi lạc trong thành phố (bản %d)."
    for n in (1, 2, 4):
        # the dog: n people ask at once; the fixed part of the prompt is cached (one slot has it, the others copy it),
        # as on a server that keeps answering the dog
        chat(sys_short, "Chào", 8, json_mode=True)
        dog = parallel(n, lambda i: chat(sys_short, questions[(i + n) % len(questions)], 700, json_mode=True))
        # decoding: short prompts, ~200 tokens each: does batching n answers raise the total tokens/s?
        dec = parallel(n, lambda i: chat("Bạn là người kể chuyện.", story % (i + 10 * n), 200, temperature=0.8))
        res["concurrency"][str(n)] = {"decode_200": dec, "dog_short_cached": dog}
        print("x%d decode: per request %.1f tok/s, all together %.1f tok/s | dog: ttft %s total %s" % (
            n, dec["per_request_decode_tps"], dec["aggregate_decode_tps"], dog["ttft_s"], dog["total_s"]), file=sys.stderr)
    res["load"]["slots4_footprint_after_mb"] = footprint_mb(srv.p.pid)
    srv.stop()


if __name__ == "__main__":
    main()
