// Sessions (dev.espos.sessions): the Claude Code sessions of your Mac, live on the S3. Watch a session's terminal,
// answer it, approve or deny what it asks, with quick replies, AI suggestions, dictation and "compose with AI".
// Nothing is sent without a tap; risky input asks once. Protocol: s3/docs/desktop-protocol.md §9.
// API level 2: net.request (auth "espos": the app's own token, added by the OS), ui.term, ui.input, ui.split,
// voice.dictate, settings, app.full / onBack. Mockups: ~/code/mockups/claude-term/.
'use strict';

const API = '{espos}/v1/device/app/dev.espos.sessions';
const SCR = app.screen, LAND = SCR.landscape;
const SMALL = SCR.w === 360 || SCR.h === 360;  // the 2.8" board: its UI at 2/3 on the panel
const DARK = !!app.dark;
// The terminal's text sizes (the package's fonts/m<px>.bin): 8 px on the 3.5" (a 6 px cell: ~52 columns upright,
// ~78 sideways); the 2.8" draws its UI at 2/3, so 12 there is 8 on its panel.
const SIZES = SMALL ? [12, 14, 15, 17] : [8, 10, 12, 14];

// ---------------------------------------------------------------- words: English, the other languages in i18n.js
// (tools/mki18n.py puts them in TR when the package is built; app.lang picks one)
var TR = TR || {};
const t = (s, a) => {
  let out = (TR[app.lang] && TR[app.lang][s]) || s;
  if (a) for (const k in a) out = out.split('{' + k + '}').join(String(a[k]));
  return out;
};

// ---------------------------------------------------------------- colours (the OS theme's)
const C = DARK
  ? {fg: '#eef1f5', dim: '#9aa3b2', faint: '#6b7380', card: '#171c26', card2: '#1f2531', chip: '#1f2430', chipFg: '#c9d0d8',
     wait: '#ffc56b', waitBg: '#3a2a10', run: '#5aa9ff', ok: '#34c759', bad: '#ff6b6b', ai: '#a48bff', cmd: '#0b0e14', link: '#5aa9ff'}
  : {fg: '#16181d', dim: '#6b7280', faint: '#8a8f99', card: '#ffffff', card2: '#eceef3', chip: '#e4e6ec', chipFg: '#3a3f4a',
     wait: '#8a4b00', waitBg: '#fff3e0', run: '#0a6cff', ok: '#1e9e52', bad: '#e0383b', ai: '#6a4cf0', cmd: '#eceef3', link: '#1360d6'};

// ---------------------------------------------------------------- risky input (docs/desktop-risky.json v1)
const RISKY = [
  ['shell', /^\s*!/], ['delete', /\brm\s+-[A-Za-z]*[rRf]/], ['delete', /\bfind\b.*\s-delete\b/],
  ['force', /\bgit\s+push\b.*(\s-f\b|\s--force)/], ['force', /\bgit\s+reset\s+--hard\b/], ['force', /\bgit\s+clean\s+-[A-Za-z]*f/],
  ['sudo', /\bsudo\b/], ['pipe_shell', /\b(curl|wget)\b[^|]*\|\s*(sudo\s+)?(sh|bash|zsh)\b/],
  ['database', /\b(drop\s+(table|database|schema)|truncate\s+table)\b/i], ['disk', /\b(mkfs(\.[a-z0-9]+)?|dd\s+if=)/],
  ['kill', /\bkill(all)?\s+-9\b/], ['perms', /\bchmod\s+(-R\s+)?777\b/], ['session', /^\s*\/(clear|exit|quit|logout)\b/],
];
const riskyWhy = (text) => { for (const [why, re] of RISKY) if (re.test(text || '')) return why; return ''; };
const WHY = () => ({
  shell: t('In Claude Code, a line that starts with ! runs as a shell command at once, without Claude.'),
  delete: t('This deletes files on your Mac. It cannot be undone from here.'),
  force: t('This rewrites or throws away git history.'),
  sudo: t('This runs with administrator rights on your Mac.'),
  pipe_shell: t('This downloads a script and runs it.'),
  database: t('This deletes data in a database.'),
  disk: t('This can wipe a disk.'),
  kill: t('This force-stops programs.'),
  perms: t('This opens file permissions to everyone.'),
  session: t('This clears or ends the Claude session.'),
  interrupt: t('This stops what Claude is doing right now.'),
  grant: t('Approving lets Claude do this on your Mac now.'),
});

// ---------------------------------------------------------------- state
const S = {
  view: 'list',          // 'list' | 'term'
  macs: [], sessions: [], rev: 0, loaded: false, listErr: '', listAt: 0, failures: 0,
  cur: null,             // {mac, s}: the session on screen
  scr: {seq: 0, err: '', why: '', starting: false},
  text: '', clearField: false,  // the prompt field; clearField: emptied at the next render (after Send)
  sheet: null,           // {kind, ...}: a card over the screen
  note: null,            // {text, tone, until}: a line under the header
  sending: false, dict: '',
  font: +store.get(SMALL ? 'size28' : 'size35', SIZES[0]),
  touchAt: 0, fastUntil: 0,  // the last tap or key (short polls while you are busy), short polls until (after a key)
  sig: '',
};
if (SIZES.indexOf(S.font) < 0) S.font = SIZES[0];

const setting = (k, d) => { try { const v = settings.get(k); return v === null || v === undefined ? d : v; } catch (e) { return d; } };
// The manifest's default list is English: its words show in the device's language; the person's own words as typed.
const QUICK_DEFAULT = ['Yes', 'No', 'Continue', 'Explain more'];
const quickReplies = () => {
  const local = [t('Yes'), t('No'), t('Continue'), t('Explain more')];  // QUICK_DEFAULT in the device's language
  const v = setting('quick', null);
  return (Array.isArray(v) && v.length ? v.slice(0, 8) : QUICK_DEFAULT).map((w) => {
    const i = QUICK_DEFAULT.indexOf(w);
    return i >= 0 ? local[i] : w;
  });
};

