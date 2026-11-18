package javax.microedition.lcdui;

/**
 * Choices on a Form: a card of rows with radio marks (EXCLUSIVE) or check boxes (MULTIPLE). Focused, up / down move
 * among its rows (leaving at the ends) and OK picks. POPUP shows the chosen row with a mark; OK opens the rows (OK
 * again picks and closes), left / right step through the choices without opening.
 */
public class ChoiceGroup extends Item implements Choice {
    private static final int IN = 3;  // rows inside the card

    final Choices ch;
    private int hi;          // the highlighted row while focused
    private boolean open;    // POPUP: the rows are shown

    public ChoiceGroup(String label, int choiceType) { this(label, choiceType, new String[0], null); }

    public ChoiceGroup(String label, int choiceType, String[] stringElements, Image[] imageElements) {
        super(label);
        if (choiceType != EXCLUSIVE && choiceType != MULTIPLE && choiceType != POPUP) throw new IllegalArgumentException();
        if (stringElements == null) throw new NullPointerException();
        if (imageElements != null && imageElements.length != stringElements.length) throw new IllegalArgumentException();
        ch = new Choices(choiceType);
        for (int i = 0; i < stringElements.length; i++)
            ch.insert(i, stringElements[i], imageElements != null ? imageElements[i] : null);
    }

    // ---- Choice
    public int append(String stringPart, Image imagePart) {
        int i = ch.insert(ch.size(), stringPart, imagePart);
        changed();
        return i;
    }

    public void insert(int elementNum, String stringPart, Image imagePart) {
        ch.insert(elementNum, stringPart, imagePart);
        if (ch.size() > 1 && hi >= elementNum) hi++;
        changed();
    }

    public void delete(int elementNum) {
        ch.delete(elementNum);
        if (hi > elementNum || hi >= ch.size()) hi--;
        if (hi < 0) hi = 0;
        changed();
    }

    public void deleteAll() {
        ch.deleteAll();
        hi = 0;
        open = false;
        changed();
    }

    public void set(int elementNum, String stringPart, Image imagePart) {
        ch.set(elementNum, stringPart, imagePart);
        changed();
    }

    public String getString(int elementNum) { return ch.string(elementNum); }
    public Image getImage(int elementNum) { return ch.image(elementNum); }
    public int size() { return ch.size(); }
    public boolean isSelected(int elementNum) { return ch.selected(elementNum); }
    public int getSelectedIndex() { return ch.selectedIndex(); }
    public int getSelectedFlags(boolean[] selectedArray_return) { return ch.getFlags(selectedArray_return); }

    public void setSelectedIndex(int elementNum, boolean selected) {
        ch.select(elementNum, selected);
        changed();
    }

    public void setSelectedFlags(boolean[] selectedArray) {
        ch.setFlags(selectedArray);
        changed();
    }

    public void setFitPolicy(int fitPolicy) {
        if (fitPolicy < TEXT_WRAP_DEFAULT || fitPolicy > TEXT_WRAP_OFF) throw new IllegalArgumentException();
        ch.fit = fitPolicy;
    }

    public int getFitPolicy() { return ch.fit; }

    public void setFont(int elementNum, Font font) {
        ch.setFont(elementNum, font);
        changed();
    }

    public Font getFont(int elementNum) { return ch.font(elementNum); }

    // ---- on the Form
    private boolean closed() { return ch.type == POPUP && !open; }

    private boolean marks() { return ch.type != POPUP || open; }

    boolean focusable() { return ch.size() > 0 || super.focusable(); }

