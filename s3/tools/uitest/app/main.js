// Smoke test of the level-2 UI nodes: ui.nav, ui.split, ui.grid (pictures, badges, tags, fallbacks), ui.input (keyed),
// ui.line, ui.qr, ui.sheet, app.full, app.onBack, button w/h. app.arg picks a state: "sheet", "orders".
app.full = true;
let tab = app.arg === 'orders' ? 1 : 0, q = '', sheet = app.arg === 'sheet', taps = 0;
const P = [['Latte', '$4.50', 'latte', '2'], ['Flat white', '$4.40', 'flatwhite'], ['Espresso', '$3.00', 'espresso'],
  ['Cappuccino', '$4.20', 'cappuccino'], ['Cold brew', '$4.80', 'coldbrew', '1', '3 left'], ['Matcha latte', '$5.20', 'matcha'],
  ['Croissant', '$3.50', 'croissant', '1'], ['Blueberry loaf', '$3.80', 'loaf'], ['Earl Grey', '$3.20', '', '', '', 'EG'],
  ['Progressive photo', '$2.00', 'prog'], ['Banana bread', 'Sold out', '', '', '', 'BB'], ['Gift card', '$25.00', '', '', '', 'GC']];
const items = () => P.filter((p) => !q || p[0].toLowerCase().includes(q.toLowerCase())).map(([title, sub, img, badge, tag, ini]) =>
  ({title, sub, image: img ? 'img/' + img + '.jpg' : '', badge: badge || '', tag: tag || '', initials: ini || '',
    bg: img ? '' : '#f3e5d6', bar: img ? '' : '#a0622d', dim: sub === 'Sold out'}));
app.onBack = () => { if (sheet) { sheet = false; return true; } if (tab) { tab = 0; return true; } return false; };
app.render = () => {
  const land = app.screen.landscape;
  const nav = ui.nav([{label: 'Sell', icon: 'icons/bag.a8'}, {label: 'Orders', icon: 'icons/receipt.a8'},
    {label: 'Customers', icon: 'icons/users.a8'}, {label: 'Shift', icon: 'icons/drawer.a8'}, {label: 'More', icon: 'icons/more.a8'}],
    tab, (i) => { tab = i; }, {badges: ['', '4', '', '', '']});
  const left = ui.column([
    ui.input(q, {key: 'q', hint: 'Search name or SKU', mode: 'search', onChange: (t) => { q = t; }}),
    ui.grid(items(), (i) => { taps++; }, {cols: 3, h: land ? 79 : 92}),
  ], {gap: 5});
  const right = ui.column([
    ui.text('Sale · ' + taps + ' taps', {bold: true}),
    ui.line('Latte', '$11.60', {bold: true}), ui.line('Large · Oat', '', {dim: true}),
    ui.line('Croissant', '$3.50', {bold: true}), ui.line('Tax incl.', '$1.79', {dim: true}),
  ], {gap: 4});
  const charge = ui.button('Charge $23.32', () => { sheet = true; }, {wide: true, bg: '#1e9e52', h: 46});
  const main = tab === 1 ? ui.column([ui.title('Orders'), ui.qr('https://example.com/checkout/order-received/1043/?key=wc_order_abc', {size: 140})], {fill: true})
    : ui.split(left, right, {w: land ? 252 : 200, bgR: '#ffffff', footR: charge, fill: true});
  const page = land ? ui.row([nav, main], {gap: 0, fill: true}) : ui.column([main, nav], {gap: 0, fill: true});
  if (!sheet) return page;
  return ui.column([page, ui.sheet([ui.title('Latte'), ui.text('Options sheet'),
    ui.row([ui.button('Small', () => {}, {bg: '#e4e6ec', color: '#16181d', h: 32}), ui.button('Large', () => {}, {h: 32})]),
    ui.button('Add · $5.80', () => { sheet = false; }, {wide: true, bg: '#1e9e52'})], {onClose: () => { sheet = false; }, w: 360})], {gap: 0, fill: true});
};