const macOf = (id) => S.macs.find((m) => m.id === id) || null;
const cur = () => S.cur && S.sessions.find((x) => x.mac === S.cur.mac && x.id === S.cur.s) || null;
const RANK = {perm: 0, input: 1, error: 2, run: 3, idle: 4};
const sorted = () => S.sessions.slice().sort((a, b) => (RANK[a.state] ?? 5) - (RANK[b.state] ?? 5) || (b.since_ms || 0) - (a.since_ms || 0));

// "2 min", "0:42" (working), "18 min", "3 h"
function ago(ms, clock) {
  if (!ms) return '';
  const s = Math.max(0, Math.round((Date.now() - ms) / 1000));
  if (clock && s < 3600) return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
  if (s < 60) return t('now');
  if (s < 3600) return t('{n} min', {n: Math.floor(s / 60)});
  return t('{n} h', {n: Math.floor(s / 3600)});
}
function stateOf(x) {
  return {
    perm: [t('Needs approval'), C.wait], input: [t('Your turn'), C.wait], run: [t('Working'), C.run],
    idle: [t('Done'), C.dim], error: [t('Error'), C.bad],
  }[x.state] || [x.state || '', C.dim];
}
function stateLine(x, short) {
  const [w, c] = stateOf(x);
  const extra = short ? '' : x.state === 'run' ? [x.tool, ago(x.since_ms, true)].filter(Boolean).join(' ') : ago(x.since_ms);
  return ui.text(w + (extra ? ' · ' + extra : ''), {size: 12, bold: true, color: c});
}

// ---------------------------------------------------------------- the network (one scheduler: the OS sends one request
// at a time, so a held poll never sits in front of a tap for long: holds only while the person just watches)
let pollT = 0, polling = false, nonceN = 0;
const nonce = () => crypto.random(8);  // 16 hex
function call(method, path, body) {
  return net.request({method, url: API + path, body, auth: 'espos', timeout: 20000})
    .then((r) => r, (e) => ({status: 0, error: e && e.code || 'offline', sent: !!(e && e.sent)}));
}
const json = (r) => { try { return r.body ? JSON.parse(r.body) : {}; } catch (e) { return {}; } };

function schedule(ms) {
  if (pollT) timer.cancel(pollT);
  pollT = timer.after(Math.max(50, ms), () => { pollT = 0; poll(); return false; });  // false: no redraw (timers)
}
function quiet() { return !!S.sheet || S.sending; }
function poll() {
  if (polling) return;
  const watching = S.view === 'term' && cur() && !S.scr.err;
  if (watching && Date.now() - S.listAt < 5000) pollScreen();
  else pollList(watching || quiet() ? 0 : 4);
}

function listError(r) {
  const e = json(r).error || '';
  if (r.status === 404 && e === 'no_desktop') return 'not_paired';
  if (r.status === 503) return 'disabled';
  if (r.status === 401 || r.status === 403) return e === 'app_not_installed' ? 'not_installed' : 'auth';
  if (r.status === 429) return 'rate';
  if (!r.status && r.error === 'denied') return 'auth';  // the OS sends no credential: the device is not linked
  if (!r.status) return net.online === false ? 'offline_s3' : 'network';
  return 'server';
}

function sigOf() {
  if (S.view !== 'term') return JSON.stringify([S.listErr, S.macs, S.sessions, denied]);
  const x = cur(), m = x && macOf(x.mac);
  return JSON.stringify([S.listErr, x && [x.name, x.state, x.req], m && [m.online, m.mode, m.stream, m.stream_why, lockOf(m)]]);
}

function pollList(wait) {
  polling = true;
  call('GET', '/sessions?rev=' + (S.rev || 0) + '&wait=' + wait).then((r) => {
    polling = false;
    S.listAt = Date.now();
    const before = S.sig;
    if (r.status === 200) {
      const d = json(r);
      S.macs = d.macs || [], S.sessions = d.sessions || [], S.rev = d.rev || 0;
      for (const m of S.macs) if (m.allowed === true) delete denied[m.id];  // turned on at the Mac
      S.loaded = true, S.listErr = '', S.failures = 0;
      schedule(S.view === 'term' ? 50 : 300);
    } else {
      S.listErr = listError(r), S.loaded = true, S.failures++;
      const retry = json(r).retry_s;
      schedule(retry ? retry * 1000 : S.listErr === 'auth' ? 3000 : [2000, 5000, 10000, 30000][Math.min(S.failures - 1, 3)]);
    }
    // Drawn again only when something shown changed: a render rebuilds the screen (74 ms on the board), while the
    // terminal draws its own lines. On the terminal only the session's state, its request and the Mac's mode count.
    S.sig = sigOf();
    if (S.sig !== before) app.refresh();
  });
}

