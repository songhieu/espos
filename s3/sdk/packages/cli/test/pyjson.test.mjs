// Python-compatible JSON: what tools/espk.py writes into a package's manifest.
import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { test } from "node:test";
import { dumps, parse, pyFloatRepr, toPlain } from "../dist/pyjson.js";
import { python } from "./helpers.mjs";

const pyDumps = (text, compact) => {
  const r = spawnSync(python, ["-c", `import json,sys; t=sys.stdin.read(); print(json.dumps(json.loads(t), ensure_ascii=False${compact ? ', separators=(",", ":")' : ""}), end="")`], { input: text, encoding: "utf8" });
  if (r.status !== 0) throw new Error(r.stderr);
  return r.stdout;
};

const CASES = [
  '{"id": "vn.a.b", "code": 10000, "f": 1.0, "e": 1e3, "small": 0.00001, "big": 1e16, "neg": -0.0, "x": 123456789012345678901234567890}',
  '{"b": 1, "a": 2, "0": 3, "10": 4, "1": 5}',
  '{"a": 1, "b": 2, "a": 3}',
  '{"s": "Tập trung\\n\\t\\"q\\" \\\\ \\u0001 \\u001f \\u007f \\u2028 é 😀 \\ud83d\\ude00", "slash": "a\\/b"}',
  '[1, 2.5, -3, 1.5e-7, 123.456, 0.1, 1e21, 5e-324, 1.7976931348623157e308, 100.0, 1e15, 1.25e+16]',
  '{"nested": {"l": [true, false, null, {"k": []}, {}], "z": -12.0}}',
];

test("dumps matches Python's json.dumps(ensure_ascii=False), compact and default", { skip: !python && "python3 not found" }, () => {
  for (const c of CASES) {
    assert.equal(dumps(parse(c), { compact: true }), pyDumps(c, true), c);
    assert.equal(dumps(parse(c)), pyDumps(c, false), c);
  }
});

test("float repr matches Python repr for random doubles", { skip: !python && "python3 not found" }, () => {
  const xs = [];
  for (let i = 0; i < 400; i++) {
    const e = Math.floor(Math.random() * 60) - 30;
    xs.push(Math.random() * Math.pow(10, e) * (Math.random() < 0.5 ? -1 : 1));
  }
  xs.push(0.1 + 0.2, 1 / 3, 2 / 3, 1e-4, 1e-5, 9.999e15, 1e16, 12345678901234567, 0.5, 3);
  const r = spawnSync(python, ["-c", "import sys,json; print('\\n'.join(repr(float(x)) for x in json.load(sys.stdin)))"], { input: JSON.stringify(xs), encoding: "utf8" });
  const want = r.stdout.trim().split("\n");
  xs.forEach((x, i) => assert.equal(pyFloatRepr(x), want[i], String(x)));
});

test("parse refuses what no JSON reader should take", () => {
  for (const bad of ["﻿{}", "{\"a\": NaN}", "{\"a\": 1,}", "{'a': 1}", '{"a": "\\ud800"}', '{"a": "x\ny"}', "[1] 2", "{\"a\": 1e999}"]) assert.throws(() => parse(bad), undefined, bad);
});

test("toPlain keeps big integers as text", () => {
  const v = toPlain(parse('{"b": 1, "1": 2, "n": 123456789012345678901, "f": 2.5}'));
  assert.equal(v.n, "123456789012345678901");
  assert.equal(v.b, 1);
  assert.equal(v.f, 2.5);
});
