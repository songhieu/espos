package javax.microedition.lcdui;

/**
 * An item the app draws: its content area (preferred size, at most the Form's width) is translated and clipped for
 * paint. Keys reach keyPressed / keyReleased / keyRepeated while it has the focus, except up / down, which go
 * through traverse first (as MIDP's forms do: traverse returning false moves the focus on, then traverseOut).
 */
public abstract class CustomItem extends Item {
    protected static final int NONE = 0, TRAVERSE_HORIZONTAL = 1, TRAVERSE_VERTICAL = 2, KEY_PRESS = 4,
            KEY_RELEASE = 8, KEY_REPEAT = 0x10, POINTER_PRESS = 0x20, POINTER_RELEASE = 0x40, POINTER_DRAG = 0x80;

    private final int[] vis = new int[4];
    private boolean inside;   // traverse said it keeps the focus
    private int lastW = -1, lastH = -1;

    protected CustomItem(String label) { super(label); }

    public int getGameAction(int keyCode) {
        int a = Canvas.action(keyCode);
        if (a == 0 && (keyCode < Canvas.KEY_NUM0 || keyCode > Canvas.KEY_NUM9) && keyCode != Canvas.KEY_STAR
                && keyCode != Canvas.KEY_POUND && keyCode >= 0)
            throw new IllegalArgumentException();
        return a;
    }

    protected final int getInteractionModes() {
        return TRAVERSE_HORIZONTAL | TRAVERSE_VERTICAL | KEY_PRESS | KEY_RELEASE | KEY_REPEAT | POINTER_PRESS
                | POINTER_RELEASE | POINTER_DRAG;
    }

    protected abstract int getMinContentWidth();
    protected abstract int getMinContentHeight();
    protected abstract int getPrefContentWidth(int height);
    protected abstract int getPrefContentHeight(int width);
    protected abstract void paint(Graphics g, int w, int h);

    protected void sizeChanged(int w, int h) {}
    protected void keyPressed(int keyCode) {}
    protected void keyReleased(int keyCode) {}
    protected void keyRepeated(int keyCode) {}
    protected void pointerPressed(int x, int y) {}
    protected void pointerReleased(int x, int y) {}
    protected void pointerDragged(int x, int y) {}
    protected void showNotify() {}
    protected void hideNotify() {}

    protected boolean traverse(int dir, int viewportWidth, int viewportHeight, int[] visRect_inout) { return false; }

    protected void traverseOut() {}

    protected final void repaint() { changed(); }

    protected final void repaint(int x, int y, int w, int h) { changed(); }

    protected final void invalidate() { changed(); }

    // ---- on the Form
    private int contentW(int w) {
        int pw = prefW >= 0 ? prefW : getPrefContentWidth(-1);
        int mw = getMinContentWidth();
        if (pw < mw) pw = mw;
        return Math.max(0, Math.min(w, pw));
    }

    private int contentH(int w) {
        int ch = prefH >= 0 ? prefH : getPrefContentHeight(contentW(w));
        int mh = getMinContentHeight();
        return Math.max(0, Math.max(ch, mh));
    }

    boolean focusable() { return true; }

    int contentHeight(int w) { return contentH(w); }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        int cw = contentW(w), ch = contentH(w);
        if (cw != lastW || ch != lastH) {
            lastW = cw;
            lastH = ch;
            sizeChanged(cw, ch);
        }
        int hl = layout & LAYOUT_CENTER;
        int cx = x + (hl == LAYOUT_CENTER ? (w - cw) / 2 : hl == LAYOUT_RIGHT ? w - cw : 0);
        if (focused) {
            g.setColor(Screen.ACCENT);
            g.drawRoundRect(cx - 2, y - 2, cw + 3, ch + 3, 6, 6);
        }
        int ox = g.getClipX(), oy = g.getClipY(), ow = g.getClipWidth(), oh = g.getClipHeight();
        int tx = g.getTranslateX(), ty = g.getTranslateY();
        g.translate(cx, y);
        g.clipRect(0, 0, cw, ch);
        g.setColor(0);
        g.setFont(Font.getDefaultFont());
        g.setStrokeStyle(Graphics.SOLID);
        try {
            paint(g, cw, ch);
        } finally {
            g.translate(tx - g.getTranslateX(), ty - g.getTranslateY());  // whatever paint did
            g.setClip(ox, oy, ow, oh);
        }
    }

    /** Where the content starts from the item's left, for touches. */
    private int contentX(int w) {
        int cw = contentW(w), hl = layout & LAYOUT_CENTER;
        return hl == LAYOUT_CENTER ? (w - cw) / 2 : hl == LAYOUT_RIGHT ? w - cw : 0;
    }

    boolean keyItem(int type, int code) {
        int a = Canvas.action(code);
        if (code == Canvas.K_UP || code == Canvas.K_DOWN) {
            if (type == 2) return true;
            int w = fullWidth();
            vis[0] = 0;
            vis[1] = 0;
            vis[2] = contentW(w);
            vis[3] = contentH(w);
            boolean keep = traverse(a, w, Display.H, vis);
            if (keep) {
                inside = true;
                changed();
                return true;
            }
            if (inside) {
                inside = false;
                traverseOut();
            }
            return false;
        }
        if (type == 1) keyPressed(code);
        else if (type == 2) keyReleased(code);
        else keyRepeated(code);
        return true;
    }

    void focusIn(int dir) {
        int w = fullWidth();
        vis[0] = 0;
        vis[1] = 0;
        vis[2] = contentW(w);
        vis[3] = contentH(w);
        inside = traverse(dir > 0 ? Canvas.DOWN : dir < 0 ? Canvas.UP : NONE, w, Display.H, vis);
    }

    void focusOut() {
        inside = false;
        traverseOut();
    }

    void visible(boolean on) {
        if (on) showNotify();
        else hideNotify();
    }

    boolean takesDrag() { return true; }

    void pointerItem(int type, int x, int y, int w) {
        int px = x - contentX(w), py = y - labelHeight(w);
        if (type == 4) pointerPressed(px, py);
        else if (type == 5) pointerReleased(px, py);
        else pointerDragged(px, py);
    }
}