// The OS sends one request at a time, so a held poll is what a key waits behind. For 4 s after a key: a 0.15 s hold
// (one look of the server's); 0.5 s while you are busy; 2 s after 30 s without a touch. A new frame asks again at once
// (the server's hold, looking every 150 ms, paces it). A server that rounds the hold down (an older or self-hosted
// one) answers early: then the holds are whole seconds and the quick ask after a key waits 150 ms.
function pollScreen() {
  const s = cur();
  if (!s) return schedule(1000);
  polling = true;
  const now = Date.now();
  const fast = now < S.fastUntil;
  const wait = S.sheet ? 0 : fast ? (S.whole ? 0 : 0.15) : now - S.touchAt < 30000 ? (S.whole ? 1 : 0.5) : 2;
  call('GET', '/screen?mac=' + s.mac + '&s=' + encodeURIComponent(s.id) + '&since=' + S.scr.seq + '&wait=' + wait).then((r) => {
    polling = false;
    ST.polls++;
    if (S.view !== 'term' || !cur() || cur().id !== s.id) return schedule(50);
    const before = S.scr.err + S.scr.why + S.scr.starting;
    if (r.status === 200) {
      let info = null;
      try { info = term.apply('t', r.body); } catch (e) { info = null; }
      S.scr.err = '';
      if (info && info.starting) {
        S.scr.starting = true;
        if (before !== S.scr.err + S.scr.why + S.scr.starting) app.refresh();
        return schedule(800);
      }
      S.scr.starting = false;
      if (info) S.scr.seq = info.seq;
      // a 0.5 s hold that keeps coming back early with nothing new: this server rounds holds down (3 in a row)
      if (info && !info.lines && wait === 0.5) S.early = Date.now() - now < 300 ? (S.early || 0) + 1 : 0, S.whole = S.early >= 3;
      if (info && info.lines) {
        ST.frames++;
        if (info.at) ST.age.push(Date.now() - info.at);
        if (ST.keyAt) ST.effect.push(Date.now() - ST.keyAt), ST.keyAt = 0;
      }
      schedule(info && info.lines || wait ? 0 : fast ? 150 : 600);
    } else if (r.status === 409) {
      const d = json(r);
      S.scr.err = d.error || 'not_streaming', S.scr.why = d.why || '';
      schedule(5000);
    } else {
      S.listErr = r.status ? listError(r) : S.listErr;
      schedule(r.status ? 3000 : 2000);
    }
    if (before !== S.scr.err + S.scr.why + S.scr.starting) app.refresh();
  });
}

// What the device adds to the delay, in the trace every 15 s while a terminal is open (log()): frames drawn and their
// age (the Mac's "at" -> drawn), key round trips, first frame after a key, polls.
const ST = {frames: 0, polls: 0, age: [], keys: [], effect: [], keyAt: 0};
const avg = (a) => a.length ? Math.round(a.reduce((x, y) => x + y, 0) / a.length) : '-';
timer.every(15000, () => {
  if (S.view !== 'term' || !(ST.frames || ST.keys.length)) return;
  log('latency: ' + ST.frames + ' frames' + (ST.age.length ? ', age ' + avg(ST.age) + ' ms (max ' + Math.max(...ST.age) + ')' : '') +
    ', keys ' + ST.keys.length + ' rtt ' + avg(ST.keys) + ' ms, key to new lines ' + avg(ST.effect) + ' ms, ' + ST.polls + ' polls, font ' + S.font);
  ST.frames = ST.polls = 0, ST.age = [], ST.keys = [], ST.effect = [];
  return false;
});

// ---------------------------------------------------------------- sending (always from a tap)
function note(text, tone, ms) {
  if (app.kb > 0) return toast(text);  // the keyboard is up: no room for a line, a toast sits over the keys
  S.note = {text, tone: tone || 'ok', until: Date.now() + (ms || 4000)};
  timer.after((ms || 4000) + 50, () => { app.refresh(); return false; });
}

const SENT = () => ({done: t('Sent'), sent: t('Sent to the Mac'), confirm_mac: t('Waiting for you to confirm it on the Mac (60 s)')});
const NOT_ALLOWED = () => t('The Mac does not take commands from this device yet. On the Mac: EspOS Desktop > Settings > Devices, turn this device on.');
const REFUSED = () => ({
  desktop_offline: t('The Mac is offline. Nothing was sent.'), command_off: t('This is turned off in EspOS Desktop on the Mac.'),
  view_only: t('The Mac is in view-only mode. Nothing was sent.'), desktop_view_only: t('The Mac is in view-only mode. Nothing was sent.'),
  not_waiting: t('The session is no longer waiting.'), req_gone: t('That question was already answered or has expired.'),
  rate: t('Too many in a row. Wait a moment.'), sender_not_allowed: NOT_ALLOWED(),
});

// What the Mac lets this device do. 'denied': nothing is sent (the Mac's list of devices that may send commands says no:
// macs[].allowed false, or a "sender_not_allowed" seen here); 'view': the Mac's view-only mode; '' when it may send.
const denied = {};  // mac id -> true after a "sender_not_allowed"; ends when the server says allowed, or "Try again"
function lockOf(mac) {
  if (!mac) return '';
  if (mac.allowed === false || denied[mac.id]) return 'denied';
  return mac.mode === 'view' ? 'view' : '';
}
// Any answer that is not a success, in words: never silent, never a bare status. A "sender_not_allowed" locks the keys
// and Approve/Deny for that Mac (no retries), drops the keys still waiting, and the reason takes the prompt's place
// (the list: under the Mac's name), where it stays.
function notSent(r, d, mac) {
  const code = d.err || d.error || '';
  log('refused ' + (r.status || 0) + ' ' + (code || '-'));  // on the serial line too
  if (code === 'sender_not_allowed' && mac) {
    denied[mac.id] = true, KQ.q = [], S.sig = '';
    if (app.kb > 0) app.closeKeyboard();
    return;
  }
  note(!r.status ? t('No connection. Nothing was sent.') : REFUSED()[code] || (code || d.state ? t('Not sent: {why}', {why: code || d.state})
    : t('Not sent ({code}).', {code: r.status})), 'bad', 8000);
}

