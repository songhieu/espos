package com.espos.j2me;

import java.util.Hashtable;
import java.util.Vector;

/**
 * HOST TESTS ONLY (j2me/test/midp/run-host.sh): a pure-Java stand-in for the device's MIDP natives, so the library's
 * logic runs on a PC's JDK. It is never built for the device (build-midp.sh leaves test/midp/host out).
 * Drawing really writes the int[] targets (fills, lines, blits with the 8 transforms, drawRGB); text draws nothing but
 * measures 6 px a character; fonts are 12 / 14 / 16 px high. Soft key labels, tones and vibration are recorded for
 * the tests to check; record stores live in a table; events come from a queue the tests fill.
 */
public final class Native {
    private Native() {}

    public static String softLeft = "", softRight = "";
    public static int tones, lastNote = -100, vibrations, flushes;
    public static boolean exited;
    public static final Hashtable props = new Hashtable(), stores = new Hashtable();
    public static final Vector events = new Vector();  // of int[] {type, a, b}

    private static void put(int[] d, int dw, int dh, int x, int y, int c, int cx, int cy, int cw, int ch) {
        if (x < cx || y < cy || x >= cx + cw || y >= cy + ch || x < 0 || y < 0 || x >= dw || y >= dh) return;
        d[y * dw + x] = c;
    }

    public static void fill(int[] d, int dw, int dh, int x, int y, int w, int h, int rgb, int cx, int cy, int cw, int ch) {
        int x1 = Math.max(x, cx), y1 = Math.max(y, cy), x2 = Math.min(x + w, cx + cw), y2 = Math.min(y + h, cy + ch);
        for (int r = y1; r < y2; r++) for (int c = x1; c < x2; c++) put(d, dw, dh, c, r, 0xFF000000 | rgb, cx, cy, cw, ch);
    }

    public static void line(int[] d, int dw, int dh, int x1, int y1, int x2, int y2, int rgb, boolean dotted,
                            int cx, int cy, int cw, int ch) {
        int dx = Math.abs(x2 - x1), dy = -Math.abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1, e = dx + dy;
        for (int i = 0; ; i++) {
            if (!dotted || (i & 2) == 0) put(d, dw, dh, x1, y1, 0xFF000000 | rgb, cx, cy, cw, ch);
            if (x1 == x2 && y1 == y2) break;
            int e2 = 2 * e;
            if (e2 >= dy) { e += dy; x1 += sx; }
            if (e2 <= dx) { e += dx; y1 += sy; }
        }
    }

    public static void arc(int[] d, int dw, int dh, int x, int y, int w, int h, int startAngle, int arcAngle, int rgb,
                           boolean fill, int cx, int cy, int cw, int ch) {
        // a whole ellipse (the angles are left out): filled, or its edge one pixel wide
        double rx = w / 2.0, ry = h / 2.0, mx = x + rx, my = y + ry;
        for (int py = y; py <= y + h; py++)
            for (int px = x; px <= x + w; px++) {
                double ex = (px + 0.5 - mx) / rx, ey = (py + 0.5 - my) / ry, e = ex * ex + ey * ey;
                double ix = (px + 0.5 - mx) / Math.max(0.5, rx - 1), iy = (py + 0.5 - my) / Math.max(0.5, ry - 1);
                if (e <= 1 && (fill || ix * ix + iy * iy > 1)) put(d, dw, dh, px, py, 0xFF000000 | rgb, cx, cy, cw, ch);
            }
    }

