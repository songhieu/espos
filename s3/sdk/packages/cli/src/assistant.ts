// EspOS Assistant Protocol v1 (s3/docs/assistant-providers.md §2): the turn's signature and the server's check of
// a provider's answer. The server does the same in Go (web/scripts/eap/eap.go): sdk/schemas/assistant-v1-vectors.json
// holds the cases both run, so `espos ai test` tells a developer exactly what the server will keep and drop.
import crypto from "node:crypto";

export const TOPICS = ["weather", "calendar", "alarm", "todo", "note", "news", "markets", "memory", "desk", "time", "reading", "chat", "refuse"];
export const KINDS = ["date", "pref", "family", "work", "other"];
const HHMM = /^([01][0-9]|2[0-3]):[0-5][0-9]$/;
const DAYS = /^[0-6]{0,7}$/;
const NAME = /^\p{L}[\p{L} '.\-]{0,39}$/u;
const CMD = /^[a-z]+\.[a-z_]+$/;
/** At most this many actions in one answer (a whole list deleted at once, held for a yes, fits). */
export const MAX_ACTIONS = 30;
export const MAX_MEMORY = 20;

/** hex(HMAC-SHA256(secret, timestamp + "." + request_id + "." + body)): the "v1" of EspOS-Signature (§2.9). */
export function signTurn(secret: string, timestamp: number | string, requestId: string, body: string): string {
  return crypto.createHmac("sha256", secret).update(`${timestamp}.${requestId}.${body}`).digest("hex");
}

/** Checks an EspOS-Signature header ("t=<ts>,v1=<hex>[,v1=<hex>]") for a body, within maxSkew seconds of now. */
export function verifySignature(header: string, secret: string, requestId: string, body: string, nowS: number, maxSkew = 300): boolean {
  const parts = header.split(",").map((p) => p.trim().split("="));
  const t = parts.find((p) => p[0] === "t")?.[1] ?? "";
  if (!/^\d{1,12}$/.test(t) || Math.abs(nowS - Number(t)) > maxSkew) return false;
  const want = Buffer.from(signTurn(secret, t, requestId, body), "hex");
  return parts.some((p) => p[0] === "v1" && /^[0-9a-f]{64}$/.test(p[1] ?? "") && crypto.timingSafeEqual(Buffer.from(p[1], "hex"), want));
}

type Obj = Record<string, any>;
const isObj = (v: unknown): v is Obj => typeof v === "object" && v !== null && !Array.isArray(v);
const runes = (s: string) => [...s].length;
const cutRunes = (s: string, n: number) => [...s].slice(0, n).join("");
const str = (v: unknown, max: number) => typeof v === "string" && runes(v) <= max;
const oneLine = (s: string) => s.split(/\s+/).filter(Boolean).join(" ");

/** The refs of one context list (alarms, todo, notes, memories) of the request. */
function refsOf(req: Obj, block: string): Set<string> {
  const items = req?.context?.[block]?.items;
  return new Set(Array.isArray(items) ? items.map((x: Obj) => x?.ref).filter((r: unknown) => typeof r === "string") : []);
}

const ARGS: Record<string, string[]> = {
  "alarm.add": ["time", "days", "label"], "alarm.update": ["ref", "time", "days", "label", "on"], "alarm.delete": ["ref"],
  "todo.add": ["text"], "todo.update": ["ref", "text"], "todo.done": ["ref", "done"], "todo.delete": ["ref"],
  "note.add": ["title", "body"], "note.update": ["ref", "title", "body", "append"], "note.delete": ["ref"],
  "setting.set": ["key", "value"], "profile.name": ["name"], "desk.run": ["cmd", "args"],
};
const LIST_OF: Record<string, string> = { alarm: "alarms", todo: "todo", note: "notes" };

/** Why one action is not acceptable ("" when it is): "args" or "ref". */
function actionWhy(type: string, a: Obj, req: Obj): string {
  for (const k of Object.keys(a)) if (!ARGS[type].includes(k)) return "args";
  const list = LIST_OF[type.split(".")[0]];
  if (list && ARGS[type].includes("ref")) {
    if (typeof a.ref !== "string") return "args";
    if (!refsOf(req, list).has(a.ref)) return "ref";
  }
  switch (type) {
    case "alarm.add":
      if (typeof a.time !== "string" || !HHMM.test(a.time)) return "args";
      if (a.days !== undefined && !(typeof a.days === "string" && DAYS.test(a.days))) return "args";
      if (a.label !== undefined && !str(a.label, 40)) return "args";
      return "";
    case "alarm.update":
      if (a.time === undefined && a.days === undefined && a.label === undefined && a.on === undefined) return "args";
      if (a.time !== undefined && !(typeof a.time === "string" && HHMM.test(a.time))) return "args";
      if (a.days !== undefined && !(typeof a.days === "string" && DAYS.test(a.days))) return "args";
      if (a.label !== undefined && !str(a.label, 40)) return "args";
      if (a.on !== undefined && typeof a.on !== "boolean") return "args";
      return "";
    case "todo.add":
      return typeof a.text === "string" && a.text.trim() !== "" && runes(a.text) <= 120 ? "" : "args";
    case "todo.update":
      return typeof a.text === "string" && a.text.trim() !== "" && runes(a.text) <= 120 ? "" : "args";
    case "todo.done":
      return typeof a.done === "boolean" ? "" : "args";
    case "note.add": {
      const t = a.title ?? "", b = a.body ?? "";
      return str(t, 60) && str(b, 2000) && (t.trim() !== "" || b.trim() !== "") ? "" : "args";
    }
    case "note.update": {
      if (a.title === undefined && a.body === undefined && a.append === undefined) return "args";
      return (a.title === undefined || str(a.title, 60)) && (a.body === undefined || str(a.body, 2000)) && (a.append === undefined || str(a.append, 2000)) ? "" : "args";
    }
    case "setting.set": {
      const cap = (req?.capabilities?.settings ?? []).find((s: Obj) => s?.key === a.key);
      if (!cap) return "args";
      const v = a.value;
      if (typeof v !== "string") return "args";
      if (cap.type === "int") {
        if (!/^-?\d{1,6}$/.test(v)) return "args";
        const n = Number(v);
        if ((cap.min !== undefined && n < cap.min) || (cap.max !== undefined && n > cap.max)) return "args";
        return "";
      }
      if (cap.type === "enum") return (cap.values ?? []).includes(v) ? "" : "args";
      if (cap.type === "time") return HHMM.test(v) ? "" : "args";
      return runes(v) >= 1 && runes(v) <= 60 ? "" : "args";
    }
    case "profile.name":
      return typeof a.name === "string" && NAME.test(oneLine(a.name)) ? "" : "args";
    case "desk.run":
      // any command by its name: the computer's own queue decides (off, offline, not allowed) and says why
      if (typeof a.cmd !== "string" || !CMD.test(a.cmd)) return "args";
      return a.args === undefined || isObj(a.args) ? "" : "args";
  }
  return ""; // the deletes: the ref was checked above
}

export interface Cleaned {
  ok: boolean;
  error?: string;
  response?: Obj;
  dropped: string[];
}

/**
 * The server's check of a provider's answer to req (§2.6, §2.8, §2.11): a wrong version, another request's id or
 * no words to say is a failure (error); anything else that does not fit is dropped and named in dropped, and the rest
 * is kept: say (cut to the device's length), heard, mood, topic, at most 5 actions the request allows with valid
 * arguments and known refs, at most 3 memory operations, follow_up, session, audio.
 */
export function cleanResponse(resp: unknown, req: Obj): Cleaned {
  const dropped: string[] = [];
  if (!isObj(resp)) return { ok: false, error: "not_object", dropped };
  if (resp.v !== "1") return { ok: false, error: "bad_version", dropped };
  if (resp.request_id !== req?.request_id) return { ok: false, error: "wrong_request", dropped };
  const say = resp.say;
  if (!isObj(say) || typeof say.text !== "string" || oneLine(say.text) === "") return { ok: false, error: "no_say", dropped };
  const out: Obj = { v: "1", request_id: resp.request_id };
  const long = say.long === true;
  const max = (long ? req?.device?.say_max_chars_long : req?.device?.say_max_chars) ?? (long ? 950 : 500);
  let text = oneLine(say.text);
  if (runes(text) > max) {
    text = cutRunes(text, max);
    const end = Math.max(text.lastIndexOf("."), text.lastIndexOf("!"), text.lastIndexOf("?"));
    if (end > (max * 2) / 5) text = text.slice(0, end + 1);
    dropped.push("say_cut");
  }
  out.say = { text, long };
  if (say.lang !== undefined) {
    if (str(say.lang, 16)) out.say.lang = say.lang;
    else dropped.push("say_lang");
  }
  if (say.if_not_done !== undefined) {
    if (typeof say.if_not_done === "string" && runes(say.if_not_done) <= 300 && oneLine(say.if_not_done) !== "") out.say.if_not_done = oneLine(say.if_not_done);
    else dropped.push("if_not_done");
  }
  if (resp.heard !== undefined) {
    if (typeof resp.heard === "string") out.heard = cutRunes(oneLine(resp.heard), 500);
    else dropped.push("heard");
  }
  const moods: string[] = req?.capabilities?.moods ?? [];
  out.mood = "happy";
  if (resp.mood !== undefined) {
    if (typeof resp.mood === "string" && moods.includes(resp.mood)) out.mood = resp.mood;
    else dropped.push("mood");
  }
  out.topic = "chat";
  if (resp.topic !== undefined) {
    if (typeof resp.topic === "string" && TOPICS.includes(resp.topic)) out.topic = resp.topic;
    else dropped.push("topic");
  }
  const allowed: string[] = req?.capabilities?.actions ?? [];
  out.actions = [];
  if (resp.actions !== undefined) {
    if (!Array.isArray(resp.actions)) dropped.push("actions");
    else {
      const ids = new Set<string>();
      resp.actions.forEach((a: unknown, i: number) => {
        if (i >= MAX_ACTIONS) {
          if (i === MAX_ACTIONS) dropped.push("actions_over");
          return;
        }
        const name = isObj(a) && typeof a.id === "string" && a.id !== "" ? a.id : String(i);
        if (!isObj(a) || typeof a.id !== "string" || a.id === "" || runes(a.id) > 32 || typeof a.type !== "string" || !isObj(a.args)) {
          dropped.push(`action:${name}:shape`);
          return;
        }
        if (!ARGS[a.type] || !allowed.includes(a.type)) {
          dropped.push(`action:${name}:type`);
          return;
        }
        if (ids.has(a.id)) {
          dropped.push(`action:${name}:dup_id`);
          return;
        }
        const why = actionWhy(a.type, a.args, req);
        if (why) {
          dropped.push(`action:${name}:${why}`);
          return;
        }
        ids.add(a.id);
        out.actions.push({ id: a.id, type: a.type, args: a.args });
      });
    }
  }
  out.memory = [];
  if (resp.memory !== undefined) {
    if (!Array.isArray(resp.memory) || !allowed.includes("memory")) dropped.push("memory");
    else {
      const refs = refsOf(req, "memories");
      resp.memory.forEach((m: unknown, i: number) => {
        if (i >= MAX_MEMORY) {
          if (i === MAX_MEMORY) dropped.push("memory_over");
          return;
        }
        if (!isObj(m)) return void dropped.push(`memory:${i}:shape`);
        const fact = typeof m.fact === "string" ? oneLine(m.fact) : "";
        if (m.op === "remember") {
          if (fact === "" || runes(fact) > 160) return void dropped.push(`memory:${i}:fact`);
          out.memory.push({ op: "remember", fact, kind: KINDS.includes(m.kind) ? m.kind : "other" });
        } else if (m.op === "update") {
          if (!refs.has(m.ref)) return void dropped.push(`memory:${i}:ref`);
          if (fact === "" || runes(fact) > 160) return void dropped.push(`memory:${i}:fact`);
          out.memory.push({ op: "update", ref: m.ref, fact });
        } else if (m.op === "forget") {
          // by ref, or by words ("match": the server finds at most 3 memories that hold them)
          if (typeof m.match === "string" && m.ref === undefined) {
            const match = oneLine(m.match);
            if (match === "" || runes(match) > 80) return void dropped.push(`memory:${i}:match`);
            out.memory.push({ op: "forget", match });
            return;
          }
          if (!refs.has(m.ref)) return void dropped.push(`memory:${i}:ref`);
          out.memory.push({ op: "forget", ref: m.ref });
        } else dropped.push(`memory:${i}:op`);
      });
    }
  }
  // the person's answer to what the server holds (request "held"), and a wish to hold this turn's actions
  out.ask_first = resp.ask_first === true;
  if (resp.held !== undefined) {
    const rh = isObj(req?.held) ? req.held : {};
    const kept: Obj = {};
    let bad = !isObj(resp.held);
    for (const [k, v] of Object.entries(isObj(resp.held) ? resp.held : {})) {
      const okv = v === "yes" || v === "no" || (k === "desk" && (v === "unclear" || v === "other"));
      if ((k !== "actions" && k !== "desk") || !okv || rh[k] === undefined) bad = true;
      else kept[k] = v;
    }
    if (bad) dropped.push("held");
    if (Object.keys(kept).length > 0) out.held = kept;
  }
  out.follow_up = { listen: isObj(resp.follow_up) && resp.follow_up.listen === true };
  const ses = isObj(resp.session) ? resp.session : {};
  out.session = {
    note: typeof ses.note === "string" ? cutRunes(oneLine(ses.note), 100).trim() : "",
    summary: typeof ses.summary === "string" && oneLine(ses.summary) !== "" ? cutRunes(oneLine(ses.summary), 1000) : null,
    end: ses.end === true,
  };
  if (resp.audio !== undefined) {
    const rates: number[] = req?.capabilities?.audio_out?.rates ?? [];
    const a = resp.audio;
    if (isObj(a) && a.format === "pcm16" && Number.isInteger(a.rate) && (rates.length === 0 || rates.includes(a.rate)) && typeof a.data === "string" && a.data !== "") {
      out.audio = { format: "pcm16", rate: a.rate, data: a.data };
    } else dropped.push("audio");
  }
  return { ok: true, response: out, dropped };
}