// kind: text | key | approve | deny. ack: the person already said yes to a risky one.
function send(kind, extra, ack, n) {
  const s = extra.session || cur();
  if (!s || S.sending) return;
  if (lockOf(macOf(s.mac)) === 'denied') return note(NOT_ALLOWED(), 'bad', 8000), app.refresh();
  const body = Object.assign({mac: s.mac, s: s.id, nonce: n || nonce(), kind}, extra.body);
  if (ack) body.ack_risky = true;
  S.sending = true;
  app.refresh();
  call('POST', '/send', body).then((r) => {
    S.sending = false;
    const d = json(r);
    if (r.status === 428) {  // the server's own check: ask once, then the same request with ack_risky
      S.sheet = {kind: 'risky', why: d.why || 'shell', what: extra.what, go: () => send(kind, extra, true)};
    } else if (r.status === 200 || r.status === 409) {
      const st = d.state || (r.status === 200 ? 'sent' : 'refused');
      if (st === 'refused' || st === 'failed') notSent(r, Object.assign({state: st}, d), macOf(s.mac));
      else {
        note(d.text || SENT()[st] || t('Sent'), st === 'confirm_mac' ? 'wait' : 'ok', st === 'confirm_mac' ? 60000 : 3000);
        if (extra.after) extra.after();
      }
      schedule(300);  // the session's state moves on: hear it soon
    } else if (!r.status && r.sent && !n) {
      send(kind, extra, ack, body.nonce);  // it may have arrived: the same nonce again is never typed twice
      return;
    } else if (r.status === 402) {
      note(t('This needs EspOS AI.'), 'bad', 6000);
    } else {
      notSent(r, d, macOf(s.mac));
    }
    app.refresh();
  });
}

function sendText(text) {
  text = String(text || '').trim();
  if (!text) return;
  S.touchAt = Date.now();
  const go = (ack) => send('text', {body: {text: text.slice(0, 2000)}, what: text, after: () => {
    S.text = '', S.clearField = true;
    S.fastUntil = Date.now() + 4000, ST.keyAt = Date.now();
  }}, ack);
  const why = riskyWhy(text);
  if (why) S.sheet = {kind: 'risky', why, what: text, go: () => go(true)};
  else go(false);
  app.refresh();
}

// Keys: a tap never waits for the one before. Taps made while a send is out go together as one kind "keys" (the
// Mac types them in order); a server that does not know "keys" yet gets them one by one. No redraw unless it failed.
// Esc and Ctrl+C stop Claude: they go only after a 1 s hold (holdDown / holdUp), with ack_risky.
const KQ = {q: [], busy: false, single: false};
function pressKey(k, ack) {
  const s = cur(), mac = s && macOf(s.mac);
  if (!s || !mac || !mac.online) return toast(t('Nothing can be sent while the Mac is offline'));
  const lock = lockOf(mac);
  if (lock === 'denied') return toast(NOT_ALLOWED());
  if (lock === 'view') return toast(t('View only: answer it in iTerm on the Mac.'));
  S.touchAt = Date.now();
  if (s.state === 'perm' && s.req) {  // Claude's own 1/2/3 dialog: the request's path, never "don't ask again"
    if (k === '1') return answer(s, true);
    if (k === '3') return answer(s, false);
    if (k === '2' || k === 'enter' || k === 'tab') return toast(t('Use Approve or Deny for this request'));
  }
  KQ.q.push({k, ack: !!ack});
  flushKeys();
}
function flushKeys(again) {
  const s = cur();
  if (KQ.busy || !KQ.q.length || !s) return;
  const m = macOf(s.mac), many = !KQ.single && !!(m && m.cmds && m.cmds['claude.keys']);  // cmds: {name: policy}
  const batch = again || KQ.q.splice(0, many ? 20 : 1);
  const keys = batch.map((x) => x.k);
  const body = batch.body || Object.assign({mac: s.mac, s: s.id, nonce: nonce()},
    keys.length === 1 ? {kind: 'key', key: keys[0]} : {kind: 'keys', keys});
  if (batch.some((x) => x.ack)) body.ack_risky = true;
  batch.body = body;
  KQ.busy = true;
  const t0 = Date.now();
  call('POST', '/send', body).then((r) => {
    KQ.busy = false;
    const d = json(r);
    if (keys.length > 1 && (r.status === 400 || r.status === 422 || d.err === 'command_off')) {  // an older server or Mac app
      KQ.single = true, KQ.q = batch.concat(KQ.q);
      return flushKeys();
    }
    if (!r.status && r.sent && !batch.retried) {  // it may have arrived: the same nonce is never typed twice
      batch.retried = true;
      return flushKeys(batch);
    }
    ST.keys.push(Date.now() - t0), ST.keyAt = Date.now();
    if (r.status === 409 && d.error === 'use_answer') KQ.q = [], toast(t('Use Approve or Deny for this request'));
    else if (r.status === 428) toast(t('Hold {key} for a second to stop Claude', {key: KEYS_LABEL[keys.find((k) => k === 'esc' || k === 'ctrl_c')] || 'Esc'}));
    else if (r.status === 200 || r.status === 409) {
      const st = d.state || (r.status === 200 ? 'sent' : 'refused');
      if (st === 'refused' || st === 'failed') KQ.q = [], notSent(r, Object.assign({state: st}, d), m), app.refresh();
    } else KQ.q = [], notSent(r, d, m), app.refresh();
    S.fastUntil = Date.now() + 4000;
    if (!polling && S.view === 'term') schedule(0);  // its effect, as soon as the Mac draws it
    flushKeys();
  });
}
let holdT = 0;
function holdDown(k) {
  S.touchAt = Date.now();
  if (holdT) timer.cancel(holdT);
  holdT = timer.after(1000, () => { holdT = 0; pressKey(k, true); toast(t('Sent {key}', {key: KEYS_LABEL[k]})); return false; });
}
function holdUp(k) {
  if (!holdT) return;
  timer.cancel(holdT), holdT = 0;
  toast(t('Hold {key} for a second to stop Claude', {key: KEYS_LABEL[k]}));
}

