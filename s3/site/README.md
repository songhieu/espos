# EspOS developer docs

The documentation site for EspOS, built with [Astro Starlight](https://starlight.astro.build).
It looks like plain developer docs: sidebar, page, "On this page", search, previous / next.

```sh
npm install
npm run dev          # http://localhost:4321, reloads on save
npm run build        # static site in dist/ (with the Pagefind search index)
npm run check:links  # after a build: every internal link and #anchor in dist/
npm run preview      # serve dist/
npm run gen:cli      # regenerate reference/cli.md from `espos help --json`
```

Node.js 22.12 or newer.

## Where the docs are published

At https://espos.dev/docs. The Astro build is the authoring step (it renders the MDX and checks the links); the
hosted website shows the same pages from `dist/`, and `dist/` can be served as it is.

If `check:links` names a missing `/_astro/ec.*.css` after the code-block settings changed, Astro reused a stale
cached page: delete `node_modules/.astro/data-store.json` and build again.

## Layout

- `src/content/docs/`: the pages (`.mdx`/`.md`). The sidebar is in `astro.config.mjs`; `index.mdx` is the Overview.
- `src/components/`: `SiteTitle`, `Status` (Available / Preview / Planned labels), `StatusBoard` (the table on the
  Overview), `DeviceShots` (rows of 320 x 480 screenshots), `Shot` (one screenshot at its own size: landscape screens, panel pictures), `Layers`, `Stack`.
- `src/styles/theme.css`: the theme (neutral greys, the orange only for links and the active item, light and dark).
- `src/assets/shots/`: screenshots from the simulator. Each was made by running the code shown next to it.
- `scripts/gen-cli.mjs`: builds `reference/cli.md` from the CLI (`../sdk/packages/cli`, or `--from help.json`).
- `scripts/check-links.mjs`: offline link and anchor check of `dist/`.

## Writing rules

- Short, plain sentences. Every feature that is not in the firmware today carries a Planned or Preview label.
- Check every API name against `src/js/jsapp.cpp`; run code samples in the simulator.
- No ellipsis character in prose. Server addresses use `https://espos.example`.
- The site URL is a placeholder: set `ESPOS_DOCS_SITE` when building for a real host.
- The header's GitHub link (`REPO` in `astro.config.mjs`) points at the public monorepo, https://github.com/songhieu/espos.
