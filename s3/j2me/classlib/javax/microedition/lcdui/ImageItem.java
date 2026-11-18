package javax.microedition.lcdui;

import java.util.Vector;

/** An image (or its alternative text when there is none), placed left, centred or right by its layout. */
public class ImageItem extends Item {
    private Image img;
    private String alt;
    private final int appearance;
    private Vector altLines;
    private int altW = -1;

    public ImageItem(String label, Image img, int layout, String altText) {
        this(label, img, layout, altText, PLAIN);
    }

    public ImageItem(String label, Image image, int layout, String altText, int appearanceMode) {
        super(label);
        if (appearanceMode < PLAIN || appearanceMode > BUTTON) throw new IllegalArgumentException();
        setLayout(layout);
        img = snapshot(image);
        alt = altText;
        appearance = appearanceMode;
    }

    static Image snapshot(Image i) { return i != null && i.isMutable() ? Image.createImage(i) : i; }

    public Image getImage() { return img; }

    public void setImage(Image img) {
        this.img = snapshot(img);
        changed();
    }

    public String getAltText() { return alt; }

    public void setAltText(String text) {
        alt = text;
        altLines = null;
        changed();
    }

    public int getAppearanceMode() { return appearance; }

    // ---- drawing
    private int pad() { return appearance == BUTTON || focusable() ? 6 : 0; }

    private Vector altLines(int w) {
        if (altLines == null || altW != w) {
            altLines = Screen.wrap(alt, Font.getDefaultFont(), w - 2 * pad());
            altW = w;
        }
        return altLines;
    }

    int contentHeight(int w) {
        int h = img != null ? img.getHeight() : Ui.height(altLines(w), Font.getDefaultFont());
        return h == 0 ? 0 : h + 2 * pad();
    }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {
        if (h <= 0) return;
        int p = pad();
        if (p > 0) Ui.card(g, x, y, w, h, focused);
        if (img == null) {
            Ui.drawLines(g, altLines(w), Font.getDefaultFont(), x + p, y + p, Screen.MUTED);
            return;
        }
        int iw = img.getWidth(), room = w - 2 * p, ix = x + p;
        int hl = layout & LAYOUT_CENTER;
        if (iw < room) {
            if (hl == LAYOUT_CENTER) ix += (room - iw) / 2;
            else if (hl == LAYOUT_RIGHT) ix += room - iw;
        }
        int cx = g.getClipX(), cy = g.getClipY(), cw = g.getClipWidth(), ch = g.getClipHeight();
        g.clipRect(x + p, y + p, room, h - 2 * p);  // wider than the screen: cut at the item's edges
        g.drawImage(img, ix, y + p, Graphics.LEFT | Graphics.TOP);
        g.setClip(cx, cy, cw, ch);
    }
}