function answer(s, approve) {
  const req = s.req;
  if (!req) return;
  const go = (ack) => send(approve ? 'approve' : 'deny', {session: s, body: {req: req.id}, what: req.cmd || req.file || ''}, ack);
  const why = approve && (req.risky || riskyWhy(req.cmd)) ? (riskyWhy(req.cmd) || 'grant') : '';
  if (why) S.sheet = {kind: 'approve', why, req, go: () => go(true)};
  else go(false);
  app.refresh();
}

// ---------------------------------------------------------------- AI: suggestions, compose, dictation
function compose() {
  const s = cur();
  const rough = S.text.trim();
  if (!s || !rough) return note(t('Type or say a few words first.'), 'wait');
  S.sheet = {kind: 'compose', rough, text: '', state: 'loading'};
  app.refresh();
  call('POST', '/compose', {mac: s.mac, s: s.id, rough, seq: S.scr.seq}).then((r) => {
    if (!S.sheet || S.sheet.kind !== 'compose') return;
    if (r.status === 200) S.sheet.text = json(r).text || '', S.sheet.state = 'ok';
    else S.sheet.state = r.status === 402 ? 'locked' : 'failed';
    app.refresh();
  });
}

const canTalk = typeof voice === 'object';
function talkDown() {
  if (!canTalk || S.dict) return;
  S.dict = 'rec';
  app.refresh();
  voice.dictate({lang: app.lang, max_ms: 30000}).then((r) => {
    S.dict = '';
    const said = (r && r.text || '').trim();
    if (said) {
      S.text = (S.text ? S.text.replace(/\s+$/, '') + ' ' : '') + said, S.clearField = true;
      if (setting('voicesend', true) !== false) askVoiceSend(S.text, 0);
    } else note(t('Nothing was heard.'), 'wait');
    app.refresh();
  }, (e) => {
    S.dict = '';
    if (e && e.code !== 'cancelled') note(e && e.code === 'locked' ? t('This needs EspOS AI.') : t('Dictation did not work ({code}).', {code: e && e.code || '?'}), 'bad');
    app.refresh();
  });
}
// Say OK to send (desktop-protocol.md §11 voice confirm): the dictated text is read back on screen and the OS listens
// once for a yes or a no. Only "confirm" sends; "unclear" asks once more; anything else keeps the text in the field.
// A risky line is never sent by voice: it waits for its taps.
function askVoiceSend(text, tries) {
  if (!canTalk || riskyWhy(text)) return;
  S.sheet = {kind: 'voicesend', text};
  voice.dictate({lang: app.lang, max_ms: 5000, expect: 'yesno', about: text, until_quiet: true}).then((r) => {
    if (!S.sheet || S.sheet.kind !== 'voicesend') return;
    const yn = r && r.yesno;
    if (yn === 'unclear' && tries < 1) return askVoiceSend(text, tries + 1);
    S.sheet = null;
    if (yn === 'confirm') sendText(text);
    else note(yn === 'cancel' ? t('Not sent.') : t('Not sent. Tap Send when it is right.'), 'wait');
    app.refresh();
  }, () => {
    if (S.sheet && S.sheet.kind === 'voicesend') S.sheet = null;
    app.refresh();
  });
}

function talkUp() { if (canTalk && S.dict === 'rec') S.dict = 'wait', voice.stop(), app.refresh(); }

// ---------------------------------------------------------------- navigation
function openSession(s) {
  S.view = 'term', S.cur = {mac: s.mac, s: s.id};
  S.scr = {seq: 0, err: '', why: '', starting: false};
  S.text = '', S.sheet = null, S.sig = '', S.touchAt = Date.now();
  term.clear('t');
  schedule(50);
  app.refresh();
}
function back() {
  if (S.sheet) S.sheet = null;
  else if (S.view === 'term') { S.view = 'list', S.cur = null, S.sig = ''; term.clear('t'); schedule(50); }
  else return false;
  app.refresh();
  return true;
}
app.onBack = back;

function zoom(d) {
  const i = Math.max(0, Math.min(SIZES.length - 1, SIZES.indexOf(S.font) + d));
  S.font = SIZES[i < 0 ? 0 : i];
  store.set(SMALL ? 'size28' : 'size35', S.font);
  toast(t('Text size {px}', {px: S.font}));
}

// ---------------------------------------------------------------- screens: pieces
const small = (label, fn, o) => ui.button(label, fn, Object.assign({h: 30, bg: C.chip, color: C.chipFg}, o));
const chip = (label, fn, o) => ui.button(label, fn, Object.assign({h: 28, bg: C.chip, color: C.chipFg}, o));

function noteLine() {
  if (!S.note || Date.now() > S.note.until) return null;
  const col = {ok: C.ok, bad: C.bad, wait: C.wait}[S.note.tone] || C.dim;
  return ui.card([ui.text(S.note.text, {size: 13, color: col})], {bg: S.note.tone === 'wait' ? C.waitBg : C.card2, gap: 2});
}

// The bar over the terminal: back, the session, its state, text size. Nothing else: the screen is the terminal.
function topBar(s) {
  const kids = [small('', back, {icon: 'back', w: 32, h: 28}), ui.text(s ? s.name : '', {bold: true, size: 15})];
  if (s) kids.push(stateLine(s, true));
  kids.push(small('A−', () => zoom(-1), {h: 28, w: 36}), small('A+', () => zoom(1), {h: 28, w: 36}));
  return ui.row(kids, {gap: 6, pad: 2});
}

