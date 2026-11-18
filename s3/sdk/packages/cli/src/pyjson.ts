// JSON read and written exactly as Python's json module does it (json.loads, then json.dumps(ensure_ascii=False)).
//
// tools/espk.py stores the manifest as json.dumps(json.loads(text), ensure_ascii=False, separators=(",", ":")) and
// installs it into the simulator with the default separators. To write byte-identical packages we keep what
// JSON.parse loses: integer vs float (1 vs 1.0), integers past 2^53, key order (JS puts "0", "1"... first) and
// Python's float spelling (1e-05, 1e+16, 100.0).

export class PyInt {
  constructor(readonly text: string) {}
}
export class PyFloat {
  constructor(readonly value: number) {}
}
export class PyObj {
  readonly keys: string[] = [];
  readonly map = new Map<string, PyVal>();
  set(k: string, v: PyVal): void {
    if (!this.map.has(k)) this.keys.push(k);
    this.map.set(k, v); // a repeated key keeps its first place and its last value, as a Python dict
  }
  get(k: string): PyVal | undefined {
    return this.map.get(k);
  }
  has(k: string): boolean {
    return this.map.has(k);
  }
  delete(k: string): void {
    if (!this.map.delete(k)) return;
    this.keys.splice(this.keys.indexOf(k), 1);
  }
  entries(): [string, PyVal][] {
    return this.keys.map((k) => [k, this.map.get(k) as PyVal]);
  }
}
export type PyVal = null | boolean | string | PyInt | PyFloat | PyVal[] | PyObj;

export class JsonError extends Error {
  constructor(message: string, readonly pos: number, readonly line: number, readonly col: number) {
    super(`${message} (line ${line} column ${col})`);
  }
}

