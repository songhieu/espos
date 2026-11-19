# EspOS logo

| File | What it is |
| --- | --- |
| `logo-horizontal.svg`, `logo-horizontal-white.svg` | The icon and the word "EspOS" side by side. Ink for light backgrounds, white for dark ones. |
| `logo-stacked.svg`, `logo-stacked-white.svg` | The icon above the word, for square spaces. |
| `icon.svg`, `icon-white.svg` | The icon alone: a chip with 12 pins and a stack of three layers. |
| `favicon.svg` | The icon redrawn for 16 px (1 px pins on whole pixels, thicker layers). It turns white when the browser is in dark mode. |

- Colours: ink `#111111` on light backgrounds, white `#ffffff` on dark ones.
- The layers are holes in the chip, so the background shows through them. Do not fill them with
  another colour.
- Leave clear space around the logo of at least a quarter of the icon's height. Below 24 px use
  `favicon.svg` instead of `icon.svg`; never show the icon smaller than 16 px, or the horizontal
  logo smaller than 24 px tall.
- Do not stretch, outline, recolour in parts, add shadows or set the word in another font.

The word is set in [Lexend](https://github.com/googlefonts/lexend) (weight 650, letter spacing
adjusted by hand) and converted to outlines, so no font is needed to show it. Lexend is licensed
under the SIL Open Font License 1.1.
