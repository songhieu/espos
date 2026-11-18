package javax.microedition.lcdui;

/** For javax.microedition.lcdui.game only (not a MIDP class): what it needs from inside this package. */
public final class LcduiBridge {
    private LcduiBridge() {}

    /** GameCanvas.flushGraphics: the buffer's region to the screen now. */
    public static void flush(Canvas c, Image buffer, int x, int y, int w, int h) {
        Display d = Display.instance;
        if (d != null) d.flushBuffer(c, buffer.px, x, y, w, h);
    }

    /** GameCanvas(true): keyPressed and keyReleased are not called (soft keys still go to commands or -6/-7). */
    public static void suppressKeys(Canvas c, boolean on) { c.suppressKeys = on; }

    /** The pixels (ARGB) of an image, for pixel-level collisions. */
    public static int[] pixels(Image img) { return img.px; }

    public static boolean hasAlpha(Image img) { return img.alpha; }

    // ---- com.nokia.mid.ui (DirectGraphics, DirectUtils)

    /** The pixels (ARGB) a Graphics draws into: an image's or the screen's. */
    public static int[] targetPixels(Graphics g) { return g.buf; }

    public static int targetWidth(Graphics g) { return g.bw; }

    public static int targetHeight(Graphics g) { return g.bh; }

    /** DirectUtils.createImage(w, h, argb): a mutable image filled with argb (transparent pixels drawn blended). */
    public static Image createMutable(int w, int h, int argb) {
        if (w <= 0 || h <= 0) throw new IllegalArgumentException();
        int[] px = new int[w * h];
        for (int i = 0; i < px.length; i++) px[i] = argb;
        return new Image(px, w, h, true, (argb >>> 24) != 0xFF);
    }
}
