package com.nokia.mid.ui;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.LcduiBridge;

/**
 * DirectGraphics on top of a Graphics. Opaque shapes use the Graphics calls; translucent ones (alpha 1..254) are
 * drawn as spans of ARGB pixels with drawRGB (blended); alpha 0 draws nothing.
 *
 * Manipulations: Nokia flips first, then rotates counter-clockwise (ROTATE_90 = 90 degrees CCW). As a Sprite
 * transform (MIRROR = flip about the vertical axis, ROTn = n degrees clockwise; CCW r = CW 360-r; a vertical flip
 * = MIRROR then ROT180; both flips = ROT180):
 *              CCW 0          CCW 90           CCW 180          CCW 270
 *   no flip    NONE           ROT270           ROT180           ROT90
 *   H          MIRROR         MIRROR_ROT270    MIRROR_ROT180    MIRROR_ROT90
 *   V          MIRROR_ROT180  MIRROR_ROT90     MIRROR           MIRROR_ROT270
 *   H + V      ROT180         ROT90            NONE             ROT270
 */
final class DirectGraphicsImpl implements DirectGraphics {
    // Sprite transforms: NONE 0, MIRROR_ROT180 1, MIRROR 2, ROT180 3, MIRROR_ROT270 4, ROT90 5, ROT270 6, MIRROR_ROT90 7
    private static final int[][] TRANSFORM = {
            {0, 6, 3, 5},  // no flip
            {2, 4, 1, 7},  // FLIP_HORIZONTAL
            {1, 7, 2, 4},  // FLIP_VERTICAL
            {3, 5, 0, 6},  // both
    };
    private static final int TOP_LEFT = Graphics.TOP | Graphics.LEFT;

    private final Graphics g;
    private int alpha = 0xFF, argbRgb = -1;  // setARGBColor's alpha, and the colour it set
    private int[] span = new int[0];
    private int spanColor;

    DirectGraphicsImpl(Graphics g) { this.g = g; }

    /** The Sprite transform for a Nokia manipulation. */
    static int transform(int manipulation) {
        int rot = manipulation & ~(FLIP_HORIZONTAL | FLIP_VERTICAL), r;
        if (rot == 0) r = 0;
        else if (rot == ROTATE_90) r = 1;
        else if (rot == ROTATE_180) r = 2;
        else if (rot == ROTATE_270) r = 3;
        else throw new IllegalArgumentException("manipulation " + manipulation);
        int f = ((manipulation & FLIP_HORIZONTAL) != 0 ? 1 : 0) + ((manipulation & FLIP_VERTICAL) != 0 ? 2 : 0);
        return TRANSFORM[f][r];
    }

    // ---- colour

    public void setARGBColor(int argbColor) {
        alpha = argbColor >>> 24;
        argbRgb = argbColor & 0xFFFFFF;
        g.setColor(argbRgb);
    }

    /** setARGBColor's alpha while its colour is still the Graphics' colour; else opaque (setColor sets alpha 255). */
    public int getAlphaComponent() { return g.getColor() == argbRgb ? alpha : 0xFF; }

    public int getNativePixelFormat() { return TYPE_INT_8888_ARGB; }

    // ---- images and pixels

    public void drawImage(Image img, int x, int y, int anchor, int manipulation) {
        if (img == null) throw new NullPointerException();
        g.drawRegion(img, 0, 0, img.getWidth(), img.getHeight(), transform(manipulation), x, y,
                anchor == 0 ? TOP_LEFT : anchor);
    }

    public void drawPixels(int[] pixels, boolean transparency, int offset, int scanlength, int x, int y, int width,
                           int height, int manipulation, int format) {
        if (pixels == null) throw new NullPointerException();
        if (format != TYPE_INT_888_RGB && format != TYPE_INT_8888_ARGB) throw new IllegalArgumentException();
        if (width <= 0 || height <= 0) return;
        boolean a = transparency && format == TYPE_INT_8888_ARGB;
        int t = transform(manipulation);
        if (t == 0) {
            g.drawRGB(pixels, offset, scanlength, x, y, width, height, a);
            return;
        }
        int[] p = new int[width * height];
        for (int r = 0; r < height; r++) {
            int s = offset + r * scanlength, d = r * width;
            for (int c = 0; c < width; c++) p[d + c] = a ? pixels[s + c] : pixels[s + c] | 0xFF000000;
        }
        put(p, width, height, x, y, t, a);
    }

