package javax.microedition.lcdui;

/**
 * Form, List, Alert and TextBox: drawn by this library (no native widgets), in the device's look: a title bar, then
 * the content; the soft keys show the commands (on the device's keypad, not on this screen).
 */
public abstract class Screen extends Displayable {
    Screen() {}

    /** Draws the whole screen (w x h) into g. */
    abstract void paintScreen(Graphics g, int w, int h);

    /** A key (1 pressed, 2 released, 3 repeated); true when it was used. */
    boolean keyScreen(int type, int code) { return false; }

    /** A touch (4 pressed, 5 released, 6 dragged) at x, y. */
    void pointerScreen(int type, int x, int y) {}

    /** Display.setCurrentItem: screens that hold items (Form, Alert) focus it and scroll it into view. */
    void focusItem(Item item) {}

    /** Draws it again soon (from any thread). */
    void invalidate() {
        Display d = Display.instance;
        if (d != null) d.requestRepaint(this, 0, 0, Display.W, Display.H);
    }

    void changed() {
        super.changed();
        invalidate();
    }

    final void paintFrame(Graphics g) { paintScreen(g, Display.W, Display.H); }

    final void key(int type, int code) {
        if (keyScreen(type, code)) invalidate();
    }

    final void pointer(int type, int x, int y) { pointerScreen(type, x, y); }

    // ---- shared drawing: the look of every screen
    static final int BG = 0xF4F5F7, INK = 0x16181D, MUTED = 0x6B7280, LINE = 0xE3E6EB, ACCENT = 0xFF7A00, CARD = 0xFFFFFF;

    /** The title bar; returns the y where the content starts. */
    static int paintTitle(Graphics g, String title, int w) {
        g.setColor(BG);
        g.fillRect(0, 0, w, Display.H);
        if (title == null || title.length() == 0) return 4;
        // wrapped, never cut: a long title takes the medium size, then more lines
        Font f = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_LARGE);
        if (f.stringWidth(title) > w - 16) f = Font.getFont(Font.FACE_SYSTEM, Font.STYLE_BOLD, Font.SIZE_MEDIUM);
        g.setFont(f);
        g.setColor(INK);
        java.util.Vector lines = wrap(title, f, w - 16);
        int y = 6;
        for (int i = 0; i < lines.size(); i++, y += f.getHeight())
            g.drawString((String) lines.elementAt(i), 8, y, Graphics.LEFT | Graphics.TOP);
        y += 6;
        g.setColor(LINE);
        g.fillRect(0, y - 1, w, 1);
        return y + 4;
    }

    /** Lines of text no wider than w (words kept whole where they fit; never cut short with dots). */
    static java.util.Vector wrap(String s, Font f, int w) {
        java.util.Vector out = new java.util.Vector();
        if (s == null) return out;
        int start = 0, n = s.length();
        while (start < n) {
            int nl = s.indexOf('\n', start);
            int end = nl < 0 ? n : nl;
            int a = start;
            while (a < end) {
                int b = end;
                while (b > a + 1 && f.substringWidth(s, a, b - a) > w) {
                    int sp = s.lastIndexOf(' ', b - 1);
                    b = sp > a ? sp : b - 1;
                }
                out.addElement(s.substring(a, b));
                a = b;
                while (a < end && s.charAt(a) == ' ') a++;
            }
            if (end == start) out.addElement("");
            start = end + 1;
        }
        return out;
    }
}
