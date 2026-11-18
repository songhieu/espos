package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;

/** Something drawn at a position: a Sprite or a TiledLayer. */
public abstract class Layer {
    int x, y, width, height;
    boolean visible = true;

    Layer(int width, int height) {
        size(width, height);
    }

    void size(int w, int h) {
        if (w < 0 || h < 0) throw new IllegalArgumentException();
        width = w;
        height = h;
    }

    public void setPosition(int x, int y) {
        this.x = x;
        this.y = y;
    }

    public void move(int dx, int dy) {
        x += dx;
        y += dy;
    }

    public final int getX() { return x; }
    public final int getY() { return y; }
    public final int getWidth() { return width; }
    public final int getHeight() { return height; }

    public void setVisible(boolean visible) { this.visible = visible; }

    public final boolean isVisible() { return visible; }

    public abstract void paint(Graphics g);
}