    public static void triangle(int[] d, int dw, int dh, int x1, int y1, int x2, int y2, int x3, int y3, int rgb,
                                int cx, int cy, int cw, int ch) {
        int minX = Math.min(x1, Math.min(x2, x3)), maxX = Math.max(x1, Math.max(x2, x3));
        int minY = Math.min(y1, Math.min(y2, y3)), maxY = Math.max(y1, Math.max(y2, y3));
        for (int y = minY; y <= maxY; y++)
            for (int x = minX; x <= maxX; x++) {
                int a = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1), b = (x3 - x2) * (y - y2) - (y3 - y2) * (x - x2),
                        c = (x1 - x3) * (y - y3) - (y1 - y3) * (x - x3);
                if ((a >= 0 && b >= 0 && c >= 0) || (a <= 0 && b <= 0 && c <= 0)) put(d, dw, dh, x, y, 0xFF000000 | rgb, cx, cy, cw, ch);
            }
    }

    public static void roundRect(int[] d, int dw, int dh, int x, int y, int w, int h, int aw, int ah, int rgb,
                                 boolean fill, int cx, int cy, int cw, int ch) {
        if (fill) {
            fill(d, dw, dh, x, y, w, h, rgb, cx, cy, cw, ch);
        } else {
            line(d, dw, dh, x, y, x + w, y, rgb, false, cx, cy, cw, ch);
            line(d, dw, dh, x, y + h, x + w, y + h, rgb, false, cx, cy, cw, ch);
            line(d, dw, dh, x, y, x, y + h, rgb, false, cx, cy, cw, ch);
            line(d, dw, dh, x + w, y, x + w, y + h, rgb, false, cx, cy, cw, ch);
        }
    }

    public static void blit(int[] d, int dw, int dh, int[] s, int sw, int sh, int sx, int sy, int w, int h,
                            int transform, int dx, int dy, boolean alpha, int cx, int cy, int cw, int ch) {
        boolean swap = (transform & 4) != 0;
        int tw = swap ? h : w, th = swap ? w : h;
        for (int ty = 0; ty < th; ty++) {
            for (int tx = 0; tx < tw; tx++) {
                int px, py;  // the source pixel shown at (tx, ty): the inverse of the Sprite transforms
                switch (transform) {
                    case 1: px = tx; py = h - 1 - ty; break;
                    case 2: px = w - 1 - tx; py = ty; break;
                    case 3: px = w - 1 - tx; py = h - 1 - ty; break;
                    case 4: px = ty; py = tx; break;
                    case 5: px = ty; py = h - 1 - tx; break;
                    case 6: px = w - 1 - ty; py = tx; break;
                    case 7: px = w - 1 - ty; py = h - 1 - tx; break;
                    default: px = tx; py = ty; break;
                }
                int c = s[(sy + py) * sw + sx + px];
                if (alpha && (c >>> 24) == 0) continue;
                put(d, dw, dh, dx + tx, dy + ty, c | (alpha ? 0 : 0xFF000000), cx, cy, cw, ch);
            }
        }
    }

    public static void rgb(int[] d, int dw, int dh, int[] src, int offset, int scanlength, int x, int y, int w, int h,
                           boolean alpha, int cx, int cy, int cw, int ch) {
        for (int r = 0; r < h; r++)
            for (int c = 0; c < w; c++) {
                int v = src[offset + r * scanlength + c];
                if (alpha && (v >>> 24) == 0) continue;
                put(d, dw, dh, x + c, y + r, v | 0xFF000000, cx, cy, cw, ch);
            }
    }

    /** A block for each character that is not a space (enough to see where text goes in a screenshot). */
    public static int text(int[] d, int dw, int dh, String s, int off, int len, int font, int x, int y, int rgb,
                           int cx, int cy, int cw, int ch) {
        int h = fontHeight(font);
        for (int i = 0; i < len; i++)
            if (s.charAt(off + i) != ' ') fill(d, dw, dh, x + i * 6 + 1, y + 3, 4, h - 6, rgb, cx, cy, cw, ch);
        return textWidth(s, off, len, font);
    }

    public static int textWidth(String s, int off, int len, int font) { return len * 6; }
    public static int fontHeight(int font) { return 12 + 2 * (font % 3); }
    public static int fontBaseline(int font) { return fontHeight(font) - 3; }

    public static int[] decodeImage(byte[] b, int off, int len) { return null; }

    public static int screenWidth() { return 240; }
    public static int screenHeight() { return 320; }

    public static void flush(int[] screen, int x, int y, int w, int h) { flushes++; }

    public static int poll(int[] ev) {
        if (events.isEmpty()) return 0;
        int[] e = (int[]) events.elementAt(0);
        events.removeElementAt(0);
        ev[0] = e[1];
        ev[1] = e[2];
        return e[0];
    }

    public static int keyStates() { return 0; }

    public static void softLabels(String left, String right) {
        softLeft = left;
        softRight = right;
    }

    public static void vibrate(int ms) { vibrations++; }

    public static void tone(int note, int ms, int volume) {
        tones++;
        lastNote = note;
    }

    public static String appProperty(String key) { return (String) props.get(key); }

    public static void exit() { exited = true; }

    public static byte[] rmsLoad(String name) { return (byte[]) stores.get(name); }

    public static boolean rmsSave(String name, byte[] data) {
        stores.put(name, data);
        return true;
    }

    public static boolean rmsDelete(String name) { return stores.remove(name) != null; }

    public static String[] rmsList() {
        String[] r = new String[stores.size()];
        int i = 0;
        for (java.util.Enumeration e = stores.keys(); e.hasMoreElements(); ) r[i++] = (String) e.nextElement();
        return r;
    }
}