    public void drawPixels(short[] pixels, boolean transparency, int offset, int scanlength, int x, int y, int width,
                           int height, int manipulation, int format) {
        if (pixels == null) throw new NullPointerException();
        if (width <= 0 || height <= 0) return;
        int t = transform(manipulation);
        boolean a = transparency && (format == TYPE_USHORT_4444_ARGB || format == TYPE_USHORT_1555_ARGB);
        int[] p = new int[width * height];
        for (int r = 0; r < height; r++) {
            int s = offset + r * scanlength, d = r * width;
            for (int c = 0; c < width; c++) {
                int v = fromShort(pixels[s + c] & 0xFFFF, format);
                p[d + c] = a ? v : v | 0xFF000000;
            }
        }
        put(p, width, height, x, y, t, a);
    }

    public void drawPixels(byte[] pixels, byte[] transparencyMask, int offset, int scanlength, int x, int y,
                           int width, int height, int manipulation, int format) {
        if (pixels == null) throw new NullPointerException();
        if (width <= 0 || height <= 0) return;
        int t = transform(manipulation);
        int[] p = new int[width * height];
        for (int r = 0; r < height; r++) {
            for (int c = 0; c < width; c++) {
                int v = fromBytes(pixels, offset, scanlength, c, r, format);
                if (transparencyMask != null && bit(transparencyMask, offset, scanlength, c, r, format) == 0) v = 0;
                p[r * width + c] = v;
            }
        }
        put(p, width, height, x, y, t, transparencyMask != null);
    }

    /** ARGB pixels (w x h) at x, y after the transform. */
    private void put(int[] p, int w, int h, int x, int y, int t, boolean a) {
        if (t == 0) g.drawRGB(p, 0, w, x, y, w, h, a);
        else g.drawRegion(Image.createRGBImage(p, w, h, a), 0, 0, w, h, t, x, y, TOP_LEFT);
    }

    public void getPixels(int[] pixels, int offset, int scanlength, int x, int y, int width, int height, int format) {
        if (pixels == null) throw new NullPointerException();
        if (format != TYPE_INT_888_RGB && format != TYPE_INT_8888_ARGB) throw new IllegalArgumentException();
        int[] src = LcduiBridge.targetPixels(g);
        int tw = area(x, y, width, height);
        int ax = x + g.getTranslateX(), ay = y + g.getTranslateY();
        for (int r = 0; r < height; r++) {
            int s = (ay + r) * tw + ax, d = offset + r * scanlength;
            for (int c = 0; c < width; c++) {
                int v = src[s + c];
                pixels[d + c] = format == TYPE_INT_888_RGB ? v & 0xFFFFFF : v;
            }
        }
    }

    public void getPixels(short[] pixels, int offset, int scanlength, int x, int y, int width, int height,
                          int format) {
        if (pixels == null) throw new NullPointerException();
        int[] src = LcduiBridge.targetPixels(g);
        int tw = area(x, y, width, height);
        int ax = x + g.getTranslateX(), ay = y + g.getTranslateY();
        for (int r = 0; r < height; r++) {
            int s = (ay + r) * tw + ax, d = offset + r * scanlength;
            for (int c = 0; c < width; c++) pixels[d + c] = (short) toShort(src[s + c], format);
        }
    }

    public void getPixels(byte[] pixels, byte[] transparencyMask, int offset, int scanlength, int x, int y, int width,
                          int height, int format) {
        if (pixels == null) throw new NullPointerException();
        int[] src = LcduiBridge.targetPixels(g);
        int tw = area(x, y, width, height);
        int ax = x + g.getTranslateX(), ay = y + g.getTranslateY();
        for (int r = 0; r < height; r++) {
            for (int c = 0; c < width; c++) {
                int v = src[(ay + r) * tw + ax + c];
                toBytes(pixels, offset, scanlength, c, r, format, v);
                if (transparencyMask != null)
                    setBit(transparencyMask, offset, scanlength, c, r, format, (v >>> 24) != 0 ? 1 : 0);
            }
        }
    }