// Claude asks: the request in one strip over the prompt (Claude's own dialog stays readable in the terminal above).
function permStrip(s, mac) {
  const req = s.req || {};
  const what = req.cmd || req.file || req.desc || '';
  const title = ui.text((req.file && !req.cmd ? t('Claude wants to edit a file') : t('Claude wants to run a command')) + ': ' + what, {size: 13, bold: true});
  if (lockOf(mac)) return ui.column([title], {gap: 3, pad: 6});  // the prompt's line says why nothing can be answered
  const deny = ui.button(t('Deny'), () => answer(s, false), {bg: C.chip, color: C.bad, h: 34, fill: !LAND, w: LAND ? 88 : 0});
  const ok = ui.button(t('Approve'), () => answer(s, true), {bg: C.ok, h: 34, fill: !LAND, w: LAND ? 104 : 0});
  if (LAND) return ui.row([deny, ok, ui.column([title], {fill: true, gap: 0})], {gap: 8, pad: 5});  // fill goes last (it takes what is left)
  return ui.column([title, ui.row([deny, ok], {gap: 8})], {gap: 5, pad: 6});
}

// The terminal keys, on top of the OS keyboard while the prompt has it (ui.input "keys"). Arrows are the package's
// icons; shift+tab cycles Claude Code's mode, so it says so. Esc and Ctrl+C: hold 1 s.
const KEYS_LABEL = {esc: 'Esc', up: '', down: '', left: '', right: '', enter: 'Enter', tab: 'Tab', shift_tab: 'Mode', 1: '1', 2: '2', 3: '3', ctrl_c: 'Ctrl+C'};
function keyBtn(k, w) {
  const stop = k === 'esc' || k === 'ctrl_c';
  const o = {h: 34, w, bg: '#2b323d', color: stop ? '#ff8a80' : '#eef1f5', icon: KEYS_LABEL[k] ? '' : 'icons/' + k + '.a8'};
  const label = k === 'shift_tab' ? t('Mode') : KEYS_LABEL[k];
  if (stop) return ui.button(label, null, Object.assign(o, {onDown: () => holdDown(k), onUp: () => holdUp(k)}));
  return ui.button(label, () => pressKey(k), Object.assign(o, {quiet: true}));  // a key is not a redraw
}
function keysRow() {
  if (LAND) return ui.row([keyBtn('esc', 40), keyBtn('tab', 40), keyBtn('shift_tab', 50), keyBtn('left', 34), keyBtn('up', 34),
    keyBtn('down', 34), keyBtn('right', 34), keyBtn('enter', 50), keyBtn('ctrl_c', 56), keyBtn('1', 26), keyBtn('2', 26), keyBtn('3', 26)], {gap: 2});
  return ui.column([
    ui.row([keyBtn('esc', 44), keyBtn('tab', 44), keyBtn('shift_tab', 54), keyBtn('ctrl_c', 62), keyBtn('1', 30), keyBtn('2', 30), keyBtn('3', 30)], {gap: 3}),
    ui.row([keyBtn('left', 52), keyBtn('up', 52), keyBtn('down', 52), keyBtn('right', 52), keyBtn('enter', 94)], {gap: 3}),
  ], {gap: 4});
}

// The prompt, like Claude Code's own: type, Enter on the keyboard sends; the terminal keys sit on the keyboard.
function promptRow(s, mac) {
  const lock = lockOf(mac);
  if (lock === 'view') return ui.column([ui.text(t('View only. Turned on in EspOS Desktop on the Mac: you can watch, not type or approve.'), {size: 12, dim: true, center: true})], {pad: 6});
  if (lock === 'denied') {  // from the start when the server says so; after a refusal, "Try again" once it is turned on
    const why = ui.text(NOT_ALLOWED(), {size: 12, color: C.wait});
    if (mac.allowed === false) return ui.column([why], {pad: 6});
    return ui.row([ui.button(t('Try again'), () => { delete denied[mac.id]; S.sig = ''; app.refresh(); }, {h: 32, bg: C.chip, color: C.chipFg}),
      ui.column([why], {fill: true, gap: 0})], {gap: 8, pad: 5});  // fill goes last (it takes what is left)
  }
  const off = !mac || !mac.online;
  const clear = S.clearField;
  S.clearField = false;
  return ui.row([
    // Two lines when the app puts a long text in (dictation, AI): the field shows it whole.
    ui.input(S.text, {key: 'reply', hint: off ? t('Nothing can be sent while the Mac is offline') : t('Message Claude'),
      lines: S.text.length > (LAND ? 44 : 24) ? 2 : 1, max: 2000,
      mode: 'chat', ok: t('Send'), force: clear, quiet: true, bg: C.card, border: C.faint, keys: off ? null : keysRow(), keysH: LAND ? 38 : 76,
      onFocus: () => { S.touchAt = Date.now(); }, onChange: (v) => { S.text = v; S.touchAt = Date.now(); },
      onSubmit: (v) => { if (!off) sendText(v); }, fill: true}),
    canTalk ? ui.button('', null, {h: 36, w: 40, bg: S.dict === 'rec' ? C.bad : C.chip, color: S.dict === 'rec' ? '#ffffff' : C.chipFg, icon: 'icons/mic.a8', onDown: off ? null : talkDown, onUp: talkUp}) : null,
    ui.button('AI', () => { if (!off) compose(); }, {h: 36, w: 40, bg: C.ai}),
  ].filter(Boolean), {gap: 5, pad: 4});
}

