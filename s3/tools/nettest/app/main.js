// net.request test app (tools/nettest/run.sh): runs every case against tools/nettest/server.mjs and posts the results
// to its /report. Not for the store.
const B = 'http://127.0.0.1:' + (app.arg || '8788');
const results = [];
let done = false;

const json = (r) => { try { return JSON.parse(r.body); } catch (e) { return {}; } };
const cases = [
  ['get', {url: B + '/ok'}, (r) => r.status === 200 && r.ok && json(r).ok === true],
  ['post json', {method: 'POST', url: B + '/echo', body: {a: 1}, headers: {'X-Custom': 'hi'}},
    (r) => { const j = json(r); return j.body === '{"a":1}' && j.type === 'application/json' && j.custom === 'hi' && j.ua.startsWith('EspOS/'); }],
  ['status 500 resolves', {url: B + '/status/500'}, (r) => r.status === 500 && !r.ok],
  ['keep header', {url: B + '/ok', keep: ['X-Test']}, (r) => r.headers['x-test'] === 'yes' && !('x-hidden' in r.headers)],
  ['redirect same origin', {url: B + '/redirect-same'}, (r) => r.status === 200 && json(r).ok === true],
  ['redirect elsewhere not followed', {url: B + '/redirect-other'}, (r) => r.status === 302],
  ['chunked body', {url: B + '/chunked'}, (r) => r.body.length === 20000],
  ['too big', {url: B + '/big?kb=300'}, null, (e) => e.code === 'too_big'],
  ['timeout after sending', {method: 'POST', url: B + '/slow?ms=4000', body: 'x', timeout: 1500}, null,
    (e) => e.code === 'timeout' && e.sent === true],
  ['dropped after sending', {method: 'POST', url: B + '/drop', body: 'x'}, null, (e) => e.sent === true],
  ['refused (not sent)', {url: 'http://127.0.0.1:9/x'}, null, (e) => e.code === 'offline' && e.sent === false],
  ['dns', {url: 'https://no-such-host.invalid/'}, null, (e) => e.code === 'dns' && e.sent === false],
  ['host not declared', {url: 'https://example.org/'}, null, (e) => e.code === 'denied' && e.sent === false],
  ['http to a public host', {url: 'http://example.com/'}, null, (e) => e.code === 'denied'],
  ['Authorization refused', {url: B + '/ok', headers: {Authorization: 'x'}}, null, (e) => e.code === 'denied'],
  ['placeholder url', {url: '{base}/ok'}, (r) => r.status === 200],
  ['secret: basic', {url: B + '/auth', auth: 'server'}, (r) => json(r).auth === 'Basic Y2tfdGVzdDpjc190ZXN0'],  // ck_test:cs_test
  ['secret: none without auth', {url: B + '/auth'}, (r) => json(r).auth === '' && json(r).key === ''],
  ['secret: not for other paths', {url: B + '/echo', auth: 'server'}, null, (e) => e.code === 'denied'],
  ['secret: unknown binding', {url: B + '/auth', auth: 'nope'}, null, (e) => e.code === 'denied'],
  ['secret: query after a 401', {url: B + '/auth401', auth: 'server'}, (r) => r.status === 200 && json(r).key === 'ck_test' && !json(r).auth],
  ['secret: query form kept', {url: B + '/auth', auth: 'server'}, (r) => json(r).key === 'ck_test' && json(r).secret === 'set' && !json(r).auth],
  ['save to cache', {url: B + '/big?kb=10', save: 'cache/t.bin'}, (r) => r.status === 200 && r.size === 10240 && r.body === undefined &&
    fs.list('cache').some((f) => f.name === 'cache/t.bin' && f.size === 10240)],
  ['save outside cache', {url: B + '/ok', save: 'data/x'}, null, (e) => e.code === 'denied'],
];

async function runAll() {
  for (const [name, opts, ok, bad] of cases) {
    try {
      const r = await net.request(opts);
      results.push({name, pass: !!ok && ok(r), got: r.status});
    } catch (e) {
      results.push({name, pass: !!bad && bad(e), got: e.code + (e.sent ? ' sent' : ''), msg: String(e.message)});
    }
    app.refresh();
  }
  // The queue: 1 on the network + 4 waiting; the 6th is refused at once.
  const ps = [];
  for (let i = 0; i < 6; i++) ps.push(net.request({url: B + '/slow?ms=200'}).then(() => 'ok', (e) => e.code));
  const codes = await Promise.all(ps);
  results.push({name: 'queue full = busy', pass: codes.filter((c) => c === 'busy').length === 1 && codes.filter((c) => c === 'ok').length === 5, got: codes.join(',')});
  results.push({name: 'online', pass: net.online === true, got: String(net.online)});
  // fs and crypto
  const t = (name, f) => { try { const v = f(); results.push({name, pass: v === true, got: String(v)}); } catch (e) { results.push({name, pass: false, got: String(e)}); } };
  t('fs write/read', () => fs.write('data/q/a.json', '{"x":1}') && fs.read('data/q/a.json') === '{"x":1}');
  t('fs bare name is data/', () => fs.write('n.txt', 'hi') && fs.read('data/n.txt') === 'hi');
  t('fs list', () => fs.list('data/q').length === 1 && fs.list('data/q')[0].name === 'data/q/a.json');
  t('fs remove', () => fs.remove('data/q/a.json') && fs.read('data/q/a.json') === undefined);
  t('fs bad name', () => { try { fs.write('../x', 'y'); return false; } catch (e) { return e instanceof RangeError; } });
  t('fs quota', () => { try { fs.write('data/big', 'z'.repeat(70 * 1024)); return false; } catch (e) { return /quota/.test(String(e)); } });
  t('fs free', () => fs.free() > 60000 && fs.free('cache') > 0);
  t('sha256', () => crypto.sha256('abc') === 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
  t('random', () => /^[0-9a-f]{16}$/.test(crypto.random(8)) && crypto.random(8) !== crypto.random(8));
  done = true;
  app.refresh();
  await net.request({method: 'POST', url: B + '/report', body: {results}});
}

app.title = 'Net test';
app.render = () => ui.column([
  ui.text(done ? 'Done: ' + results.filter((r) => r.pass).length + ' of ' + results.length : 'Running ' + results.length),
  ui.list(results.map((r) => ({title: r.name, right: r.pass ? 'pass' : 'FAIL', sub: r.got + (r.msg ? ' ' + r.msg : '')}))),
]);
runAll();
