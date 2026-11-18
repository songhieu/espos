#!/usr/bin/env node
// Checks every internal link and anchor in the built site (dist/), with no network access.
//   npm run build && npm run check:links
// For each HTML page: every href/src that points inside the site must resolve to a file in dist/, and every
// #fragment must match an id on the target page. External links are listed, not fetched.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const site = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const dist = path.join(site, 'dist');
if (!fs.existsSync(dist)) {
  console.error('check-links: no dist/ folder; run npm run build first');
  process.exit(2);
}

const pages = [];
(function walk(dir) {
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) walk(p);
    else if (e.name.endsWith('.html')) pages.push(p);
  }
})(dist);

const idsCache = new Map();
function idsOf(file) {
  if (!idsCache.has(file)) {
    const html = fs.readFileSync(file, 'utf8');
    const ids = new Set();
    for (const m of html.matchAll(/\sid=("([^"]*)"|'([^']*)')/g)) ids.add(m[2] ?? m[3]);
    idsCache.set(file, ids);
  }
  return idsCache.get(file);
}

function resolveTarget(fromFile, url) {
  const [rawPath] = url.split('#');
  const clean = decodeURIComponent(rawPath.split('?')[0]);
  let abs;
  if (clean === '') abs = fromFile;
  else if (clean.startsWith('/')) abs = path.join(dist, clean);
  else abs = path.resolve(path.dirname(fromFile), clean);
  const candidates = [abs, path.join(abs, 'index.html'), abs + '.html'];
  return candidates.find((c) => fs.existsSync(c) && fs.statSync(c).isFile());
}

const broken = [];
const external = new Set();
let checked = 0;

for (const page of pages) {
  const html = fs.readFileSync(page, 'utf8');
  for (const m of html.matchAll(/\s(href|src)=("([^"]*)"|'([^']*)')/g)) {
    const url = (m[3] ?? m[4]).replace(/&amp;/g, '&');
    if (!url || url.startsWith('data:') || url.startsWith('mailto:') || url.startsWith('javascript:')) continue;
    if (/^[a-z]+:\/\//i.test(url) || url.startsWith('//')) {
      external.add(url);
      continue;
    }
    checked++;
    const target = resolveTarget(page, url);
    const where = path.relative(dist, page);
    if (!target) {
      broken.push(`${where}: ${url} (no such file)`);
      continue;
    }
    const frag = url.includes('#') ? decodeURIComponent(url.split('#')[1]) : '';
    if (frag && target.endsWith('.html') && !idsOf(target).has(frag)) {
      broken.push(`${where}: ${url} (no #${frag} on ${path.relative(dist, target)})`);
    }
  }
}

console.log(`check-links: ${pages.length} pages, ${checked} internal links and anchors checked, ${external.size} external links (not fetched)`);
if (process.argv.includes('--external')) for (const u of [...external].sort()) console.log(`  external: ${u}`);
if (broken.length) {
  console.error(`check-links: ${broken.length} broken:`);
  for (const b of broken) console.error(`  ${b}`);
  process.exit(1);
}
console.log('check-links: no broken links');