// ---------------------------------------------------------------- sheets
function sheet() {
  const sh = S.sheet;
  if (!sh) return null;
  const close = () => { S.sheet = null; };
  if (sh.kind === 'risky' || sh.kind === 'approve') {
    const title = sh.stop ? t('Stop Claude?') : sh.kind === 'approve'
      ? (sh.why === 'delete' ? t('Approve deleting files?') : sh.why === 'grant' ? t('Approve this?') : t('Approve a risky command?'))
      : sh.why === 'shell' ? t('This goes straight to the shell on your Mac') : t('Send a risky line?');
    const what = sh.kind === 'approve' ? (sh.req.cmd || sh.req.file || '') : sh.what;
    return ui.sheet([
      ui.title(title, {size: 17, color: sh.stop ? C.fg : C.bad}),
      what ? ui.card([ui.text(what, {size: 13})], {bg: C.cmd}) : null,
      ui.text(WHY()[sh.why] || '', {size: 13}),
      ui.row([
        ui.button(t('Cancel'), close, {bg: C.chip, color: C.chipFg, fill: true}),
        ui.button(sh.stop ? t('Stop Claude') : sh.kind === 'approve' ? (sh.why === 'delete' ? t('Approve delete') : t('Approve')) : t('Send anyway'),
          () => { S.sheet = null; sh.go(); }, {bg: C.bad, fill: true}),
      ], {gap: 8}),
    ].filter(Boolean), {onClose: close});
  }
  if (sh.kind === 'compose') {
    const kids = [ui.text(t('Your words'), {size: 12, bold: true, dim: true}), ui.card([ui.text(sh.rough, {size: 13, dim: true})], {bg: C.card2})];
    if (sh.state === 'loading') kids.push(ui.text(t('AI is writing a clear reply'), {size: 13, color: C.ai}));
    else if (sh.state === 'locked') kids.push(ui.text(t('This needs EspOS AI.'), {size: 13, color: C.wait}));
    else if (sh.state === 'failed') kids.push(ui.text(t('AI could not write it now. Try again.'), {size: 13, color: C.bad}));
    else {
      kids.push(ui.text('AI · ' + t('Clear reply'), {size: 12, bold: true, color: C.ai}));
      kids.push(ui.input(sh.text, {key: 'composed', lines: 3, max: 2000, mode: 'chat', onChange: (v) => { sh.text = v; }}));
      kids.push(ui.text(t('Written by AI from your words and the end of this session. Check it before you send.'), {size: 12, dim: true}));
    }
    kids.push(ui.row([
      ui.button(t('Use mine'), () => { S.sheet = null; sendText(sh.rough); }, {bg: C.chip, color: C.chipFg, fill: true, h: 40}),
      ui.button(t('Try again'), () => { S.text = sh.rough; compose(); }, {bg: C.chip, color: C.chipFg, fill: true, h: 40}),
      sh.state === 'ok' ? ui.button(t('Send this'), () => { S.sheet = null; sendText(sh.text); }, {bg: C.run, fill: true, h: 40}) : null,
    ].filter(Boolean), {gap: 6}));
    return ui.sheet(kids, {onClose: close});
  }
  if (sh.kind === 'voicesend') {
    const stopListening = () => { try { voice.stop(true); } catch (e) {} };
    return ui.sheet([
      ui.title(t('Send this?'), {size: 17}),
      ui.card([ui.text(sh.text, {size: 14})], {bg: C.card2}),
      ui.text(t('Say OK to send it, or tap Send.'), {size: 13, dim: true}),
      ui.row([
        ui.button(t('Cancel'), () => { stopListening(); S.sheet = null; }, {bg: C.chip, color: C.chipFg, fill: true}),
        ui.button(t('Send'), () => { stopListening(); S.sheet = null; sendText(sh.text); }, {bg: C.run, fill: true}),
      ], {gap: 8}),
    ], {onClose: () => { stopListening(); S.sheet = null; }});
  }
  return null;
}

// ---------------------------------------------------------------- screens
function emptyState(title, body, button) {
  return ui.column([
    ui.spacer(LAND ? 4 : 40),
    ui.title(title, {center: true, size: 18}),
    ...[].concat(body).map((b) => ui.text(b, {center: true, size: 14, dim: true})),
    button || null,
  ].filter(Boolean), {gap: LAND ? 7 : 10, pad: 18, center: true, scroll: true, fill: true});
}

