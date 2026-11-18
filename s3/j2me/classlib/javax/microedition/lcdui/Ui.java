package javax.microedition.lcdui;

import java.util.Vector;

/** The look shared by Form, List, Alert, TextBox and the items (Screen's colours, cards, marks). Not a MIDP class. */
final class Ui {
    static final int M = 8;              // the screens' side margin
    static final int GAP = 6;            // between items
    static final int PAD = 8;            // inside a card
    static final int MARK = 14;          // radio / check box size
    static final int TINT = 0xFFE9D6;    // a light accent (hyperlink focus, Gauge track focus)
    static final int WHITE = 0xFFFFFF;

    private Ui() {}

    static Font text() { return Font.getDefaultFont(); }
    static Font bold() { return Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_MEDIUM); }
    static Font label() { return Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_SMALL); }
    static Font small() { return Font.getFont(Font.FACE_SYSTEM, Font.STYLE_PLAIN, Font.SIZE_SMALL); }

    /** A white card; focused: an accent border two pixels wide. */
    static void card(Graphics g, int x, int y, int w, int h, boolean focused) {
        g.setColor(Screen.CARD);
        g.fillRoundRect(x, y, w, h, 12, 12);
        if (focused) {
            g.setColor(Screen.ACCENT);
            g.drawRoundRect(x, y, w - 1, h - 1, 12, 12);
            g.drawRoundRect(x + 1, y + 1, w - 3, h - 3, 10, 10);
        } else {
            g.setColor(Screen.LINE);
            g.drawRoundRect(x, y, w - 1, h - 1, 12, 12);
        }
    }

    /** A radio mark (MARK square) at x, y; inv: drawn on the accent (white). */
    static void radio(Graphics g, int x, int y, boolean on, boolean inv) {
        g.setColor(inv ? WHITE : on ? Screen.ACCENT : Screen.MUTED);
        g.drawArc(x, y, MARK - 1, MARK - 1, 0, 360);
        if (on) g.fillArc(x + 3, y + 3, MARK - 6, MARK - 6, 0, 360);
    }

    /** A check box at x, y. */
    static void check(Graphics g, int x, int y, boolean on, boolean inv) {
        if (on) {
            g.setColor(inv ? WHITE : Screen.ACCENT);
            g.fillRoundRect(x, y, MARK, MARK, 4, 4);
            g.setColor(inv ? Screen.ACCENT : WHITE);
            for (int i = 0; i < 2; i++) {
                g.drawLine(x + 3, y + 6 + i, x + 6, y + 9 + i);
                g.drawLine(x + 6, y + 9 + i, x + 11, y + 3 + i);
            }
        } else {
            g.setColor(inv ? WHITE : Screen.MUTED);
            g.drawRoundRect(x, y, MARK - 1, MARK - 1, 4, 4);
        }
    }

    /** A small triangle pointing down, centred on cx, cy (the popup's mark). */
    static void arrowDown(Graphics g, int cx, int cy, int color) {
        g.setColor(color);
        g.fillTriangle(cx - 5, cy - 2, cx + 5, cy - 2, cx, cy + 3);
    }

    /** Lines (wrapped text) one under another from x, y; returns their height. */
    static int drawLines(Graphics g, Vector lines, Font f, int x, int y, int color) {
        g.setFont(f);
        g.setColor(color);
        int fh = f.getHeight();
        int top = g.getClipY(), bottom = top + g.getClipHeight();
        for (int i = 0; i < lines.size(); i++) {
            int ly = y + i * fh;
            if (ly + fh > top && ly < bottom) g.drawString((String) lines.elementAt(i), x, ly, Graphics.LEFT | Graphics.TOP);
        }
        return lines.size() * fh;
    }

    static int height(Vector lines, Font f) { return lines.size() * f.getHeight(); }

    /** The scroll position mark at the right edge of a view top..top+vh showing total pixels from scroll. */
    static void scrollbar(Graphics g, int top, int vh, int total, int scroll) {
        if (total <= vh || vh <= 0) return;
        int th = Math.max(16, vh * vh / total);
        int ty = top + (vh - th) * scroll / Math.max(1, total - vh);
        g.setColor(0xC4C9D2);
        g.fillRoundRect(Display.W - 4, ty, 3, th, 3, 3);
    }

    /** Where Screen.paintTitle starts the content (before the first paint tells). */
    static int contentTop(String title) {
        if (title == null || title.length() == 0) return 4;
        return 6 + Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_LARGE).getHeight() + 6 + 4;
    }

    /** Keeps a scroll position inside 0..total-vh. */
    static int clampScroll(int s, int total, int vh) {
        if (s > total - vh) s = total - vh;
        return s < 0 ? 0 : s;
    }
}
