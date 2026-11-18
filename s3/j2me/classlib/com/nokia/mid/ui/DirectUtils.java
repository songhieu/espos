package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.LcduiBridge;

public class DirectUtils {
    private static final Object LOCK = new Object();
    private static Graphics lastG;
    private static DirectGraphics lastD;

    /** Games ask for it every frame: the same Graphics gets the same DirectGraphics (it keeps the alpha). */
    public static DirectGraphics getDirectGraphics(Graphics g) {
        if (g == null) throw new NullPointerException();
        synchronized (LOCK) {
            if (g != lastG) {
                lastD = new DirectGraphicsImpl(g);
                lastG = g;
            }
            return lastD;
        }
    }

    public static Image createImage(byte[] imageData, int imageOffset, int imageLength) {
        return Image.createImage(imageData, imageOffset, imageLength);
    }

    /** A mutable image filled with ARGBcolor (0 is fully transparent). */
    public static Image createImage(int width, int height, int ARGBcolor) {
        return LcduiBridge.createMutable(width, height, ARGBcolor);
    }
}
