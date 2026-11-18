package javax.microedition.lcdui;

import java.util.Vector;

/**
 * Text, wrapped (never cut short). PLAIN: text on the screen's background (on a card while it has commands, so the
 * focus shows); HYPERLINK: accent, underlined; BUTTON: a full-width button.
 */
public class StringItem extends Item {
    private String text;
    private final int appearance;
    private Font font;
    private Vector lines;
    private int linesW = -1;

    public StringItem(String label, String text) { this(label, text, PLAIN); }

    public StringItem(String label, String text, int appearanceMode) {
        super(label);
        if (appearanceMode < PLAIN || appearanceMode > BUTTON) throw new IllegalArgumentException();
        this.text = text;
        appearance = appearanceMode;
    }

    public String getText() { return text; }

    public void setText(String text) {
        this.text = text;
        lines = null;
        changed();
    }

    public int getAppearanceMode() { return appearance; }

    public void setFont(Font font) {
        this.font = font;
        lines = null;
        changed();
    }

    public Font getFont() { return font != null ? font : Font.getDefaultFont(); }

    // ---- drawing
    private Font drawFont() {
        if (font != null) return font;
        return appearance == BUTTON ? Ui.bold() : Font.getDefaultFont();
    }

    /** The space around the text: none for plain text, a card's padding otherwise. */
    private int pad() {
        if (appearance == BUTTON || (appearance == PLAIN && focusable())) return Ui.PAD;
        return appearance == HYPERLINK ? 3 : 0;
    }

    private Vector lines(int w) {
        int tw = w - 2 * pad();
        if (lines == null || linesW != tw) {
            lines = Screen.wrap(text, drawFont(), tw);
            linesW = tw;
        }
        return lines;
    }

    int contentHeight(int w) {
        Vector l = lines(w);
        if (l.size() == 0 && appearance != BUTTON) return 0;
        return Math.max(1, l.size()) * drawFont().getHeight() + 2 * pad();
    }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        if (h <= 0) return;
        Font f = drawFont();
        Vector l = lines(w);
        int p = pad(), fh = f.getHeight();
        if (appearance == BUTTON) {
            if (focused) {
                g.setColor(Screen.ACCENT);
                g.fillRoundRect(x, y, w, h, 12, 12);
            } else {
                Ui.card(g, x, y, w, h, false);
            }
            g.setFont(f);
            g.setColor(focused ? Ui.WHITE : Screen.INK);
            for (int i = 0; i < l.size(); i++)
                g.drawString((String) l.elementAt(i), x + w / 2, y + p + i * fh, Graphics.HCENTER | Graphics.TOP);
            return;
        }
        if (appearance == HYPERLINK) {
            if (focused) {
                g.setColor(Ui.TINT);
                g.fillRoundRect(x, y, w, h, 8, 8);
            }
            Ui.drawLines(g, l, f, x + p, y + p, Screen.ACCENT);
            for (int i = 0; i < l.size(); i++) {
                int lw = f.stringWidth((String) l.elementAt(i));
                g.fillRect(x + p, y + p + i * fh + f.getBaselinePosition() + 1, lw, 1);
            }
            return;
        }
        if (p > 0) Ui.card(g, x, y, w, h, focused);
        Ui.drawLines(g, l, f, x + p, y + p, Screen.INK);
    }
}
