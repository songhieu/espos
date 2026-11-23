// {{name}}: a small provider for the EspOS Assistant Protocol v1 (espos.dev/docs/guides/assistant). Node 20, no
// dependencies. It checks the server's signature, then answers the weather from the request, sets an alarm, takes a
// yes for something held, and asks again when nothing was heard. Make it yours: replace answer() with your model.
//
//   ESPOS_SIGNING_SECRET=<espos ai secret's secret> node provider/server.mjs        (PORT, default 8787)
//   espos ai test http://localhost:8787/espos/turn
import crypto from "node:crypto";
import http from "node:http";

const SECRET = process.env.ESPOS_SIGNING_SECRET ?? "";
const PORT = Number(process.env.PORT ?? 8787);
if (!SECRET) throw new Error("set ESPOS_SIGNING_SECRET (espos ai secret makes one)");

const MANIFEST = {
  protocol: ["1"], name: "{{name}}", endpoint: "https://ai.example.com/espos/turn", auth: { hmac: true, bearer: "none" },
  input: { text: true }, output: { stream: false }, languages: ["*"], persona: "espos", memory: "espos",
  scopes: { required: ["turn.text", "locale"], optional: ["place", "weather", "alarms", "act.alarms"] },
  actions: ["alarm.add"],
};

// EspOS-Signature: t=<unix seconds>,v1=<hex HMAC-SHA256(secret, t + "." + request_id + "." + body)>[,v1=...]
const seen = new Map(); // request ids of the last 10 minutes: a replay is refused
function signed(req, body) {
  const parts = String(req.headers["espos-signature"] ?? "").split(",").map((p) => p.trim().split("="));
  const t = parts.find((p) => p[0] === "t")?.[1] ?? "";
  const id = String(req.headers["espos-request-id"] ?? "");
  if (!/^\d+$/.test(t) || Math.abs(Date.now() / 1000 - Number(t)) > 300 || !id || seen.has(id)) return false;
  const want = crypto.createHmac("sha256", SECRET).update(`${t}.${id}.${body}`).digest();
  const ok = parts.some((p) => p[0] === "v1" && /^[0-9a-f]{64}$/.test(p[1] ?? "") && crypto.timingSafeEqual(Buffer.from(p[1], "hex"), want));
  if (ok) seen.set(id, Date.now());
  for (const [k, at] of seen) if (Date.now() - at > 600000) seen.delete(k);
  return ok;
}

// One turn: the request in, the answer out (the schema: @espos/sdk schemas/assistant-v1.schema.json).
function answer(turn) {
  const text = (turn.turn?.input?.text ?? "").trim();
  const name = turn.user?.name ? `, ${turn.user.name}` : "";
  const out = { v: "1", request_id: turn.request_id, heard: text, mood: "happy", topic: "chat" };
  if (!text) return { ...out, say: { text: "I didn't catch that. Say it again?" }, follow_up: { listen: true } };
  if (turn.held?.actions && /\b(yes|ok|sure|do it)\b/i.test(text)) return { ...out, say: { text: "Done." }, held: { actions: "yes" } };
  const alarm = text.match(/alarm (?:for|at) (\d{1,2})(?::(\d{2}))?\s*(am|pm)?/i);
  if (alarm && turn.capabilities?.actions?.includes("alarm.add")) {
    let h = Number(alarm[1]) % 12 + (alarm[3]?.toLowerCase() === "pm" ? 12 : 0);
    if (!alarm[3] && Number(alarm[1]) >= 12) h = Number(alarm[1]);
    const time = `${String(h).padStart(2, "0")}:${alarm[2] ?? "00"}`;
    return { ...out, topic: "alarm", say: { text: `Alarm set for ${time}${name}.`, if_not_done: "I couldn't set that alarm." },
      actions: [{ id: "a1", type: "alarm.add", args: { time, days: "" } }] };
  }
  if (/weather|rain|cold|hot/i.test(text)) {
    const w = turn.context?.weather;
    const city = turn.context?.place?.city ?? "your city";
    if (w?.state !== "fresh" || !w.days?.length) return { ...out, topic: "weather", say: { text: `I have no fresh weather for ${city} yet.` } };
    const d = w.days[0];
    return { ...out, topic: "weather", say: { text: `In ${city} today: ${d.text}, ${Math.round(d.min_c)} to ${Math.round(d.max_c)} degrees${name}.` } };
  }
  return { ...out, say: { text: `You said: ${text}` } };
}

http.createServer((req, res) => {
  let body = "";
  req.on("data", (c) => (body += c));
  req.on("end", () => {
    const send = (status, obj) => {
      res.writeHead(status, { "Content-Type": "application/json" });
      res.end(JSON.stringify(obj));
    };
    if (req.method === "GET" && req.url === "/.well-known/espos-assistant.json") return send(200, MANIFEST);
    if (req.method !== "POST" || req.url !== "/espos/turn") return send(404, { error: { code: "bad_request" } });
    if (!signed(req, body)) return send(401, { error: { code: "unauthorized", message: "bad signature" } });
    let turn;
    try {
      turn = JSON.parse(body);
    } catch {
      return send(400, { error: { code: "bad_request", message: "not JSON" } });
    }
    send(200, answer(turn));
  });
}).listen(PORT, () => console.log(`{{name}} answers on http://localhost:${PORT}/espos/turn`));
