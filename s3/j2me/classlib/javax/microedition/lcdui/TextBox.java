package javax.microedition.lcdui;

/**
 * A full-screen text editor: the title, a card with the text and its caret, under it the input mode and the count
 * against maxSize. Keys as TextField (TextEditor); up / down move the caret a line; a tap places it.
 */
public class TextBox extends Screen {
    final TextEditor ed;
    private String inputMode;
    private int scroll, top = -1, total;

    public TextBox(String title, String text, int maxSize, int constraints) {
        this.title = title;
        ed = new TextEditor(text, maxSize, constraints);
        ed.screen = this;
    }

    public String getString() { return ed.get(); }

    public void setString(String text) {
        ed.set(text);
        invalidate();
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
        invalidate();
    }

    public void insert(char[] data, int offset, int length, int position) {
        if (data == null) throw new NullPointerException();
        if (offset < 0 || length < 0 || offset + length > data.length) throw new ArrayIndexOutOfBoundsException();
        insert(new String(data, offset, length), position);
    }

    public void delete(int offset, int length) {
        ed.delete(offset, length);
        invalidate();
    }

    public int getMaxSize() { return ed.max; }

    public int setMaxSize(int maxSize) {
        int m = ed.setMax(maxSize);
        invalidate();
        return m;
    }

    public int size() { return ed.size(); }

    public int getCaretPosition() { return ed.caret; }

    public void setConstraints(int constraints) {
        ed.setConstraints(constraints);
        invalidate();
    }

    public int getConstraints() { return ed.constraints; }

    public void setInitialInputMode(String characterSubset) { inputMode = characterSubset; }

    // ---- drawing and keys
    private int textW() { return Display.W - 2 * Ui.M - 2 * Ui.PAD; }

    void paintScreen(Graphics g, int w, int h) {
        top = paintTitle(g, title, w);
        Font f = Font.getDefaultFont(), sf = Ui.small();
        int vh = h - top, fh = f.getHeight();
        int textH = Math.max(ed.height(f, textW()), 3 * fh);
        int cardH = textH + 2 * Ui.PAD;
        total = 4 + cardH + 4 + sf.getHeight() + 6;
        // the caret's line in view
        int cy = 4 + Ui.PAD + ed.caretY();
        if (cy - 4 < scroll) scroll = cy - 4;
        else if (cy + fh + 4 > scroll + vh) scroll = cy + fh + 4 - vh;
        scroll = Ui.clampScroll(scroll, total, vh);
        g.setClip(0, top, w, vh);
        int y = top + 4 - scroll;
        Ui.card(g, Ui.M, y, w - 2 * Ui.M, cardH, true);
        ed.paint(g, f, Ui.M + Ui.PAD, y + Ui.PAD, textW(), ed.editable());
        g.setFont(sf);
        g.setColor(Screen.MUTED);
        int sy = y + cardH + 4;
        String tag = ed.modeName();
        if (tag != null) g.drawString(tag, Ui.M + 2, sy, Graphics.LEFT | Graphics.TOP);
        g.drawString(ed.size() + " / " + ed.max, w - Ui.M - 2, sy, Graphics.RIGHT | Graphics.TOP);
        g.setClip(0, 0, w, h);
        Ui.scrollbar(g, top, vh, total, scroll);
    }

    boolean keyScreen(int type, int code) {
        if (ed.key(type, code)) return true;
        if (type == 2) return false;
        if (code == Canvas.K_UP || code == Canvas.K_DOWN) {
            ed.lineMove(code == Canvas.K_UP ? -1 : 1);
            return true;
        }
        return false;
    }

    void pointerScreen(int type, int x, int y) {
        if (type != 4) return;
        int t = top >= 0 ? top : Ui.contentTop(title);
        ed.touch(x - Ui.M - Ui.PAD, y - (t + 4 - scroll) - Ui.PAD);
        invalidate();
    }
}
