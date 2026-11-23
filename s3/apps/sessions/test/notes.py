#!/usr/bin/env python3
"""Puts Sessions' server notes into a simulator data folder's dash.json (the dashboard's "notes"), as the server sends
them while Claude waits (desktop-protocol.md §9.6, with the pop-up fields of src/popnote.h):

    python3 apps/sessions/test/notes.py SIM_DATA/dash.json [perm|risky|file|two|wait|status|next|late]
"""
import json
import os
import sys
import time

path, kind = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "perm"
now = int(time.time() * 1000)
if os.environ.get("SIM_CLOCK"):  # the simulator's own clock (run.sh CLOCK): the notes' expiry follows it
    now = int(time.mktime(time.strptime(os.environ["SIM_CLOCK"], "%Y-%m-%d %H:%M:%S")) * 1000)


def perm(sid, req, name, cmd, why, risky=False, file=False):
    return {"title": f"Claude ({name}): Bash {cmd}", "time": time.strftime("%H:%M"), "kind": "claude_perm",
            "app": "dev.espos.sessions", "arg": f"900:{sid}:{req}", "ref": req, "exp_ms": now + 522000,
            "pop": {"head": name, "title": "Claude wants to edit a file" if file else "Claude wants to run a command",
                    "mono": cmd, "text": why, "say": f"{name} is waiting for your OK"},
            "acts": [{"id": "deny", "post": "/send", "body": {"mac": 900, "s": sid, "kind": "deny", "req": req}},
                     {"id": "approve", "post": "/send", "body": {"mac": 900, "s": sid, "kind": "approve", "req": req},
                      "risky": risky}]}  # from the shared risky list (docs/desktop-risky.json), as the server


d = json.load(open(path))
notes = [n for n in d.get("notes", []) if "app" not in n]
if kind == "risky":
    notes.append(perm("s-1", "r-1", "espos", "rm -rf build dist ~/.cache/espos", "Clean the old build outputs", risky=True))
elif kind == "file":
    notes.append(perm("s-1", "r-1", "espos", "src/n2/desktop/Page.tsx  +1 -1", "Fall back to a default title", file=True))
elif kind == "status":
    notes.append({"title": "Claude: 2 sessions", "time": time.strftime("%H:%M"), "kind": "claude_status",
                  "app": "dev.espos.sessions", "arg": "900", "exp_ms": now + 600000,
                  "pop": {"head": "Claude", "title": "What Claude is doing",
                          "text": "espos waits for your approval to push. web finished the route and asks about the docs."},
                  "acts": []})
elif kind == "wait":
    notes.append({"title": "Claude (web) is waiting for you", "time": time.strftime("%H:%M"), "kind": "claude_wait",
                  "app": "dev.espos.sessions", "arg": "900:s-2", "exp_ms": now + 1800000,
                  "pop": {"head": "web", "title": "Claude is waiting for you", "text": "Should I also update the docs page?",
                          "say": "web is waiting for you"},
                  "acts": []})
elif kind == "next":  # another request alone (a later dashboard: run.sh announcelock)
    notes.append(perm("s-6", "r-6", "api", "npm publish", "Publish 2.4.0 to npm"))
elif kind == "late":
    notes.append(perm("s-7", "r-7", "web", "git push origin main", "Push the docs page"))
else:
    notes.append(perm("s-1", "r-1", "espos", "git push origin main", "Push the settings fix to GitHub"))
    if kind == "two":
        notes.append(perm("s-6", "r-6", "api", "npm publish", "Publish 2.4.0 to npm"))
        notes.append(perm("s-7", "r-7", "web", "git push origin main", "Push the docs page"))
d["notes"] = notes
json.dump(d, open(path, "w"), ensure_ascii=False)