    /** Checks x, y, w, h (translated) lies on the target; returns the target's width. */
    private int area(int x, int y, int w, int h) {
        int tw = LcduiBridge.targetWidth(g), th = LcduiBridge.targetHeight(g);
        int ax = x + g.getTranslateX(), ay = y + g.getTranslateY();
        if (w < 0 || h < 0 || ax < 0 || ay < 0 || ax + w > tw || ay + h > th)
            throw new IllegalArgumentException("area outside the target");
        return tw;
    }

    // ---- pixel formats

    private static int c4(int v) { return v * 17; }
    private static int c5(int v) { return (v << 3) | (v >> 2); }
    private static int c6(int v) { return (v << 2) | (v >> 4); }

    static int fromShort(int p, int format) {
        switch (format) {
            case TYPE_USHORT_4444_ARGB:
                return (c4(p >> 12) << 24) | (c4((p >> 8) & 15) << 16) | (c4((p >> 4) & 15) << 8) | c4(p & 15);
            case TYPE_USHORT_444_RGB:
                return 0xFF000000 | (c4((p >> 8) & 15) << 16) | (c4((p >> 4) & 15) << 8) | c4(p & 15);
            case TYPE_USHORT_555_RGB:
                return 0xFF000000 | (c5((p >> 10) & 31) << 16) | (c5((p >> 5) & 31) << 8) | c5(p & 31);
            case TYPE_USHORT_1555_ARGB:
                return ((p & 0x8000) != 0 ? 0xFF000000 : 0) | (c5((p >> 10) & 31) << 16) | (c5((p >> 5) & 31) << 8)
                        | c5(p & 31);
            case TYPE_USHORT_565_RGB:
                return 0xFF000000 | (c5((p >> 11) & 31) << 16) | (c6((p >> 5) & 63) << 8) | c5(p & 31);
        }
        throw new IllegalArgumentException("format " + format);
    }

    static int toShort(int v, int format) {
        int a = v >>> 24, r = (v >> 16) & 255, gr = (v >> 8) & 255, b = v & 255;
        switch (format) {
            case TYPE_USHORT_4444_ARGB: return ((a >> 4) << 12) | ((r >> 4) << 8) | ((gr >> 4) << 4) | (b >> 4);
            case TYPE_USHORT_444_RGB: return ((r >> 4) << 8) | ((gr >> 4) << 4) | (b >> 4);
            case TYPE_USHORT_555_RGB: return ((r >> 3) << 10) | ((gr >> 3) << 5) | (b >> 3);
            case TYPE_USHORT_1555_ARGB: return (a >= 128 ? 0x8000 : 0) | ((r >> 3) << 10) | ((gr >> 3) << 5) | (b >> 3);
            case TYPE_USHORT_565_RGB: return ((r >> 3) << 11) | ((gr >> 2) << 5) | (b >> 3);
        }
        throw new IllegalArgumentException("format " + format);
    }

    /**
     * Byte formats: positions in pixels (offset and scanlength too), packed most significant bits first, except
     * TYPE_BYTE_1_GRAY_VERTICAL: a byte holds 8 pixels of a column, the top one in the lowest bit, and scanlength
     * is in bytes between bands of 8 rows. 1 bit: 1 = black. Gray levels: 0 = black.
     */
    private static int fromBytes(byte[] px, int off, int scan, int c, int r, int format) {
        int i = off + r * scan + c;
        switch (format) {
            case TYPE_BYTE_1_GRAY:
                return ((px[i >> 3] >> (7 - (i & 7))) & 1) != 0 ? 0xFF000000 : 0xFFFFFFFF;
            case TYPE_BYTE_1_GRAY_VERTICAL:
                return ((px[off + c + (r >> 3) * scan] >> (r & 7)) & 1) != 0 ? 0xFF000000 : 0xFFFFFFFF;
            case TYPE_BYTE_2_GRAY: return gray(((px[i >> 2] >> ((3 - (i & 3)) * 2)) & 3) * 85);
            case TYPE_BYTE_4_GRAY: return gray(((px[i >> 1] >> ((1 - (i & 1)) * 4)) & 15) * 17);
            case TYPE_BYTE_8_GRAY: return gray(px[i] & 255);
            case TYPE_BYTE_332_RGB: {
                int v = px[i] & 255;
                return 0xFF000000 | ((((v >> 5) & 7) * 255 / 7) << 16) | ((((v >> 2) & 7) * 255 / 7) << 8)
                        | ((v & 3) * 85);
            }
        }
        throw new IllegalArgumentException("format " + format);
    }

