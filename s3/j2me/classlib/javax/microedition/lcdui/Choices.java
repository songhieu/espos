package javax.microedition.lcdui;

import java.util.Vector;

/**
 * The elements of a List or a ChoiceGroup (strings, images, fonts, the selection, by Choice's rules) and how one
 * element's row looks. Not a MIDP class. Text always wraps (TEXT_WRAP_OFF too: nothing is ever cut short).
 */
final class Choices {
    static final int VPAD = 6;  // above and below a row's content

    final int type;
    private final Vector strings = new Vector(), images = new Vector(), fonts = new Vector(), wrapped = new Vector();
    private boolean[] sel = new boolean[8];
    private int wrapW = -1;
    int fit = Choice.TEXT_WRAP_DEFAULT;

    Choices(int type) { this.type = type; }

    int size() { return strings.size(); }

    /** One element selected at all times (when there are any): not MULTIPLE. */
    boolean exclusive() { return type != Choice.MULTIPLE; }

    void check(int i) {
        if (i < 0 || i >= strings.size()) throw new IndexOutOfBoundsException("element " + i);
    }

    String string(int i) {
        check(i);
        return (String) strings.elementAt(i);
    }

    Image image(int i) {
        check(i);
        return (Image) images.elementAt(i);
    }

    Font font(int i) {
        check(i);
        Font f = (Font) fonts.elementAt(i);
        return f != null ? f : Font.getDefaultFont();
    }

    void setFont(int i, Font f) {
        check(i);
        fonts.setElementAt(f, i);
        wrapped.setElementAt(null, i);
    }

    int insert(int i, String s, Image img) {
        if (s == null) throw new NullPointerException();
        int n = strings.size();
        if (i < 0 || i > n) throw new IndexOutOfBoundsException("element " + i);
        strings.insertElementAt(s, i);
        images.insertElementAt(ImageItem.snapshot(img), i);
        fonts.insertElementAt(null, i);
        wrapped.insertElementAt(null, i);
        if (n + 1 > sel.length) {
            boolean[] b = new boolean[sel.length * 2];
            System.arraycopy(sel, 0, b, 0, n);
            sel = b;
        }
        System.arraycopy(sel, i, sel, i + 1, n - i);
        sel[i] = false;
        if (exclusive() && n == 0) sel[0] = true;
        return i;
    }

    void set(int i, String s, Image img) {
        check(i);
        if (s == null) throw new NullPointerException();
        strings.setElementAt(s, i);
        images.setElementAt(ImageItem.snapshot(img), i);
        wrapped.setElementAt(null, i);
    }

    void delete(int i) {
        check(i);
        boolean was = sel[i];
        strings.removeElementAt(i);
        images.removeElementAt(i);
        fonts.removeElementAt(i);
        wrapped.removeElementAt(i);
        int n = strings.size();
        System.arraycopy(sel, i + 1, sel, i, n - i);
        sel[n] = false;
        if (exclusive() && was && n > 0) sel[Math.min(i, n - 1)] = true;
    }

    void deleteAll() {
        strings.removeAllElements();
        images.removeAllElements();
        fonts.removeAllElements();
        wrapped.removeAllElements();
        for (int i = 0; i < sel.length; i++) sel[i] = false;
    }

    boolean selected(int i) {
        check(i);
        return sel[i];
    }

    int selectedIndex() {
        if (!exclusive()) return -1;
        for (int i = 0; i < strings.size(); i++) if (sel[i]) return i;
        return -1;
    }

    /** Choice.setSelectedIndex: exclusive kinds ignore "off" (one stays selected). */
    void select(int i, boolean on) {
        check(i);
        if (exclusive()) {
            if (!on) return;
            for (int k = 0; k < strings.size(); k++) sel[k] = false;
        }
        sel[i] = on;
    }

    int getFlags(boolean[] a) {
        if (a == null) throw new NullPointerException();
        int n = strings.size();
        if (a.length < n) throw new IllegalArgumentException();
        int c = 0;
        for (int i = 0; i < a.length; i++) {
            a[i] = i < n && sel[i];
            if (a[i]) c++;
        }
        return c;
    }

    void setFlags(boolean[] a) {
        if (a == null) throw new NullPointerException();
        int n = strings.size();
        if (a.length < n) throw new IllegalArgumentException();
        if (!exclusive()) {
            for (int i = 0; i < n; i++) sel[i] = a[i];
            return;
        }
        if (n == 0) return;
        int first = 0;
        for (int i = n - 1; i >= 0; i--) if (a[i]) first = i;  // the first true one, else element 0
        select(first, true);
    }

    // ---- a row: [mark] [image] text, the text wrapped

    Vector lines(int i, int tw) {
        if (tw != wrapW) {
            for (int k = 0; k < wrapped.size(); k++) wrapped.setElementAt(null, k);
            wrapW = tw;
        }
        Vector v = (Vector) wrapped.elementAt(i);
        if (v == null) {
            v = Screen.wrap(string(i), font(i), tw);
            wrapped.setElementAt(v, i);
        }
        return v;
    }

    private int textX(int i, boolean marks) {
        int x = Ui.PAD;
        if (marks) x += Ui.MARK + 8;
        Image img = image(i);
        if (img != null) x += img.getWidth() + 6;
        return x;
    }

    private int textW(int i, int w, boolean marks) { return Math.max(8, w - textX(i, marks) - Ui.PAD); }

    int rowHeight(int i, int w, boolean marks) {
        int h = Math.max(1, lines(i, textW(i, w, marks)).size()) * font(i).getHeight();
        Image img = image(i);
        if (img != null && img.getHeight() > h) h = img.getHeight();
        if (marks && Ui.MARK > h) h = Ui.MARK;
        return h + 2 * VPAD;
    }

    /** Row i at x, y (w x h); marks: a radio / check mark first; hi: highlighted (accent, white text). */
    void paintRow(Graphics g, int i, int x, int y, int w, int h, boolean marks, boolean hi) {
        if (hi) {
            g.setColor(Screen.ACCENT);
            g.fillRoundRect(x, y, w, h, 10, 10);
        }
        int cx = x + Ui.PAD;
        if (marks) {
            int my = y + (h - Ui.MARK) / 2;
            if (type == Choice.MULTIPLE) Ui.check(g, cx, my, sel[i], hi);
            else Ui.radio(g, cx, my, sel[i], hi);
            cx += Ui.MARK + 8;
        }
        Image img = image(i);
        if (img != null) {
            g.drawImage(img, cx, y + (h - img.getHeight()) / 2, Graphics.LEFT | Graphics.TOP);
            cx += img.getWidth() + 6;
        }
        Font f = font(i);
        Vector l = lines(i, textW(i, w, marks));
        int th = Math.max(1, l.size()) * f.getHeight();
        Ui.drawLines(g, l, f, cx, y + (h - th) / 2, hi ? Ui.WHITE : Screen.INK);
    }
}
