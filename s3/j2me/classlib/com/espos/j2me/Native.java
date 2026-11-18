package com.espos.j2me;

/**
 * The MIDP layer's natives (src/j2me/midp.cpp). Every drawing call takes the target pixels (ARGB, one int a pixel,
 * row after row), their size, and the clip already intersected with the target and translated (cx, cy, cw, ch in
 * target pixels). Colours are 0xRRGGBB (drawn opaque) unless a call says otherwise.
 */
public final class Native {
    private Native() {}

    // ---- drawing
    public static native void fill(int[] d, int dw, int dh, int x, int y, int w, int h, int rgb,
                                   int cx, int cy, int cw, int ch);
    public static native void line(int[] d, int dw, int dh, int x1, int y1, int x2, int y2, int rgb, boolean dotted,
                                   int cx, int cy, int cw, int ch);
    /** An arc or a pie (fill) in the box x, y, w, h: from startAngle degrees (3 o'clock, counter-clockwise) for arcAngle. */
    public static native void arc(int[] d, int dw, int dh, int x, int y, int w, int h, int startAngle, int arcAngle,
                                  int rgb, boolean fill, int cx, int cy, int cw, int ch);
    public static native void triangle(int[] d, int dw, int dh, int x1, int y1, int x2, int y2, int x3, int y3,
                                       int rgb, int cx, int cy, int cw, int ch);
    public static native void roundRect(int[] d, int dw, int dh, int x, int y, int w, int h, int aw, int ah,
                                        int rgb, boolean fill, int cx, int cy, int cw, int ch);
    /**
     * The region sx, sy, w, h of s (sw x sh) to d with a Sprite transform (0..7), its top-left corner at dx, dy
     * after the transform. alpha: s has transparent pixels (blended by their alpha byte); else drawn opaque.
     */
    public static native void blit(int[] d, int dw, int dh, int[] s, int sw, int sh, int sx, int sy, int w, int h,
                                   int transform, int dx, int dy, boolean alpha, int cx, int cy, int cw, int ch);
    /** Graphics.drawRGB: rgbData from offset, scanlength a row (may be negative), w x h at x, y. */
    public static native void rgb(int[] d, int dw, int dh, int[] src, int offset, int scanlength, int x, int y, int w,
                                  int h, boolean alpha, int cx, int cy, int cw, int ch);
    /** Characters off..off+len of s in a font (Font.id()), the text box's top-left at x, y. Returns its width. */
    public static native int text(int[] d, int dw, int dh, String s, int off, int len, int font, int x, int y, int rgb,
                                  int cx, int cy, int cw, int ch);

    // ---- fonts (id: Font.id(): size 0 small, 1 medium, 2 large; + 3 when bold)
    public static native int textWidth(String s, int off, int len, int font);
    public static native int fontHeight(int font);
    public static native int fontBaseline(int font);

    // ---- images
    /** A PNG (or JPEG) file: {w, h, pixels...} ARGB, or null when it cannot be read. */
    public static native int[] decodeImage(byte[] b, int off, int len);

    // ---- the screen and events
    public static native int screenWidth();
    public static native int screenHeight();
    /** The screen's pixels (screenWidth x screenHeight) changed in x, y, w, h: show them. */
    public static native void flush(int[] screen, int x, int y, int w, int h);
    /**
     * The next event, 0 when none: 1 key pressed, 2 key released, 3 key repeated (ev[0] = key code),
     * 4 pointer pressed, 5 released, 6 dragged (ev[0], ev[1] = x, y on the screen), 7 the game is paused (shown
     * again: 8), 9 the user asked to leave.
     */
    public static native int poll(int[] ev);
    /** GameCanvas key bits held now or pressed since the last call (as getKeyStates). */
    public static native int keyStates();
    /** The command labels shown on the left and right soft keys ("" for none). */
    public static native void softLabels(String left, String right);
    public static native void vibrate(int ms);
    /**
     * The game's own Canvas says its screen is w x h (a port for another phone that ignores the real size): the device
     * may open the game again at that size (remembered for this game).
     */
    public static native void screenHint(int w, int h);
    /** A tone (MIDI note 0..127) for ms at volume 0..100, or silence (note < 0). */
    public static native void tone(int note, int ms, int volume);

    // ---- the MIDlet
    /** The JAD/manifest attribute, or null. */
    public static native String appProperty(String key);
    /** notifyDestroyed: the device closes the game. */
    public static native void exit();

    // ---- record stores (one file a store, the game's own folder)
    public static native byte[] rmsLoad(String name);
    public static native boolean rmsSave(String name, byte[] data);
    public static native boolean rmsDelete(String name);
    public static native String[] rmsList();
}
