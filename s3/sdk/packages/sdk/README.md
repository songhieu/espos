# @espos/sdk

TypeScript typings for the EspOS JavaScript API and JSON schemas for package manifests.

## API level 1 (`espos.d.ts`)

What firmware 0.5.0 (the first that installs `.espk` packages) gives a JavaScript app, exactly: the prelude and the native functions in
`s3/src/js/jsapp.cpp`, described in `s3/docs/apps.md`. Tests in `packages/cli/test/sdk.test.mjs` and `i18n.test.mjs` compare the
declared globals, `ui.*` functions, `app.screen` and `app.lang` (and its tags) with the C++ sources.

| global | what | permission |
|---|---|---|
| `ui` | `column`, `row`, `card` (`half` in landscape), `title`, `text`, `big`, `button`, `chips`, `list`, `progress`, `spacer`: the screen as a tree | |
| `app` | `title`, `render`, `refresh()`, `badge(n)`, `arg`, `screen` (`w`, `h`, `landscape`), `lang` (the device's language: `en`, `vi`, `zh-Hans`, `es`, `fr`, `de`, `ja`, `ko`, `pt-BR`, `ru`, `id`) | `badge` needs `notify` |
| `timer` | `every(ms, fn)`, `after(ms, fn)`, `cancel(id)`; at most 8, while the app is open | |
| `sound` | `chime()`, `beep()`, `click()` | `sound` |
| `notify` | `notify(title, body, opts)`, `notify.schedule(at, ...)`, `notify.cancel(tag)` | `notify` |
| `toast(text)` | a short message | |
| `store` | `get(key, default)`, `set(key, value)`: JSON per app, keys 1-14 characters, values up to 3000 | |

Not in level 1: `console`, `setTimeout`/`setInterval`, `fetch` or any network, modules at runtime (`espos build`
bundles them), Promise callbacks (the runtime never runs the job queue, so `async`/`await` and `.then()` never
continue), lifecycle hooks. The modules planned in `s3/docs/platform.md` (lifecycle, storage quotas, `net`,
`data`, `theme`, `sensors`, ...) are not declared, because they do not exist yet.

Use in a project (what `espos create --template js-app-ts` sets up):

```json
{"compilerOptions": {"lib": ["ES2023"], "types": [], "strict": true, "noEmit": true, "module": "ESNext", "moduleResolution": "Bundler"},
 "include": ["src/**/*.ts", "types/*.d.ts"]}
```

with `types/espos.d.ts` copied by `espos types`, or `"types": ["@espos/sdk"]` once the package is installed.
Plain JavaScript gets the same checks in an editor with `// @ts-check` and the file in the project.

## Schemas (`schemas/`)

| file | what |
|---|---|
| `manifest.schema.json` | `manifest.json` in the current format (firmware 0.5.0, s3/docs/packages.md), with `i18n` (names in other languages) and the engines `lang` and `font`. `espos check` applies these rules and more. |
| `manifest-v2.schema.json` | **Draft.** Manifest v2 from s3/docs/platform.md (`type`, `runtime`, `api {min, target}`, `screens`, `theme`, the version 1 permission list, `entry`). No firmware reads it yet. |
| `project.schema.json` | `espos.json`: what `espos build` puts into a package. |
