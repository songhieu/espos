#!/usr/bin/env python3
"""Check that llm-server builds Gemma's prompt exactly like the model's own chat template.

Renders a few conversations two ways and compares the text:
  * the GGUF's tokenizer.chat_template (Jinja, rendered with jinja2), and
  * bin/llm-server --dump-prompts (what the server really sends to the model).

    worker/.venv/bin/python llm/check_template.py models/llm/gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf

Needs jinja2 (the TTS worker's venv has it). Exit 0 when all cases match.
"""
import json, os, struct, subprocess, sys, tempfile, time, urllib.request

import jinja2

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 9413


def gguf_template(path):
    T = {0: "<B", 1: "<b", 2: "<H", 3: "<h", 4: "<I", 5: "<i", 6: "<f", 7: "<?", 10: "<Q", 11: "<q", 12: "<d"}
    with open(path, "rb") as f:
        f.read(4)
        struct.unpack("<I", f.read(4))
        _, nkv = struct.unpack("<QQ", f.read(16))

        def rs():
            n, = struct.unpack("<Q", f.read(8))
            return f.read(n).decode("utf-8", "replace")

        def skip(t):
            if t == 8:
                rs()
            elif t == 9:
                at, = struct.unpack("<I", f.read(4))
                n, = struct.unpack("<Q", f.read(8))
                if at == 8:
                    for _ in range(n):
                        rs()
                else:
                    f.seek(struct.calcsize(T[at]) * n, 1)
            else:
                f.seek(struct.calcsize(T[t]), 1)

        for _ in range(nkv):
            k = rs()
            t, = struct.unpack("<I", f.read(4))
            if k == "tokenizer.chat_template":
                return rs()
            skip(t)
    sys.exit("no chat template in " + path)


CASES = [
    [{"role": "user", "content": "Xin chào"}],
    [{"role": "system", "content": "  Bạn là chú chó Boop.\n"}, {"role": "user", "content": "Mấy giờ rồi?"}],
    [{"role": "system", "content": "Trả lời ngắn."}, {"role": "user", "content": "Chào"},
     {"role": "assistant", "content": "Gâu! Chào chủ."}, {"role": "user", "content": " Hôm nay thế nào? "}],
    [{"role": "user", "content": [{"type": "text", "text": "Một"}, {"type": "text", "text": " hai "}]}],
    [{"role": "user", "content": "A"}, {"role": "assistant", "content": "<|channel>thought\nnghĩ<channel|>Đáp"}, {"role": "user", "content": "B"}],
]


def main():
    model = sys.argv[1]
    binary = os.path.join(HERE, "..", "bin", "llm-server")
    tmpl = jinja2.Environment(loader=jinja2.BaseLoader()).from_string(gguf_template(model))
    dump = tempfile.mkdtemp()
    p = subprocess.Popen([binary, "--model", model, "--port", str(PORT), "--slots", "1", "--ctx", "2048", "--dump-prompts", dump],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(240):
            try:
                urllib.request.urlopen("http://127.0.0.1:%d/health" % PORT, timeout=2)
                break
            except Exception:
                time.sleep(0.5)
        bad = 0
        for i, msgs in enumerate(CASES):
            want = tmpl.render(messages=msgs, add_generation_prompt=True, bos_token="<bos>")
            before = set(os.listdir(dump))
            body = json.dumps({"messages": msgs, "max_tokens": 1}).encode()
            urllib.request.urlopen(urllib.request.Request("http://127.0.0.1:%d/v1/chat/completions" % PORT, body), timeout=120).read()
            new = sorted(set(os.listdir(dump)) - before)
            got = "<bos>" + open(os.path.join(dump, new[0])).read()
            ok = got == want
            bad += not ok
            print("case %d: %s" % (i + 1, "same" if ok else "DIFFERENT"))
            if not ok:
                print("  template: %r\n  server:   %r" % (want, got))
        sys.exit(1 if bad else 0)
    finally:
        p.terminate()


if __name__ == "__main__":
    main()