    private static int gray(int l) { return 0xFF000000 | (l << 16) | (l << 8) | l; }

    private static int luma(int v) { return (((v >> 16) & 255) * 30 + ((v >> 8) & 255) * 59 + (v & 255) * 11) / 100; }

    private static void toBytes(byte[] px, int off, int scan, int c, int r, int format, int v) {
        int i = off + r * scan + c;
        switch (format) {
            case TYPE_BYTE_1_GRAY:
            case TYPE_BYTE_1_GRAY_VERTICAL:
                setBit(px, off, scan, c, r, format, luma(v) < 128 ? 1 : 0);
                return;
            case TYPE_BYTE_2_GRAY: {
                int s = (3 - (i & 3)) * 2;
                px[i >> 2] = (byte) ((px[i >> 2] & ~(3 << s)) | ((luma(v) >> 6) << s));
                return;
            }
            case TYPE_BYTE_4_GRAY: {
                int s = (1 - (i & 1)) * 4;
                px[i >> 1] = (byte) ((px[i >> 1] & ~(15 << s)) | ((luma(v) >> 4) << s));
                return;
            }
            case TYPE_BYTE_8_GRAY:
                px[i] = (byte) luma(v);
                return;
            case TYPE_BYTE_332_RGB:
                px[i] = (byte) ((((v >> 21) & 7) << 5) | (((v >> 13) & 7) << 2) | ((v >> 6) & 3));
                return;
        }
        throw new IllegalArgumentException("format " + format);
    }

    /** A transparency mask bit (1 = opaque), laid out as a 1 bit format (vertical when the pixels are). */
    private static int bit(byte[] m, int off, int scan, int c, int r, int format) {
        if (format == TYPE_BYTE_1_GRAY_VERTICAL) return (m[off + c + (r >> 3) * scan] >> (r & 7)) & 1;
        int i = off + r * scan + c;
        return (m[i >> 3] >> (7 - (i & 7))) & 1;
    }

    private static void setBit(byte[] m, int off, int scan, int c, int r, int format, int v) {
        int at, s;
        if (format == TYPE_BYTE_1_GRAY_VERTICAL) {
            at = off + c + (r >> 3) * scan;
            s = r & 7;
        } else {
            int i = off + r * scan + c;
            at = i >> 3;
            s = 7 - (i & 7);
        }
        m[at] = (byte) (v != 0 ? m[at] | (1 << s) : m[at] & ~(1 << s));
    }

    // ---- shapes

