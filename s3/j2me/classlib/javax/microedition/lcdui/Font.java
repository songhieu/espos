package javax.microedition.lcdui;

import com.espos.j2me.Native;

/** MIDP fonts: three sizes (12, 14, 16 px Ubuntu on EspOS), plain or bold; italic and underline draw as plain. */
public final class Font {
    public static final int FACE_SYSTEM = 0, FACE_MONOSPACE = 32, FACE_PROPORTIONAL = 64;
    public static final int STYLE_PLAIN = 0, STYLE_BOLD = 1, STYLE_ITALIC = 2, STYLE_UNDERLINED = 4;
    public static final int SIZE_SMALL = 8, SIZE_MEDIUM = 0, SIZE_LARGE = 16;
    public static final int FONT_STATIC_TEXT = 0, FONT_INPUT_TEXT = 1;

    private static final Font[] cache = new Font[3 * 3 * 8];
    private static Font def;

    private final int face, style, size;
    private final int id;  // Native's font: 0 small, 1 medium, 2 large, +3 bold
    private int height = -1, baseline = -1;

    private Font(int face, int style, int size) {
        this.face = face;
        this.style = style;
        this.size = size;
        int s = size == SIZE_SMALL ? 0 : size == SIZE_LARGE ? 2 : 1;
        id = s + ((style & STYLE_BOLD) != 0 ? 3 : 0);
    }

    public static Font getFont(int face, int style, int size) {
        if ((face != FACE_SYSTEM && face != FACE_MONOSPACE && face != FACE_PROPORTIONAL) || (style & ~7) != 0
                || (size != SIZE_SMALL && size != SIZE_MEDIUM && size != SIZE_LARGE))
            throw new IllegalArgumentException();
        int fi = face == FACE_SYSTEM ? 0 : face == FACE_MONOSPACE ? 1 : 2;
        int si = size == SIZE_SMALL ? 0 : size == SIZE_MEDIUM ? 1 : 2;
        int k = (fi * 3 + si) * 8 + style;
        synchronized (cache) {
            if (cache[k] == null) cache[k] = new Font(face, style, size);
            return cache[k];
        }
    }

    public static Font getDefaultFont() {
        if (def == null) def = getFont(FACE_SYSTEM, STYLE_PLAIN, SIZE_MEDIUM);
        return def;
    }

    public static Font getFont(int fontSpecifier) {
        if (fontSpecifier != FONT_STATIC_TEXT && fontSpecifier != FONT_INPUT_TEXT) throw new IllegalArgumentException();
        return getDefaultFont();
    }

    public int getStyle() { return style; }
    public int getSize() { return size; }
    public int getFace() { return face; }
    public boolean isPlain() { return style == STYLE_PLAIN; }
    public boolean isBold() { return (style & STYLE_BOLD) != 0; }
    public boolean isItalic() { return (style & STYLE_ITALIC) != 0; }
    public boolean isUnderlined() { return (style & STYLE_UNDERLINED) != 0; }

    public int getHeight() {
        if (height < 0) height = Native.fontHeight(id);
        return height;
    }

    public int getBaselinePosition() {
        if (baseline < 0) baseline = Native.fontBaseline(id);
        return baseline;
    }

    public int charWidth(char ch) { return Native.textWidth(String.valueOf(ch), 0, 1, id); }

    public int charsWidth(char[] ch, int offset, int length) {
        if (offset < 0 || length < 0 || offset + length > ch.length) throw new ArrayIndexOutOfBoundsException();
        return Native.textWidth(new String(ch, offset, length), 0, length, id);
    }

    public int stringWidth(String str) {
        if (str == null) throw new NullPointerException();
        return Native.textWidth(str, 0, str.length(), id);
    }

    public int substringWidth(String str, int offset, int len) {
        if (offset < 0 || len < 0 || offset + len > str.length()) throw new StringIndexOutOfBoundsException();
        return Native.textWidth(str, offset, len, id);
    }

    /** The font Native draws with. */
    int id() { return id; }
}
