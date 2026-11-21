"""Fetch today's VnExpress RSS (the feeds the EspOS news job reads) into fixtures/news.local.json.

    python3 make_news_fixture.py          # assist_bench.py then seeds the EspOS server's news_cache from it

The repository ships made-up sample news in fixtures/news.json (not real articles). news.local.json, when it exists,
is used instead; it stays on your machine (.gitignore): the articles belong to their publisher.
"""
import json, re, sys, urllib.request, html, os

SECTIONS = ["tin-moi-nhat", "thoi-su", "the-gioi", "kinh-doanh", "khoa-hoc-cong-nghe", "suc-khoe"]
FEED = {"khoa-hoc-cong-nghe": "khoa-hoc"}  # VnExpress feed names that differ from the section slug

def text(s):
    s = re.sub(r"<!\[CDATA\[(.*?)\]\]>", r"\1", s, flags=re.S)
    s = re.sub(r"<[^>]+>", " ", s)
    return " ".join(html.unescape(s).split())

out = {}
for slug in SECTIONS:
    url = "https://vnexpress.net/rss/%s.rss" % FEED.get(slug, slug)
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 (local benchmark)"})
    raw = urllib.request.urlopen(req, timeout=20).read().decode("utf-8", "replace")
    items = []
    for m in re.finditer(r"<item>(.*?)</item>", raw, re.S):
        it = m.group(1)
        title = text((re.search(r"<title>(.*?)</title>", it, re.S) or [None, ""])[1])
        link = text((re.search(r"<link>(.*?)</link>", it, re.S) or [None, ""])[1])
        desc = text((re.search(r"<description>(.*?)</description>", it, re.S) or [None, ""])[1])
        if title:
            items.append({"id": re.sub(r"\D", "", link)[-8:] or str(len(items)), "title": title, "lead": desc[:400], "url": link})
        if len(items) >= 8:
            break
    out[slug] = items
    print(slug, len(items), file=sys.stderr)
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "news.local.json")
json.dump(out, open(path, "w"), ensure_ascii=False, indent=1)
print("wrote", path)