/** json.loads: strict JSON. Rejects what Python accepts but no JSON reader should (NaN, Infinity, a BOM). */
export function parse(text: string): PyVal {
  let i = 0;
  const fail = (msg: string, at = i): never => {
    let line = 1, col = 1;
    for (let k = 0; k < at && k < text.length; k++) {
      if (text[k] === "\n") line++, (col = 1);
      else col++;
    }
    throw new JsonError(msg, at, line, col);
  };
  if (text.charCodeAt(0) === 0xfeff) fail("starts with a byte order mark (save the file as UTF-8 without BOM)", 0);
  const ws = () => {
    while (i < text.length) {
      const c = text[i];
      if (c === " " || c === "\t" || c === "\n" || c === "\r") i++;
      else break;
    }
  };
  const value = (depth: number): PyVal => {
    if (depth > 200) fail("nested too deep");
    ws();
    const c = text[i];
    if (c === "{") {
      i++;
      const o = new PyObj();
      ws();
      if (text[i] === "}") {
        i++;
        return o;
      }
      for (;;) {
        ws();
        if (text[i] !== '"') fail("expected a key in double quotes");
        const k = str();
        ws();
        if (text[i] !== ":") fail("expected ':'");
        i++;
        o.set(k, value(depth + 1));
        ws();
        if (text[i] === ",") {
          i++;
          continue;
        }
        if (text[i] === "}") {
          i++;
          return o;
        }
        fail("expected ',' or '}'");
      }
    }
    if (c === "[") {
      i++;
      const a: PyVal[] = [];
      ws();
      if (text[i] === "]") {
        i++;
        return a;
      }
      for (;;) {
        a.push(value(depth + 1));
        ws();
        if (text[i] === ",") {
          i++;
          continue;
        }
        if (text[i] === "]") {
          i++;
          return a;
        }
        fail("expected ',' or ']'");
      }
    }
    if (c === '"') return str();
    if (text.startsWith("true", i)) return (i += 4), true;
    if (text.startsWith("false", i)) return (i += 5), false;
    if (text.startsWith("null", i)) return (i += 4), null;
    const m = /^-?(?:0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?/.exec(text.slice(i, i + 400));
    if (m && m[0].length) {
      i += m[0].length;
      if (m[1] === undefined && m[2] === undefined) return new PyInt(m[0] === "-0" ? "0" : m[0].replace(/^-0$/, "0"));
      const v = Number(m[0]);
      if (!Number.isFinite(v)) fail("number out of range", i - m[0].length);
      return new PyFloat(v);
    }
    if (i >= text.length) fail("unexpected end of the text");
    return fail(`unexpected character ${JSON.stringify(c)}`);
  };
  const str = (): string => {
    const start = i;
    i++;
    let out = "";
    for (;;) {
      if (i >= text.length) fail("unterminated string", start);
      const ch = text.charCodeAt(i);
      if (ch === 0x22) {
        i++;
        break;
      }
      if (ch < 0x20) fail("control character in a string (write \\n, \\t, ...)");
      if (ch === 0x5c) {
        const e = text[i + 1];
        const simple: Record<string, string> = { '"': '"', "\\": "\\", "/": "/", b: "\b", f: "\f", n: "\n", r: "\r", t: "\t" };
        if (e in simple) {
          out += simple[e];
          i += 2;
          continue;
        }
        if (e === "u") {
          const h = text.slice(i + 2, i + 6);
          if (!/^[0-9a-fA-F]{4}$/.test(h)) fail("bad \\u escape");
          out += String.fromCharCode(parseInt(h, 16));
          i += 6;
          continue;
        }
        fail("bad escape");
      }
      out += text[i];
      i++;
    }
    // A lone surrogate cannot be written as UTF-8 (Python's .encode() fails on it too).
    for (let k = 0; k < out.length; k++) {
      const u = out.charCodeAt(k);
      if (u >= 0xd800 && u <= 0xdbff) {
        const n = out.charCodeAt(k + 1);
        if (n >= 0xdc00 && n <= 0xdfff) {
          k++;
          continue;
        }
        fail("lone UTF-16 surrogate in a string", start);
      }
      if (u >= 0xdc00 && u <= 0xdfff) fail("lone UTF-16 surrogate in a string", start);
    }
    return out;
  };
  const v = value(0);
  ws();
  if (i < text.length) fail("extra data after the JSON value");
  return v;
}

/** Python's repr() of a float: the shortest digits that read back the same, exponent when decpt <= -4 or > 16. */
export function pyFloatRepr(x: number): string {
  if (Number.isNaN(x)) return "NaN";
  if (x === Infinity) return "Infinity";
  if (x === -Infinity) return "-Infinity";
  if (x === 0) return Object.is(x, -0) ? "-0.0" : "0.0";
  const neg = x < 0;
  const [mant, expS] = Math.abs(x).toExponential().split("e");
  const exp = parseInt(expS, 10);
  const digits = mant.replace(".", "");
  const decpt = exp + 1;
  let s: string;
  if (decpt <= -4 || decpt > 16) {
    s = digits[0] + (digits.length > 1 ? "." + digits.slice(1) : "") + "e" + (exp < 0 ? "-" : "+") + String(Math.abs(exp)).padStart(2, "0");
  } else if (decpt <= 0) {
    s = "0." + "0".repeat(-decpt) + digits;
  } else if (decpt >= digits.length) {
    s = digits + "0".repeat(decpt - digits.length) + ".0";
  } else {
    s = digits.slice(0, decpt) + "." + digits.slice(decpt);
  }
  return (neg ? "-" : "") + s;
}

const ESC: Record<string, string> = { '"': '\\"', "\\": "\\\\", "\b": "\\b", "\f": "\\f", "\n": "\\n", "\r": "\\r", "\t": "\\t" };

/** json.dumps(s, ensure_ascii=False) of a string. */
export function pyStr(s: string): string {
  // eslint-disable-next-line no-control-regex
  return '"' + s.replace(/[\x00-\x1f\\"]/g, (c) => ESC[c] ?? "\\u" + c.charCodeAt(0).toString(16).padStart(4, "0")) + '"';
}

export interface DumpOpts {
  /** separators=(",", ":") (true) or Python's default (", ", ": ") */
  compact?: boolean;
  /** indent=N (Python puts ", " -> "," at line ends when indenting) */
  indent?: number;
}

/** json.dumps(v, ensure_ascii=False, ...) */
export function dumps(v: PyVal, o: DumpOpts = {}): string {
  const item = o.indent !== undefined ? "," : o.compact ? "," : ", ";
  const kv = o.compact ? ":" : ": ";
  const rec = (x: PyVal, lvl: number): string => {
    if (x === null) return "null";
    if (x === true) return "true";
    if (x === false) return "false";
    if (typeof x === "string") return pyStr(x);
    if (x instanceof PyInt) return x.text;
    if (x instanceof PyFloat) return pyFloatRepr(x.value);
    const nl = o.indent !== undefined ? "\n" + " ".repeat(o.indent * (lvl + 1)) : "";
    const end = o.indent !== undefined ? "\n" + " ".repeat(o.indent * lvl) : "";
    if (Array.isArray(x)) {
      if (!x.length) return "[]";
      return "[" + nl + x.map((e) => rec(e, lvl + 1)).join(item + nl) + end + "]";
    }
    const es = x.entries();
    if (!es.length) return "{}";
    return "{" + nl + es.map(([k, e]) => pyStr(k) + kv + rec(e, lvl + 1)).join(item + nl) + end + "}";
  };
  return rec(v, 0);
}

/** A plain JS value (for checks and printing). Integers past 2^53 become strings so nothing is silently rounded. */
export function toPlain(v: PyVal): unknown {
  if (v === null || typeof v === "boolean" || typeof v === "string") return v;
  if (v instanceof PyInt) {
    const n = Number(v.text);
    return Number.isSafeInteger(n) ? n : v.text;
  }
  if (v instanceof PyFloat) return v.value;
  if (Array.isArray(v)) return v.map(toPlain);
  const o: Record<string, unknown> = Object.create(null);
  for (const [k, e] of v.entries()) o[k] = toPlain(e);
  return o;
}

/** A plain JS value as Python values (objects keep their key order; whole numbers become ints). */
export function fromPlain(v: unknown): PyVal {
  if (v === null || v === undefined) return null;
  if (typeof v === "boolean" || typeof v === "string") return v;
  if (typeof v === "number") return Number.isInteger(v) ? new PyInt(String(v)) : new PyFloat(v);
  if (typeof v === "bigint") return new PyInt(v.toString());
  if (v instanceof PyInt || v instanceof PyFloat || v instanceof PyObj) return v;
  if (Array.isArray(v)) return v.map(fromPlain);
  const o = new PyObj();
  for (const [k, e] of Object.entries(v as object)) if (e !== undefined) o.set(k, fromPlain(e));
  return o;
}

/** JSON paths ("code", "icon.bg[0]") whose numbers were written as floats (1.0, 1e3). */
export function floatPaths(v: PyVal, path = "", out = new Set<string>()): Set<string> {
  if (v instanceof PyFloat) out.add(path);
  else if (Array.isArray(v)) v.forEach((e, i) => floatPaths(e, `${path}[${i}]`, out));
  else if (v instanceof PyObj) for (const [k, e] of v.entries()) floatPaths(e, path ? `${path}.${k}` : k, out);
  return out;
}
