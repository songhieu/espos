// Hello settings: the simplest app with settings (docs: guides/settings). The form is not code: the manifest's
// "settings" list draws it on the device (Settings > Apps > Hello settings > Settings) and on the web.
// settings.get(key) reads a value (or its default); a secret reads as {set: true|false}, never its value.
// app.onSettings(changed) runs when values change here or on the web; settings.open() opens the form.
let changed = [];

app.title = 'Hello settings';
app.onSettings = (keys) => {
  changed = keys;
};

const show = (v) => (v === null ? '-' : Array.isArray(v) ? (v.length ? v.join(', ') : '-') : String(v));

app.render = () => {
  const units = settings.get('units') === 'f' ? 'Fahrenheit' : 'Celsius';
  const key = settings.get('api_key');
  return ui.column([
    ui.card([
      ui.title('Hello, ' + settings.get('name') + '!', {color: settings.get('color')}),
      ui.text('Speed ' + settings.get('speed') + ', units ' + units + ', seconds ' + (settings.get('seconds') ? 'on' : 'off')),
      ui.text('Wake up at ' + settings.get('wake')),
      ui.text('Tags: ' + show(settings.get('tags'))),
    ]),
    ui.card([
      ui.text('Shop: ' + show(settings.get('shop_url'))),
      ui.text('API key: ' + (key.set ? 'set' : 'not set'), {dim: true}),
    ]),
    changed.length ? ui.text('Changed: ' + changed.join(', '), {dim: true}) : ui.spacer(4),
    ui.button('Edit settings', () => settings.open(), {wide: true}),
  ]);
};
