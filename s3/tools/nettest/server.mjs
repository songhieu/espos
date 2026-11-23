#!/usr/bin/env node
// The test server for net.request (src/js/jsnet.cpp): every case the API must handle, plus /report where the test app
// posts its results. `node tools/nettest/server.mjs [port]` (default 8788). Used by tools/nettest/run.sh.
import http from 'node:http';

const PORT = +(process.argv[2] || 8788);
let report = null;
const seen = [];

const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  let body = '';
  req.on('data', (c) => (body += c));
  req.on('end', () => {
    seen.push({method: req.method, path: u.pathname, auth: req.headers.authorization || '', query: u.search});
    const json = (st, o, h = {}) => {
      res.writeHead(st, Object.assign({'Content-Type': 'application/json'}, h));
      res.end(JSON.stringify(o));
    };
    const p = u.pathname;
    if (p === '/ok') return json(200, {ok: true}, {'X-Test': 'yes', 'X-Hidden': 'no'});
    if (p === '/echo') return json(200, {method: req.method, body, type: req.headers['content-type'] || '',
                                         ua: req.headers['user-agent'] || '', custom: req.headers['x-custom'] || ''});
    if (p.startsWith('/status/')) return json(+p.slice(8), {status: +p.slice(8)});
    if (p === '/redirect-same') { res.writeHead(302, {Location: '/ok'}); return res.end(); }
    if (p === '/redirect-other') { res.writeHead(302, {Location: 'https://example.org/'}); return res.end(); }
    if (p === '/big') { res.writeHead(200, {'Content-Type': 'text/plain'}); return res.end('x'.repeat(+(u.searchParams.get('kb') || 300) * 1024)); }
    if (p === '/chunked') {
      res.writeHead(200, {'Content-Type': 'text/plain'});  // no Content-Length: Node sends it chunked
      let n = 0;
      const t = setInterval(() => { res.write('y'.repeat(2000)); if (++n === 10) { clearInterval(t); res.end(); } }, 5);
      return;
    }
    if (p === '/slow') return setTimeout(() => json(200, {slow: true}), +(u.searchParams.get('ms') || 3000));
    if (p === '/drop') return req.socket.destroy();  // the request arrived; the answer never comes
    if (p === '/auth') return json(200, {auth: req.headers.authorization || '', key: u.searchParams.get('consumer_key') || '',
                                         secret: u.searchParams.get('consumer_secret') ? 'set' : ''});
    if (p === '/auth401') {  // a host that drops Authorization: only the query form gets through
      if (!u.searchParams.get('consumer_key')) return json(401, {code: 'woocommerce_rest_authentication_error', message: 'Consumer key is missing.'});
      return json(200, {key: u.searchParams.get('consumer_key'), auth: req.headers.authorization || ''});
    }
    if (p === '/report') { report = JSON.parse(body || 'null'); return json(200, {ok: true}); }
    if (p === '/results') return json(200, {report, seen});
    json(404, {error: 'no such case'});
  });
});
server.listen(PORT, '0.0.0.0', () => console.log(`nettest server on http://127.0.0.1:${PORT}`));
