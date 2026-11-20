The bot's art2 looks and standby screens, by bot-art (made in the screen-test project oledtest/tools, copied here so
the firmware's generator `tools/bot/gen.py` and the public repository have them). Change them there and copy again,
or change them here; `python3 tools/bot/gen.py` makes `src/bot/art.{h,cpp}` from them.

- `px.py`: pixel-exact 1-bit drawing (shapes at pixel centres, 1-px outlines, Bayer dither, sprites), the fonts.
- `digits2.py`: the clock and number digit sets.
- `looks2.py`: the dog's looks B "Mắt to", C "Chó pixel", D "Nét mảnh" (`face_only(look)`: scenes + the reply box).
- `standby2.py`: the 21 standby screens (`SCREENS`: background loop, live fields, overlay, drift; `draw_live` is the
  reference drawing the firmware's `src/bot/ui.cpp` follows).
- `fonts/Tiny5.ttf`: Tiny5 (SIL Open Font License 1.1, `fonts/Tiny5-OFL.txt`), a 5-px font with every Vietnamese letter.
