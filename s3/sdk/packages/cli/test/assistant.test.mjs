// EspOS Assistant Protocol v1 (docs/assistant-providers.md §2): the schema, its examples, the signature and the
// server's check of a provider's answer, against the vectors the server's Go code (web/scripts/eap) runs too.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { cleanResponse, signTurn, verifySignature } from "../dist/assistant.js";
import { checkDef } from "../dist/jsonschema.js";
import { here } from "./helpers.mjs";

const SCHEMAS = path.resolve(here, "../../sdk/schemas");
const read = (f) => JSON.parse(fs.readFileSync(path.join(SCHEMAS, f), "utf8"));
const schema = read("assistant-v1.schema.json");
const examples = read("assistant-v1-examples.json");
const vectors = read("assistant-v1-vectors.json");

test("every example validates against its schema entry", () => {
  for (const name of ["manifest", "turn", "response", "error"]) {
    assert.deepEqual(checkDef(schema, name, examples[name]), [], name);
  }
  for (const ev of examples.events) assert.deepEqual(checkDef(schema, "event", ev), [], ev.event);
  const final = examples.events.find((e) => e.event === "final").data;
  assert.deepEqual(checkDef(schema, "response", final), []);
  for (const a of examples.events[0].data.actions) assert.deepEqual(checkDef(schema, "action", a), []);
});

test("the schema catches what it should", () => {
  const bad = structuredClone(examples.response);
  bad.mood = "dance";
  bad.actions.push({ id: "x", type: "alarm.add" });
  bad.extra = 1;
  const p = checkDef(schema, "response", bad);
  assert.ok(p.some((l) => l.includes("$.mood")), p.join("\n"));
  assert.ok(p.some((l) => l.includes("missing args")), p.join("\n"));
  assert.ok(p.some((l) => l.includes("unexpected extra")), p.join("\n"));
  const turn = structuredClone(examples.turn);
  delete turn.locale;
  turn.context.alarms.items[0].time = "7:00";
  const q = checkDef(schema, "turn", turn);
  assert.ok(q.some((l) => l.includes("missing locale")) && q.some((l) => l.includes("alarms.items[0].time")), q.join("\n"));
});

test("signing vectors", () => {
  for (const v of vectors.signing) {
    assert.equal(signTurn(v.secret, v.timestamp, v.request_id, v.body), v.signature);
  }
  const v = vectors.signing[0];
  const header = `t=${v.timestamp},v1=${v.signature}`;
  assert.ok(verifySignature(header, v.secret, v.request_id, v.body, v.timestamp + 10));
  assert.ok(!verifySignature(header, v.secret, v.request_id, v.body, v.timestamp + 301), "too old");
  assert.ok(!verifySignature(header, v.secret, v.request_id, v.body + " ", v.timestamp), "another body");
  assert.ok(!verifySignature(header, vectors.signing[2].secret, v.request_id, v.body, v.timestamp), "another secret");
  // rotation: the old and the new secret's signatures side by side
  const both = `t=${v.timestamp},v1=${signTurn("esps_old", v.timestamp, v.request_id, v.body)},v1=${v.signature}`;
  assert.ok(verifySignature(both, v.secret, v.request_id, v.body, v.timestamp));
});

test("the server's check of an answer (shared vectors)", () => {
  for (const c of vectors.responses) {
    const req = structuredClone(examples.turn);
    if (c.req_actions) req.capabilities.actions = c.req_actions;
    if (c.req_held) req.held = c.req_held;
    const resp = c.response === "examples.response" ? examples.response : c.response;
    const r = cleanResponse(resp, req);
    const e = c.expect;
    assert.equal(r.ok, e.ok, c.name);
    if (e.error) assert.equal(r.error, e.error, c.name);
    if (!r.ok) continue;
    assert.deepEqual(checkDef(schema, "response", r.response), [], `${c.name}: the kept answer validates`);
    if (e.dropped) assert.deepEqual(r.dropped, e.dropped, c.name);
    if (e.actions) assert.deepEqual(r.response.actions.map((a) => a.id), e.actions, c.name);
    if (e.memory) assert.deepEqual(r.response.memory.map((m) => m.op), e.memory, c.name);
    if (e.memory_kinds) assert.deepEqual(r.response.memory.filter((m) => m.op === "remember").map((m) => m.kind), e.memory_kinds, c.name);
    if (e.mood) assert.equal(r.response.mood, e.mood, c.name);
    if (e.topic) assert.equal(r.response.topic, e.topic, c.name);
    if (e.say_max) assert.ok([...r.response.say.text].length <= e.say_max, c.name);
    if (e.say_ends) assert.ok(r.response.say.text.endsWith(e.say_ends), c.name);
    if (e.listen !== undefined) assert.equal(r.response.follow_up.listen, e.listen, c.name);
    if (e.note_len !== undefined) assert.equal([...r.response.session.note].length, e.note_len, c.name);
    if (e.summary_null) assert.equal(r.response.session.summary, null, c.name);
    if (e.end !== undefined) assert.equal(r.response.session.end, e.end, c.name);
    if (e.audio !== undefined) assert.equal(!!r.response.audio, e.audio, c.name);
    if (e.held !== undefined) assert.deepEqual(r.response.held, e.held, c.name);
    if (e.ask_first !== undefined) assert.equal(r.response.ask_first, e.ask_first, c.name);
  }
});
