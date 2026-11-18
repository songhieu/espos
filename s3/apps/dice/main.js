// Dice: roll 1 to 3 dice, or draw lots (who does the dishes, what's for lunch).
// Texts are English; TEXT has them in other languages (app.lang is the device's language: "vi", "de"...).
const TEXT = {
  vi: {
    'Dice': 'Xúc xắc', 'Draw lots': 'Bốc thăm', 'Total {0}': 'Tổng {0}', '1 die': '1 viên', '2 dice': '2 viên',
    '3 dice': '3 viên', 'Roll': 'Tung', 'Draw': 'Bốc', 'Tap Draw to pick one': 'Chạm Bốc để chọn',
  },
  'zh-Hans': {
    'Dice': '色子', 'Draw lots': '抽签', 'Total {0}': '共 {0} 点', '1 die': '1 颗', '2 dice': '2 颗', '3 dice': '3 颗',
    'Roll': '掷色子', 'Draw': '抽签', 'Tap Draw to pick one': '点按“抽签”选一个',
  },
  es: {
    'Dice': 'Dados', 'Draw lots': 'Sorteo', 'Total {0}': 'Total {0}', '1 die': '1 dado', '2 dice': '2 dados',
    '3 dice': '3 dados', 'Roll': 'Tirar', 'Draw': 'Sortear', 'Tap Draw to pick one': 'Toca Sortear para elegir',
  },
  fr: {
    'Dice': 'Dés', 'Draw lots': 'Tirage au sort', 'Total {0}': 'Total {0}', '1 die': '1 dé', '2 dice': '2 dés',
    '3 dice': '3 dés', 'Roll': 'Lancer', 'Draw': 'Tirer', 'Tap Draw to pick one': 'Touchez Tirer pour choisir',
  },
  de: {
    'Dice': 'Würfel', 'Draw lots': 'Losen', 'Total {0}': 'Summe {0}', '1 die': '1 Würfel', '2 dice': '2 Würfel',
    '3 dice': '3 Würfel', 'Roll': 'Würfeln', 'Draw': 'Ziehen', 'Tap Draw to pick one': 'Tippe auf Ziehen',
  },
  ja: {
    'Dice': 'サイコロ', 'Draw lots': 'くじ引き', 'Total {0}': '合計 {0}', '1 die': '1個', '2 dice': '2個', '3 dice': '3個',
    'Roll': '振る', 'Draw': '引く', 'Tap Draw to pick one': '「引く」をタップ',
  },
  ko: {
    'Dice': '주사위', 'Draw lots': '제비뽑기', 'Total {0}': '합계 {0}', '1 die': '1개', '2 dice': '2개', '3 dice': '3개',
    'Roll': '굴리기', 'Draw': '뽑기', 'Tap Draw to pick one': '뽑기를 눌러 고르세요',
  },
  'pt-BR': {
    'Dice': 'Dados', 'Draw lots': 'Sorteio', 'Total {0}': 'Total {0}', '1 die': '1 dado', '2 dice': '2 dados',
    '3 dice': '3 dados', 'Roll': 'Jogar', 'Draw': 'Sortear', 'Tap Draw to pick one': 'Toque em Sortear para escolher',
  },
  ru: {
    'Dice': 'Кубики', 'Draw lots': 'Жребий', 'Total {0}': 'Сумма {0}', '1 die': '1 кубик', '2 dice': '2 кубика',
    '3 dice': '3 кубика', 'Roll': 'Бросить', 'Draw': 'Тянуть', 'Tap Draw to pick one': 'Нажмите «Тянуть»',
  },
  id: {
    'Dice': 'Dadu', 'Draw lots': 'Undian', 'Total {0}': 'Total {0}', '1 die': '1 dadu', '2 dice': '2 dadu',
    '3 dice': '3 dadu', 'Roll': 'Lempar', 'Draw': 'Undi', 'Tap Draw to pick one': 'Ketuk Undi untuk memilih',
  },
};
const t = (s, ...a) => ((TEXT[app.lang] || {})[s] || s).replace(/\{(\d)\}/g, (_, i) => a[i]);
// What to draw from, in the device's language (English when it has none).
const SETS_BY_LANG = {
  en: [
    {name: 'Dishes', items: ['Dad', 'Mom', 'Brother', 'Sister']},
    {name: 'Lunch', items: ['Noodles', 'Rice bowl', 'Sandwich', 'Salad', 'Pizza', 'Soup']},
    {name: 'Yes or no', items: ['Yes', 'No', 'Ask again tomorrow']},
  ],
  vi: [
    {name: 'Ai rửa bát', items: ['Bố', 'Mẹ', 'Anh', 'Em']},
    {name: 'Trưa ăn gì', items: ['Phở', 'Cơm tấm', 'Bún bò', 'Bánh mì', 'Hủ tiếu', 'Mì Quảng']},
    {name: 'Có hay không', items: ['Có', 'Không', 'Để mai tính']},
  ],
  'zh-Hans': [
    {name: '谁洗碗', items: ['爸爸', '妈妈', '哥哥', '妹妹']},
    {name: '午饭吃啥', items: ['牛肉面', '饺子', '盖浇饭', '麻辣烫', '炒饭', '包子']},
    {name: '行不行', items: ['行', '不行', '明天再说']},
  ],
  es: [
    {name: 'Platos', items: ['Papá', 'Mamá', 'Hermano', 'Hermana']},
    {name: '¿Qué comemos?', items: ['Paella', 'Tortilla', 'Lentejas', 'Cocido', 'Gazpacho', 'Croquetas']},
    {name: 'Sí o no', items: ['Sí', 'No', 'Ya veremos']},
  ],
  fr: [
    {name: 'Vaisselle', items: ['Papa', 'Maman', 'Papi', 'Mamie']},
    {name: 'Déjeuner', items: ['Quiche', 'Steak-frites', 'Croque-monsieur', 'Ratatouille', 'Crêpes', 'Blanquette']},
    {name: 'Oui ou non', items: ['Oui', 'Non', 'On verra demain']},
  ],
  de: [
    {name: 'Abwasch', items: ['Papa', 'Mama', 'Bruder', 'Schwester']},
    {name: 'Mittagessen', items: ['Schnitzel', 'Currywurst', 'Döner', 'Spätzle', 'Bratwurst', 'Linsensuppe']},
    {name: 'Ja oder nein', items: ['Ja', 'Nein', 'Frag morgen nochmal']},
  ],
  ja: [
    {name: '皿洗い', items: ['お父さん', 'お母さん', 'お兄ちゃん', 'お姉ちゃん']},
    {name: 'お昼ごはん', items: ['ラーメン', 'カレー', 'うどん', '定食', 'そば', 'おにぎり']},
    {name: 'はい/いいえ', items: ['はい', 'いいえ', '明日また聞いて']},
  ],
  ko: [
    {name: '설거지', items: ['아빠', '엄마', '형', '누나']},
    {name: '점심 메뉴', items: ['김치찌개', '비빔밥', '짜장면', '된장찌개', '김밥', '라면']},
    {name: '할까 말까', items: ['해', '하지 마', '내일 다시 물어봐']},
  ],
  'pt-BR': [
    {name: 'Louça', items: ['Pai', 'Mãe', 'Irmão', 'Irmã']},
    {name: 'Almoço', items: ['Feijoada', 'Prato feito', 'Strogonoff', 'Parmegiana', 'Moqueca', 'Pastel']},
    {name: 'Sim ou não', items: ['Sim', 'Não', 'Pergunta amanhã']},
  ],
  ru: [
    {name: 'Посуда', items: ['Папа', 'Мама', 'Брат', 'Сестра']},
    {name: 'Обед', items: ['Борщ', 'Пельмени', 'Плов', 'Щи', 'Котлеты', 'Гречка']},
    {name: 'Да или нет', items: ['Да', 'Нет', 'Спроси завтра']},
  ],
  id: [
    {name: 'Cuci piring', items: ['Ayah', 'Ibu', 'Kakak', 'Adik']},
    {name: 'Makan apa?', items: ['Nasi goreng', 'Bakso', 'Soto', 'Gado-gado', 'Mi ayam', 'Nasi Padang']},
    {name: 'Ya/Tidak', items: ['Ya', 'Tidak', 'Tanya besok lagi']},
  ],
};
const SETS = SETS_BY_LANG[app.lang] || SETS_BY_LANG.en;

