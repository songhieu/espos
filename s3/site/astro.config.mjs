// @ts-check
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Where the docs are served. Placeholder until the owner picks a host.
const SITE = process.env.ESPOS_DOCS_SITE ?? 'https://docs.espos.example';
// The public monorepo: s3/ (the S3 firmware, the SDK and these docs), server/, firmware/ (the X4).
const REPO = 'https://github.com/songhieu/espos';

const planned = { text: 'Planned', variant: 'note' };
const preview = { text: 'Preview', variant: 'caution' };
const inProgress = { text: 'In progress', variant: 'caution' };

export default defineConfig({
  site: SITE,
  // No typographic rewriting: "..." must stay three dots (the docs never show an ellipsis character),
  // and straight quotes stay straight, as in code and device messages.
  markdown: { smartypants: false },
  integrations: [
    starlight({
      title: 'EspOS',
      description:
        'Developer documentation for EspOS, the open-source app platform for ESP32-S3 devices: apps, packages, the store, simulators and the SDK.',
      logo: { src: './src/assets/logo.svg', alt: 'EspOS' },
      social: [{ icon: 'github', label: 'GitHub', href: REPO }],
      favicon: '/favicon.svg',
      customCss: [
        '@fontsource-variable/inter',
        '@fontsource-variable/jetbrains-mono',
        './src/styles/theme.css',
      ],
      components: {
        SiteTitle: './src/components/SiteTitle.astro',
      },
      lastUpdated: false,
      pagination: true,
      tableOfContents: { minHeadingLevel: 2, maxHeadingLevel: 3 },
      expressiveCode: {
        themes: ['github-dark-default', 'github-light'],
        styleOverrides: {
          borderRadius: '6px',
          codeFontFamily: "'JetBrains Mono Variable', ui-monospace, SFMono-Regular, Menlo, monospace",
          codeFontSize: '0.84rem',
          frames: { shadowColor: 'transparent' },
        },
      },
      sidebar: [
        { label: 'Overview', link: '/' },
        {
          label: 'Getting started',
          items: [
            { label: 'What is EspOS', slug: 'getting-started/introduction' },
            { label: 'Set up your tools', slug: 'getting-started/install' },
            { label: 'Your first app', slug: 'getting-started/first-app' },
            { label: 'The development loop', slug: 'getting-started/workflow' },
          ],
        },
        {
          label: 'Concepts',
          items: [
            { label: 'The OS and packages', slug: 'concepts/architecture' },
            { label: 'Package types', slug: 'concepts/package-types' },
            { label: 'The manifest', slug: 'concepts/manifest' },
            { label: 'API levels', slug: 'concepts/api-levels' },
            { label: 'Permissions', slug: 'concepts/permissions' },
            { label: 'App lifecycle', slug: 'concepts/lifecycle' },
            { label: 'Storage', slug: 'concepts/storage' },
            { label: 'The store', slug: 'concepts/store' },
            { label: 'Notifications', slug: 'concepts/notifications' },
            { label: 'Themes and design tokens', slug: 'concepts/themes', badge: planned },
            { label: 'Performance budgets', slug: 'concepts/performance' },
            { label: 'Security model', slug: 'concepts/security' },
            { label: 'Hosted and self-hosted', slug: 'concepts/hosting' },
          ],
        },
        {
          label: 'Guides',
          items: [
            {
              label: 'JavaScript apps',
              items: [
                { label: 'Building the screen', slug: 'guides/js/ui' },
                { label: 'State and rendering', slug: 'guides/js/state' },
                { label: 'Timers', slug: 'guides/js/timers' },
                { label: 'Sound', slug: 'guides/js/sound' },
                { label: 'Notifications and badges', slug: 'guides/js/notifications' },
                { label: 'Saving data', slug: 'guides/js/storage' },
                { label: 'TypeScript and modules', slug: 'guides/js/typescript', badge: preview },
              ],
            },
            { label: 'Languages and translation', slug: 'guides/languages' },
            { label: 'Landscape and screen size', slug: 'guides/landscape' },
            { label: 'Packaging, parts and add-ons', slug: 'guides/packaging' },
            { label: 'Java (J2ME) games', slug: 'guides/java-games', badge: preview },
            { label: 'Test in the simulator', slug: 'guides/simulator' },
            { label: 'Test in QEMU', slug: 'guides/qemu' },
            { label: 'Work with a real device', slug: 'guides/device' },
            { label: 'Publish to the store', slug: 'guides/publishing' },
            { label: 'Inside the Java VM', slug: 'guides/java-vm' },
          ],
        },
        {
          label: 'Hardware & porting',
          items: [
            { label: 'Overview', slug: 'hardware' },
            { label: 'Supported boards', slug: 'guides/boards' },
            { label: 'Board-specific code', slug: 'hardware/board-code', badge: inProgress },
            { label: 'Port to a new board', slug: 'hardware/porting', badge: inProgress },
            { label: 'Hardware profiles', slug: 'hardware/profiles' },
            { label: 'Board support packages', slug: 'hardware/bsp', badge: inProgress },
          ],
        },
        {
          label: 'Reference',
          items: [
            { label: 'JavaScript API (level 1)', slug: 'reference/js-api' },
            { label: 'Manifest fields', slug: 'reference/manifest' },
            { label: '.espk file format', slug: 'reference/espk' },
            { label: 'Declarative faces and widgets', slug: 'reference/decl', badge: inProgress },
            { label: 'Store catalog', slug: 'reference/catalog' },
            { label: 'Package sync (s3.pkgs)', slug: 'reference/pkgs-sync' },
            { label: 'CLI', slug: 'reference/cli', badge: preview },
            { label: 'Simulator commands', slug: 'reference/sim-commands' },
            { label: 'Dev console commands', slug: 'reference/dev-console' },
            { label: 'Limits', slug: 'reference/limits' },
            { label: 'Error messages', slug: 'reference/errors' },
          ],
        },
        {
          label: 'Platform roadmap',
          items: [
            { label: 'Roadmap', slug: 'roadmap' },
            { label: 'Hardware and drivers', slug: 'roadmap/hardware', badge: planned },
            { label: 'Self-hosting', slug: 'roadmap/self-hosting', badge: preview },
          ],
        },
        {
          label: 'Contributing',
          items: [
            { label: 'How to contribute', slug: 'contributing' },
            { label: 'Repository layout', slug: 'contributing/repo-layout' },
            { label: 'Build the firmware', slug: 'contributing/firmware' },
            { label: 'Code and UI style', slug: 'contributing/style' },
          ],
        },
      ],
    }),
  ],
});