function listBody() {
  if (!S.loaded) return emptyState(t('Looking for your Mac'), t('One moment.'));
  const e = S.listErr;
  if (e === 'not_paired') return emptyState(t('Connect your Mac'), [
    t('1. Install EspOS Desktop from espos.dev/desktop.'), t('2. Open it: it shows a 6-digit code.'),
    t('3. On espos.dev, open Devices, tap Add and type the code.'), t('Your Claude Code sessions then show up here.')],
    ui.button(t('Check again'), () => schedule(50), {w: 200}));
  if (e === 'disabled') return emptyState(t('Sessions are paused'), t('EspOS has paused computer control for everyone for a while. Your Mac and Claude keep working as usual. Try again later.'));
  if (e === 'auth') return emptyState(t('This device is not linked'), t('Link it to your EspOS account on espos.dev, then open Sessions again.'));
  if (e === 'not_installed') return emptyState(t('Getting this app ready'), t('EspOS has not seen Sessions on this device yet. It takes up to a minute after installing.'));
  const parts = [];
  if (e && !S.macs.length) return emptyState({offline_s3: t('No internet connection.'), network: t('Cannot reach EspOS right now.'),
    rate: t('Too many requests. Trying again soon.'), server: t('EspOS had a problem. Trying again.')}[e] || e, t('One moment.'));
  if (e) parts.push(ui.card([ui.text({offline_s3: t('No internet connection.'), network: t('Cannot reach EspOS right now.'), rate: t('Too many requests. Trying again soon.'), server: t('EspOS had a problem. Trying again.')}[e] || e, {size: 13, color: C.wait})], {bg: C.waitBg}));
  if (!S.macs.length) return emptyState(t('Connect your Mac'), t('No computer is paired with this account yet. Install EspOS Desktop from espos.dev/desktop.'));
  for (const m of S.macs) {
    const mine = sorted().filter((x) => x.mac === m.id);
    const lock = lockOf(m);
    const status = m.mode === 'off' ? t('emergency stop') : !m.online ? t('offline since {t}', {t: ago(m.seen_ms)}) : lock ? t('view only') : t('online');
    parts.push(ui.text(m.name + ' · ' + status, {size: 12, bold: true, color: !m.online || m.mode === 'off' ? C.faint : lock ? C.wait : C.ok}));
    if (m.mode === 'off') {
      parts.push(ui.card([ui.text(t('Emergency stop on {mac}', {mac: m.name}), {bold: true, color: C.bad}),
        ui.text(t('EspOS Desktop disconnected itself. Nothing is shown or sent until someone turns it back on at the Mac.'), {size: 13})]));
      continue;
    }
    if (!m.online) parts.push(ui.text(t('Your Mac is offline. This is what it sent last; nothing can be sent until it is back.'), {size: 12, dim: true}));
    else if (lock === 'denied') parts.push(ui.text(NOT_ALLOWED(), {size: 12, color: C.wait}));
    if (!mine.length) {
      parts.push(ui.card([ui.text(t('No Claude sessions right now'), {bold: true}),
        ui.text(t('Start claude in iTerm2 on your Mac and it shows up here within seconds.'), {size: 13, dim: true})], {half: true}));
      continue;
    }
    const cards = mine.map((x) => {
      const kids = [ui.row([ui.text(x.name, {bold: true, size: 15}), stateLine(x)], {gap: 8, top: true}), x.cwd ? ui.text(x.cwd, {size: 11, dim: true}) : null];
      // Claude's own report on itself (claude.status, desktop-protocol.md §10) when there is one, else its last line
      if (x.report) kids.push(ui.text(x.report, {size: 13}));
      else if (x.last) kids.push(ui.text(x.last, {size: 13}));
      if (x.state === 'perm' && x.req && m.online) {
        kids.push(ui.card([ui.text(x.req.cmd || x.req.file || x.req.desc || '', {size: 13})], {bg: C.cmd, gap: 2}));
        if (!lock) kids.push(ui.row([
          ui.button(t('Deny'), () => answer(x, false), {bg: C.chip, color: C.bad, h: 32, fill: true}),
          ui.button(t('Approve'), () => answer(x, true), {bg: C.ok, h: 32, fill: true}),
        ], {gap: 8}));
      }
      return ui.card(kids.filter(Boolean), {half: true, gap: 4, onTap: () => openSession(x)});
    });
    parts.push(ui.row(cards, {gap: 7, top: true}));
  }
  return ui.column(parts, {gap: 7});
}

function listScreen() {
  return ui.column([
    ui.row([ui.title(t('Sessions'), {size: 22})], {pad: 8}),
    noteLine(),
    ui.column([listBody()], {scroll: true, fill: true, pad: 8, gap: 8}),
    sheet(),
  ].filter(Boolean), {fill: true, gap: 0});
}

function termScreen() {
  const s = cur();
  if (!s) {  // it ended, or the list has not come yet
    return ui.column([topBar(null), emptyState(S.loaded ? t('This session has ended') : t('Opening the session'), S.loaded ? t('It is no longer on the Mac.') : t('One moment.'))], {fill: true, gap: 0});
  }
  const mac = macOf(s.mac);
  // Keyboard up: the OS made the screen that much shorter (app.kb): the terminal, a request's strip and the prompt
  // stay; back (or a tap on the terminal) puts the keys away.
  const kbUp = app.kb > 0, tight = kbUp && LAND, perm = s.state === 'perm' && s.req;
  const kids = kbUp ? [] : [topBar(s), noteLine()];
  if (mac && !mac.online && !kbUp) kids.push(ui.text(t('Mac offline since {t}. This is the last screen it sent.', {t: ago(mac.seen_ms)}), {size: 12, dim: true}));
  if (mac && mac.stream === false || S.scr.err) {
    const why = (mac && mac.stream_why) || S.scr.why;
    kids.push(ui.column([
      ui.card([ui.text(why === 'view' ? t('The Mac is in view-only mode') : why === 'not_iterm' ? t('This session is not in iTerm2') : why === 'ended' ? t('This session has ended') : t('Terminal sharing is off on this Mac'), {bold: true}),
        ui.text(t('You still see the state, the last line and permission requests. To see the terminal, turn on "Share terminal text" in EspOS Desktop, under Data sent.'), {size: 13, dim: true})]),
      s.last ? ui.card([ui.text(t('Last line'), {size: 12, bold: true, dim: true}), ui.text(s.last, {size: 13})]) : null,
    ].filter(Boolean), {scroll: true, fill: true, pad: 8, gap: 8}));
  } else if (!(tight && perm)) {  // sideways with the keyboard and a request: the strip and the prompt fill the room
    kids.push(ui.term({key: 't', font: S.font, wrap: 'fit', fill: true, pill: t('↓ {n} new lines'),
      onTap: () => { S.touchAt = Date.now(); if (kbUp) app.closeKeyboard(); }}));  // a tap on the terminal puts the keys away
  }
  if (perm) kids.push(permStrip(s, mac));
  kids.push(promptRow(s, mac), kbUp ? null : ui.spacer(6), sheet());  // the spacer: room for the OS home bar
  return ui.column(kids.filter(Boolean), {fill: true, gap: 0});
}

app.full = true;
app.selfDraw = true;  // answers do not redraw: pollList / pollScreen / send call app.refresh() when the screen changed
app.title = t('Sessions');
app.render = () => (S.view === 'term' ? termScreen() : listScreen());

// Opened from a notification: "<mac>:<session>[:<req>]" opens that session.
if (app.arg) {
  const [m, s] = String(app.arg).split(':');
  if (m && s) S.view = 'term', S.cur = {mac: +m, s}, S.touchAt = Date.now();
}
timer.every(30000, () => app.refresh());  // "2 min" and the countdowns move on
poll();