    int contentHeight(int w) {
        int n = ch.size();
        if (n == 0) return 0;
        int rw = w - 2 * IN;
        if (closed()) {
            int s = Math.max(0, ch.selectedIndex());
            return ch.rowHeight(s, rw - 20, false) + 2 * IN;
        }
        int h = 2 * IN;
        for (int i = 0; i < n; i++) h += ch.rowHeight(i, rw, marks());
        return h;
    }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        int n = ch.size();
        if (n == 0) return;
        Ui.card(g, x, y, w, h, focused);
        int rw = w - 2 * IN;
        if (closed()) {
            int s = Math.max(0, ch.selectedIndex());
            int rh = ch.rowHeight(s, rw - 20, false);
            ch.paintRow(g, s, x + IN, y + IN, rw - 20, rh, false, false);
            Ui.arrowDown(g, x + w - 16, y + h / 2, focused ? Screen.ACCENT : Screen.MUTED);
            return;
        }
        int ry = y + IN, top = g.getClipY(), bottom = top + g.getClipHeight();
        for (int i = 0; i < n; i++) {
            int rh = ch.rowHeight(i, rw, marks());
            if (ry + rh > top && ry < bottom) ch.paintRow(g, i, x + IN, ry, rw, rh, marks(), focused && i == hi);
            ry += rh;
        }
    }

    /** Picks row i (EXCLUSIVE, POPUP) or ticks it (MULTIPLE); the Form's listener hears it. */
    private void choose(int i) {
        if (i < 0 || i >= ch.size()) return;
        ch.select(i, ch.type == MULTIPLE ? !ch.selected(i) : true);
        if (ch.type == POPUP) open = false;
        changed();
        stateChanged();
    }

    boolean keyItem(int type, int code) {
        int n = ch.size();
        if (type == 2 || n == 0) return false;
        int a = Canvas.action(code);
        if (closed()) {
            if (a == Canvas.FIRE && type == 1) {
                open = true;
                hi = Math.max(0, ch.selectedIndex());
                changed();
                return true;
            }
            if (a == Canvas.LEFT || a == Canvas.RIGHT) {
                int s = Math.max(0, ch.selectedIndex()) + (a == Canvas.LEFT ? -1 : 1);
                choose((s + n) % n);
                return true;
            }
            return false;
        }
        if (a == Canvas.UP || a == Canvas.DOWN) {
            int next = hi + (a == Canvas.UP ? -1 : 1);
            if (next < 0 || next >= n) return open;  // an open popup keeps the keys; else the Form moves on
            hi = next;
            changed();
            return true;
        }
        if (a == Canvas.FIRE && type == 1) {
            choose(hi);
            return true;
        }
        return false;
    }

    void focusIn(int dir) {
        int n = ch.size();
        if (dir > 0) hi = 0;
        else if (dir < 0) hi = Math.max(0, n - 1);
        else hi = Math.max(0, Math.min(hi, n - 1));
    }

    void focusOut() {
        if (open) {
            open = false;
            changed();
        }
    }

    /** The row at y (from the item's top), or -1. */
    private int rowAt(int y, int w) {
        int ry = labelHeight(w) + IN, rw = w - 2 * IN;
        if (closed()) return -1;
        for (int i = 0; i < ch.size(); i++) {
            int rh = ch.rowHeight(i, rw, marks());
            if (y >= ry && y < ry + rh) return i;
            ry += rh;
        }
        return -1;
    }

    void pointerItem(int type, int x, int y, int w) {
        if (ch.size() == 0) return;
        if (type == 4) {
            int r = rowAt(y, w);
            if (r >= 0) {
                hi = r;
                changed();
            }
        } else if (type == 5) {
            if (closed()) {
                if (y >= labelHeight(w)) {
                    open = true;
                    hi = Math.max(0, ch.selectedIndex());
                    changed();
                }
                return;
            }
            choose(rowAt(y, w));
        }
    }

    int focusTop(int w) {
        if (closed() || ch.size() == 0) return 0;
        int ry = labelHeight(w) + IN, rw = w - 2 * IN;
        for (int i = 0; i < hi && i < ch.size(); i++) ry += ch.rowHeight(i, rw, marks());
        return hi == 0 ? 0 : ry;  // the first row: the label too
    }

    int focusHeight(int w) {
        if (closed() || ch.size() == 0) return height(w);
        int rh = ch.rowHeight(Math.min(hi, ch.size() - 1), w - 2 * IN, marks());
        return hi == 0 ? labelHeight(w) + IN + rh : rh;
    }
}
