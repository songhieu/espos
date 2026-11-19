#!/usr/bin/env python3
"""The simulator's network for the dog assistant's turn (src/pet/talk.cpp, SIM build): one HTTPS request to the live
API, the answer written to stdout as the device would read it from its socket:

    STATUS <code>\\n  then "<Header>: <value>\\n" lines, then "\\n", then the body's bytes as they arrive (de-chunked,
    in whatever pieces the network gives: the simulator splits them again at odd sizes to test the byte carry)

    simnet.py METHOD PATH [BODYFILE CONTENT-TYPE]

The device token comes from the environment (SIM_DEVICE_TOKEN, set by simpair.py for its own run): never an argument,
never written anywhere.
"""
import http.client
import os
import sys


def main():
    method, path = sys.argv[1], sys.argv[2]
    body = open(sys.argv[3], "rb").read() if len(sys.argv) > 3 and sys.argv[3] != "-" else None
    ctype = sys.argv[4] if len(sys.argv) > 4 else "application/json"
    tok = os.environ.get("SIM_DEVICE_TOKEN", "")
    host = os.environ.get("SIM_API_HOST", "api.espos.dev")
    out = sys.stdout.buffer
    try:
        c = http.client.HTTPSConnection(host, 443, timeout=30)
        h = {"Authorization": "Device " + tok, "User-Agent": "EspOS-S3-sim", "Accept-Encoding": "identity"}
        if body is not None:
            h["Content-Type"] = ctype
        c.request(method, path, body=body, headers=h)
        r = c.getresponse()
    except Exception as e:  # no network: the device's "no connection"
        out.write(f"STATUS -1\nX-Error: {e}\n\n".encode())
        return
    out.write(f"STATUS {r.status}\n".encode())
    for k, v in r.getheaders():
        out.write(f"{k}: {v}\n".encode())
    out.write(b"\n")
    out.flush()
    while True:
        d = r.read1(4096)
        if not d:
            break
        out.write(d)
        out.flush()


if __name__ == "__main__":
    main()
