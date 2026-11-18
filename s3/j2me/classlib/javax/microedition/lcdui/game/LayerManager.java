package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;

/** Layers in order (index 0 on top), painted through a view window. */
public class LayerManager {
    private static final int HUGE = 1 << 24;  // the default window: big, but no overflow in clip maths

    private Layer[] layers = new Layer[4];
    private int count;
    private int vx, vy, vw = Integer.MAX_VALUE, vh = Integer.MAX_VALUE;

    public LayerManager() {}

    private int indexOf(Layer l) {
        for (int i = 0; i < count; i++) if (layers[i] == l) return i;
        return -1;
    }

    private void removeAt(int i) {
        System.arraycopy(layers, i + 1, layers, i, count - i - 1);
        layers[--count] = null;
    }

    private void addAt(Layer l, int i) {
        if (count == layers.length) {
            Layer[] a = new Layer[count * 2];
            System.arraycopy(layers, 0, a, 0, count);
            layers = a;
        }
        System.arraycopy(layers, i, layers, i + 1, count - i);
        layers[i] = l;
        count++;
    }

    public void append(Layer l) {
        if (l == null) throw new NullPointerException();
        int i = indexOf(l);
        if (i >= 0) removeAt(i);
        addAt(l, count);
    }

    public void insert(Layer l, int index) {
        if (l == null) throw new NullPointerException();
        int i = indexOf(l);
        if (index < 0 || index > count || (i >= 0 && index >= count)) throw new IndexOutOfBoundsException();
        if (i >= 0) removeAt(i);
        addAt(l, index);
    }

    public Layer getLayerAt(int index) {
        if (index < 0 || index >= count) throw new IndexOutOfBoundsException();
        return layers[index];
    }

    public int getSize() { return count; }

    public void remove(Layer l) {
        if (l == null) throw new NullPointerException();
        int i = indexOf(l);
        if (i >= 0) removeAt(i);
    }

    public void setViewWindow(int x, int y, int width, int height) {
        if (width < 0 || height < 0) throw new IllegalArgumentException();
        vx = x;
        vy = y;
        vw = width;
        vh = height;
    }

    /** The view window's top-left at (x, y) of g; the layers from the last (bottom) to the first (top). */
    public void paint(Graphics g, int x, int y) {
        if (g == null) throw new NullPointerException();
        int cx = g.getClipX(), cy = g.getClipY(), cw = g.getClipWidth(), ch = g.getClipHeight();
        int dx = x - vx, dy = y - vy;
        g.translate(dx, dy);
        g.clipRect(vx, vy, Math.min(vw, HUGE), Math.min(vh, HUGE));
        try {
            if (g.getClipWidth() > 0 && g.getClipHeight() > 0)
                for (int i = count - 1; i >= 0; i--) if (layers[i].visible) layers[i].paint(g);
        } finally {
            g.translate(-dx, -dy);
            g.setClip(cx, cy, cw, ch);
        }
    }
}
