// {{name}}: a counter that keeps its number between visits.
// EspOS JavaScript API level 1: ui, app, timer, sound, notify, toast, store
// (types and docs: @espos/sdk espos.d.ts; s3/docs/apps.md).

let count = store.get('count', 0);

function change(by) {
  count = Math.max(0, count + by);
  store.set('count', count);
  if (count > 0 && count % 10 === 0) {
    sound.chime(); // needs "sound" in manifest.json "perms"
    toast('Reached ' + count);
  }
}

app.title = '{{name}}';
app.render = () => ui.column([
  ui.card([
    ui.text('Counted', {dim: true, center: true}),
    ui.big(String(count), {color: '#2563eb'}),
  ], {center: true}),
  ui.row([
    ui.button('Minus 1', () => change(-1), {bg: '#6b7280'}),
    ui.button('Plus 1', () => change(1)),
  ], {center: true}),
  ui.button('Reset', () => { count = 0; store.set('count', 0); }, {wide: true, bg: '#ef4444'}),
], {gap: 12});
