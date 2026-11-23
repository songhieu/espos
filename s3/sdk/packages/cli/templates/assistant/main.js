// {{name}}: an assistant app. Its brain is not here: it runs at your provider (provider/server.mjs is a small
// working one), and the EspOS server sends it each turn as a signed EspOS Assistant Protocol v1 request.
// This screen only says who answers; the person picks the app in Settings › Default apps › Assistant.
// (types and docs: @espos/sdk espos.d.ts; espos.dev/docs/guides/assistant)

app.title = '{{name}}';
app.render = () => ui.column([
  ui.card([
    ui.text('{{name}}', {center: true}),
    ui.text('Your assistant', {dim: true, center: true}),
  ], {center: true}),
  ui.text('Say the wake word, then ask. Pick it in Settings, Default apps, Assistant.', {wrap: true}),
], {gap: 12});