    public void drawTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argbColor) {
        drawPolygon(new int[] {x1, x2, x3}, 0, new int[] {y1, y2, y3}, 0, 3, argbColor);
    }

    public void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, int argbColor) {
        int a = argbColor >>> 24;
        if (a == 0) return;
        if (a == 0xFF) {
            int old = g.getColor();
            g.setColor(argbColor & 0xFFFFFF);
            g.fillTriangle(x1, y1, x2, y2, x3, y3);
            g.setColor(old);
            return;
        }
        fillPolygon(new int[] {x1, x2, x3}, 0, new int[] {y1, y2, y3}, 0, 3, argbColor);
    }

    public void drawPolygon(int[] xPoints, int xOffset, int[] yPoints, int yOffset, int nPoints, int argbColor) {
        if (xPoints == null || yPoints == null) throw new NullPointerException();
        if ((argbColor >>> 24) == 0 || nPoints <= 0) return;
        int old = g.getColor();
        g.setColor(argbColor & 0xFFFFFF);
        for (int i = 0; i < nPoints; i++) {
            int j = (i + 1) % nPoints;
            line(xPoints[xOffset + i], yPoints[yOffset + i], xPoints[xOffset + j], yPoints[yOffset + j], argbColor);
        }
        g.setColor(old);
    }

    /** A polygon's side; translucent sides leave out their end point (the next side's start): no corner blends twice. */
    private void line(int x1, int y1, int x2, int y2, int argb) {
        if ((argb >>> 24) == 0xFF) {
            g.drawLine(x1, y1, x2, y2);
            return;
        }
        int dx = Math.abs(x2 - x1), dy = -Math.abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
        int err = dx + dy, x = x1, y = y1;
        for (;;) {
            boolean end = x == x2 && y == y2;
            if (end && (x != x1 || y != y1)) break;
            span(x, y, 1, argb);
            if (end) break;
            int e2 = 2 * err;
            if (e2 >= dy) {
                err += dy;
                x += sx;
            }
            if (e2 <= dx) {
                err += dx;
                y += sy;
            }
        }
    }

    /** Even-odd scanline fill: a pixel is in when its centre is (so a rectangle fills as fillRect does). */
    public void fillPolygon(int[] xPoints, int xOffset, int[] yPoints, int yOffset, int nPoints, int argbColor) {
        if (xPoints == null || yPoints == null) throw new NullPointerException();
        if ((argbColor >>> 24) == 0 || nPoints < 3) return;
        int minY = Integer.MAX_VALUE, maxY = Integer.MIN_VALUE;
        for (int i = 0; i < nPoints; i++) {
            int y = yPoints[yOffset + i];
            if (y < minY) minY = y;
            if (y > maxY) maxY = y;
        }
        int cy = g.getClipY();
        minY = Math.max(minY, cy);
        maxY = Math.min(maxY, cy + g.getClipHeight());
        int old = g.getColor();
        g.setColor(argbColor & 0xFFFFFF);
        long[] xs = new long[nPoints];
        for (int y = minY; y < maxY; y++) {
            int k = 0;
            for (int i = 0; i < nPoints; i++) {
                int j = i + 1 == nPoints ? 0 : i + 1;
                int x1 = xPoints[xOffset + i], y1 = yPoints[yOffset + i];
                int x2 = xPoints[xOffset + j], y2 = yPoints[yOffset + j];
                if ((y1 <= y && y < y2) || (y2 <= y && y < y1)) {
                    // x where the edge crosses the row's centre y + 0.5, in 16.16 fixed point
                    long num = (long) (2 * (y - y1) + 1) * (x2 - x1);
                    long den = 2L * (y2 - y1);
                    xs[k++] = ((long) x1 << 16) + (num << 16) / den;
                }
            }
            for (int i = 1; i < k; i++) {
                long v = xs[i];
                int j = i - 1;
                while (j >= 0 && xs[j] > v) {
                    xs[j + 1] = xs[j];
                    j--;
                }
                xs[j + 1] = v;
            }
            for (int i = 0; i + 1 < k; i += 2) {
                int xa = (int) ((xs[i] - 0x8000 + 0xFFFF) >> 16), xb = (int) ((xs[i + 1] - 0x8000 + 0xFFFF) >> 16);
                span(xa, y, xb - xa, argbColor);
            }
        }
        g.setColor(old);
    }

    /** len pixels from x, y: fillRect when opaque (the colour is set), else blended ARGB. */
    private void span(int x, int y, int len, int argb) {
        if (len <= 0) return;
        if ((argb >>> 24) == 0xFF) {
            g.fillRect(x, y, len, 1);
            return;
        }
        if (span.length < len || spanColor != argb) {
            if (span.length < len) span = new int[Math.max(len, 64)];
            for (int i = 0; i < span.length; i++) span[i] = argb;
            spanColor = argb;
        }
        g.drawRGB(span, 0, len, x, y, len, 1, true);
    }
}