let n = 2, dice = [3, 5], tab = 0, pick = '', set = 0;
const roll = () => { dice = Array.from({length: n}, () => 1 + Math.floor(Math.random() * 6)); sound.click(); };
const draw = () => { const it = SETS[set].items; pick = it[Math.floor(Math.random() * it.length)]; sound.chime(); };

app.title = t('Dice');
app.render = () => ui.column([
  ui.chips([t('Dice'), t('Draw lots')], tab, (i) => { tab = i; }),
  tab === 0 ? ui.column([
    ui.card([
      ui.big(dice.join('  '), {color: '#6d28d9'}),
      ui.text(t('Total {0}', dice.reduce((a, b) => a + b, 0)), {center: true, bold: true, size: 18}),
    ], {center: true}),
    ui.chips([t('1 die'), t('2 dice'), t('3 dice')], n - 1, (i) => { n = i + 1; roll(); }),
    ui.button(t('Roll'), roll, {bg: '#6d28d9', wide: true}),
  ]) : ui.column([
    ui.chips(SETS.map(s => s.name), set, (i) => { set = i; pick = ''; }),
    ui.card([
      ui.text(pick ? SETS[set].name : t('Tap Draw to pick one'), {center: true, dim: true}),
      ui.title(pick || '?', {center: true, size: 24}),
    ], {center: true}),
    ui.button(t('Draw'), draw, {bg: '#6d28d9', wide: true}),
    ui.text(SETS[set].items.join(' · '), {size: 13, dim: true}),
  ]),
]);
