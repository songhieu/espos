// The device's credential goes only to the calling app's own path (src/js/jsnet.cpp ownPath/deviceAuth). Each case is
// one request; the results go to the screen and to the mock (its own path), where apps/sessions/test/run.sh checks them.
const E = '{espos}/v1/device/app/';
const CASES = [
  ['own', E + 'dev.espos.authtest/echo?a=1', 'espos', 'ok'],
  ['other_app', E + 'dev.espos.sessions/echo', 'espos', 'denied'],
  ['dotdot', E + 'dev.espos.authtest/../sessions/echo', 'espos', 'denied'],
  ['encoded', E + 'dev.espos.authtest/%2e%2e/sessions/echo', 'espos', 'denied'],
  ['double_slash', E + 'dev.espos.authtest//echo', 'espos', 'denied'],
  ['lookalike_id', E + 'dev.espos.authtestx/echo', 'espos', 'denied'],
  ['other_path', '{espos}/v1/device/settings', 'espos', 'denied'],
  ['outside_host', 'https://example.com/v1/device/app/dev.espos.authtest/echo', 'espos', 'denied'],
  ['other_app_no_auth', E + 'dev.espos.sessions/echo/plain', '', 'ok'],
  ['own_no_auth', E + 'dev.espos.authtest/echo/plain', '', 'ok'],
];
const got = [];
function run(i) {
  if (i >= CASES.length) {
    app.refresh();
    net.request({method: 'POST', url: E + 'dev.espos.authtest/echo/report', body: {results: got}, auth: 'espos'});
    return;
  }
  const [name, url, auth, want] = CASES[i];
  let r;
  try {
    r = net.request({url, auth: auth || undefined, timeout: 5000});
  } catch (e) {  // refused at once: thrown by the call itself
    got.push({name, want, got: e.code || 'error'});
    return run(i + 1);
  }
  r.then((a) => { got.push({name, want, got: a.status === 200 ? 'ok' : 'status ' + a.status}); run(i + 1); },
         (e) => { got.push({name, want, got: e.code || 'error'}); run(i + 1); });
}
app.title = 'Auth test';
app.render = () => ui.column(got.map((g) => ui.text(g.name + ': ' + g.got + (g.got === g.want ? '' : '  (want ' + g.want + ')'),
  {size: 13, color: g.got === g.want ? '#1e9e52' : '#e0383b'})), {pad: 8, gap: 4});
run(0);
