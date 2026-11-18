package javax.microedition.lcdui;

/**
 * Text on a Form, typed in place with the keypad (TextEditor: multi-tap letters, # mode, * delete). The card shows
 * the input mode's tag at its top right while focused; up / down leave the field.
 */
public class TextField extends Item {
    public static final int ANY = 0, EMAILADDR = 1, NUMERIC = 2, PHONENUMBER = 3, URL = 4, DECIMAL = 5;
    public static final int PASSWORD = 0x10000, UNEDITABLE = 0x20000, SENSITIVE = 0x40000, NON_PREDICTIVE = 0x80000,
            INITIAL_CAPS_WORD = 0x100000, INITIAL_CAPS_SENTENCE = 0x200000;
    public static final int CONSTRAINT_MASK = 0xFFFF;

    private static final int TAG_W = 30;  // room kept at the right for the mode tag

    final TextEditor ed;
    private String inputMode;

    public TextField(String label, String text, int maxSize, int constraints) {
        super(label);
        ed = new TextEditor(text, maxSize, constraints);
    }

    public String getString() { return ed.get(); }

    public void setString(String text) {
        ed.set(text);
        changed();
    }

    public int getChars(char[] data) {
        int n = ed.size();
        if (data.length < n) throw new ArrayIndexOutOfBoundsException();
        ed.buf.getChars(0, n, data, 0);
        return n;
    }

    public void setChars(char[] data, int offset, int length) {
        if (data == null) {
            setString("");
            return;
        }
        if (offset < 0 || length < 0 || offset + length > data.length) throw new ArrayIndexOutOfBoundsException();
        setString(new String(data, offset, length));
    }

    public void insert(String src, int position) {
        ed.insert(src, position);
        changed();
    }

    public void insert(char[] data, int offset, int length, int position) {
        if (data == null) throw new NullPointerException();
        if (offset < 0 || length < 0 || offset + length > data.length) throw new ArrayIndexOutOfBoundsException();
        insert(new String(data, offset, length), position);
    }

    public void delete(int offset, int length) {
        ed.delete(offset, length);
        changed();
    }

    public int getMaxSize() { return ed.max; }

    public int setMaxSize(int maxSize) {
        int m = ed.setMax(maxSize);
        changed();
        return m;
    }

    public int size() { return ed.size(); }

    public int getCaretPosition() { return ed.caret; }

    public void setConstraints(int constraints) {
        ed.setConstraints(constraints);
        changed();
    }

    public int getConstraints() { return ed.constraints; }

    public void setInitialInputMode(String characterSubset) { inputMode = characterSubset; }

    // ---- on the Form
    boolean focusable() { return ed.editable() || super.focusable(); }

    private int textW(int w) { return w - 2 * Ui.PAD - (ed.editable() ? TAG_W : 0); }

    int contentHeight(int w) {
        return ed.height(Font.getDefaultFont(), textW(w)) + 2 * Ui.PAD;
    }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        ed.screen = owner;
        Ui.card(g, x, y, w, h, focused);
        ed.paint(g, Font.getDefaultFont(), x + Ui.PAD, y + Ui.PAD, textW(w), focused && ed.editable());
        String tag = focused ? ed.modeName() : null;
        if (tag != null) {
            Font f = Ui.small();
            g.setFont(f);
            g.setColor(Screen.MUTED);
            g.drawString(tag, x + w - Ui.PAD, y + Ui.PAD, Graphics.RIGHT | Graphics.TOP);
        }
    }

    boolean keyItem(int type, int code) {
        ed.screen = owner;
        String before = ed.get();
        if (ed.key(type, code)) {
            changed();
            if (!before.equals(ed.get())) stateChanged();
            return true;
        }
        return super.keyItem(type, code);
    }

    void pointerItem(int type, int x, int y, int w) {
        if (type == 4) {
            ed.touch(x - Ui.PAD, y - labelHeight(w) - Ui.PAD);
            changed();
        } else if (type == 5 && !ed.editable()) {
            super.pointerItem(type, x, y, w);
        }
    }

    int focusTop(int w) {
        int h = height(w), ch = Font.getDefaultFont().getHeight();
        if (h <= Display.H / 2) return 0;
        return labelHeight(w) + Ui.PAD + ed.caretY() - ch;  // a tall field: the caret's line
    }

    int focusHeight(int w) {
        int h = height(w);
        if (h <= Display.H / 2) return h;
        return 3 * Font.getDefaultFont().getHeight();
    }
}
