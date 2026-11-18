package javax.microedition.lcdui;

import com.espos.j2me.Native;

/** Draws into an image's pixels or the screen's (both ARGB int[]); the pixel work is in Native. */
public class Graphics {
    public static final int HCENTER = 1, VCENTER = 2, LEFT = 4, RIGHT = 8, TOP = 16, BOTTOM = 32, BASELINE = 64;
    public static final int SOLID = 0, DOTTED = 1;

    final int[] buf;
    final int bw, bh;
    private int tx, ty;
    private int clipX, clipY, clipW, clipH;  // in translated coordinates' origin space (absolute pixels)
    private int color;
    private Font font = Font.getDefaultFont();
    private int stroke = SOLID;

    Graphics(int[] buf, int bw, int bh) {
        this.buf = buf;
        this.bw = bw;
        this.bh = bh;
        reset();
    }

    /** As a fresh Graphics: no translation, the whole target, black, the default font, solid. */
    void reset() {
        tx = ty = 0;
        clipX = clipY = 0;
        clipW = bw;
        clipH = bh;
        color = 0;
        font = Font.getDefaultFont();
        stroke = SOLID;
    }

    public void translate(int x, int y) {
        tx += x;
        ty += y;
    }

    public int getTranslateX() { return tx; }
    public int getTranslateY() { return ty; }

    public int getColor() { return color; }
    public int getRedComponent() { return (color >> 16) & 0xFF; }
    public int getGreenComponent() { return (color >> 8) & 0xFF; }
    public int getBlueComponent() { return color & 0xFF; }
    public int getGrayScale() { return (getRedComponent() * 30 + getGreenComponent() * 59 + getBlueComponent() * 11) / 100; }

    public void setColor(int red, int green, int blue) {
        if (red < 0 || red > 255 || green < 0 || green > 255 || blue < 0 || blue > 255)
            throw new IllegalArgumentException();
        color = (red << 16) | (green << 8) | blue;
    }

    public void setColor(int RGB) { color = RGB & 0xFFFFFF; }

    public void setGrayScale(int value) {
        if (value < 0 || value > 255) throw new IllegalArgumentException();
        color = (value << 16) | (value << 8) | value;
    }

    public Font getFont() { return font; }
    public void setFont(Font f) { font = f == null ? Font.getDefaultFont() : f; }

    public void setStrokeStyle(int style) {
        if (style != SOLID && style != DOTTED) throw new IllegalArgumentException();
        stroke = style;
    }

    public int getStrokeStyle() { return stroke; }

    public int getClipX() { return clipX - tx; }
    public int getClipY() { return clipY - ty; }
    public int getClipWidth() { return clipW; }
    public int getClipHeight() { return clipH; }

    public void clipRect(int x, int y, int width, int height) {
        int x1 = Math.max(clipX, x + tx), y1 = Math.max(clipY, y + ty);
        int x2 = Math.min(clipX + clipW, x + tx + width), y2 = Math.min(clipY + clipH, y + ty + height);
        clipX = x1;
        clipY = y1;
        clipW = Math.max(0, x2 - x1);
        clipH = Math.max(0, y2 - y1);
    }

    public void setClip(int x, int y, int width, int height) {
        int x1 = Math.max(0, x + tx), y1 = Math.max(0, y + ty);
        int x2 = Math.min(bw, x + tx + width), y2 = Math.min(bh, y + ty + height);
        clipX = x1;
        clipY = y1;
        clipW = Math.max(0, x2 - x1);
        clipH = Math.max(0, y2 - y1);
    }

    public void drawLine(int x1, int y1, int x2, int y2) {
        Native.line(buf, bw, bh, x1 + tx, y1 + ty, x2 + tx, y2 + ty, color, stroke == DOTTED, clipX, clipY, clipW, clipH);
    }

    public void fillRect(int x, int y, int width, int height) {
        if (width <= 0 || height <= 0) return;
        Native.fill(buf, bw, bh, x + tx, y + ty, width, height, color, clipX, clipY, clipW, clipH);
    }

    public void drawRect(int x, int y, int width, int height) {
        if (width < 0 || height < 0) return;
        if (width == 0 || height == 0) {
            drawLine(x, y, x + width, y + height);
            return;
        }
        drawLine(x, y, x + width - 1, y);
        drawLine(x + width, y, x + width, y + height - 1);
        drawLine(x + width, y + height, x + 1, y + height);
        drawLine(x, y + height, x, y + 1);
    }

    public void drawRoundRect(int x, int y, int width, int height, int arcWidth, int arcHeight) {
        if (width < 0 || height < 0) return;
        Native.roundRect(buf, bw, bh, x + tx, y + ty, width, height, Math.abs(arcWidth), Math.abs(arcHeight), color, false,
                clipX, clipY, clipW, clipH);
    }

    public void fillRoundRect(int x, int y, int width, int height, int arcWidth, int arcHeight) {
        if (width <= 0 || height <= 0) return;
        Native.roundRect(buf, bw, bh, x + tx, y + ty, width, height, Math.abs(arcWidth), Math.abs(arcHeight), color, true,
                clipX, clipY, clipW, clipH);
    }

    public void fillArc(int x, int y, int width, int height, int startAngle, int arcAngle) {
        if (width <= 0 || height <= 0 || arcAngle == 0) return;
        Native.arc(buf, bw, bh, x + tx, y + ty, width, height, startAngle, arcAngle, color, true, clipX, clipY, clipW, clipH);
    }

    public void drawArc(int x, int y, int width, int height, int startAngle, int arcAngle) {
        if (width < 0 || height < 0 || arcAngle == 0) return;
        Native.arc(buf, bw, bh, x + tx, y + ty, width, height, startAngle, arcAngle, color, false, clipX, clipY, clipW, clipH);
    }

    public void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3) {
        Native.triangle(buf, bw, bh, x1 + tx, y1 + ty, x2 + tx, y2 + ty, x3 + tx, y3 + ty, color, clipX, clipY, clipW, clipH);
    }

    // ---- text
    private int textX(int x, int w, int anchor) {
        if ((anchor & HCENTER) != 0) return x - w / 2;
        if ((anchor & RIGHT) != 0) return x - w;
        return x;
    }

    private int textY(int y, int anchor) {
        if ((anchor & BOTTOM) != 0) return y - font.getHeight();
        if ((anchor & BASELINE) != 0) return y - font.getBaselinePosition();
        return y;
    }

    private static void checkTextAnchor(int anchor) {
        if (anchor == 0) return;
        int h = anchor & (LEFT | RIGHT | HCENTER), v = anchor & (TOP | BOTTOM | BASELINE);
        if ((anchor & ~(LEFT | RIGHT | HCENTER | TOP | BOTTOM | BASELINE)) != 0 || (h & (h - 1)) != 0 || (v & (v - 1)) != 0)
            throw new IllegalArgumentException("anchor " + anchor);
    }

    public void drawString(String str, int x, int y, int anchor) {
        if (str == null) throw new NullPointerException();
        drawSubstring(str, 0, str.length(), x, y, anchor);
    }

    public void drawSubstring(String str, int offset, int len, int x, int y, int anchor) {
        if (str == null) throw new NullPointerException();
        if (offset < 0 || len < 0 || offset + len > str.length()) throw new StringIndexOutOfBoundsException();
        checkTextAnchor(anchor);
        if (len == 0) return;
        int w = (anchor & (HCENTER | RIGHT)) != 0 ? Native.textWidth(str, offset, len, font.id()) : 0;
        int left = textX(x + tx, w, anchor), top = textY(y + ty, anchor);
        int drawn = Native.text(buf, bw, bh, str, offset, len, font.id(), left, top, color, clipX, clipY, clipW, clipH);
        if (font.isUnderlined())
            Native.fill(buf, bw, bh, left, top + font.getBaselinePosition() + 1, drawn, 1, color, clipX, clipY, clipW, clipH);
    }

    public void drawChar(char character, int x, int y, int anchor) {
        drawString(String.valueOf(character), x, y, anchor);
    }

    public void drawChars(char[] data, int offset, int length, int x, int y, int anchor) {
        if (offset < 0 || length < 0 || offset + length > data.length) throw new ArrayIndexOutOfBoundsException();
        drawString(new String(data, offset, length), x, y, anchor);
    }

    // ---- images
    public void drawImage(Image img, int x, int y, int anchor) {
        if (img == null) throw new NullPointerException();
        drawRegion(img, 0, 0, img.w, img.h, 0, x, y, anchor);
    }

    public void drawRegion(Image src, int x_src, int y_src, int width, int height, int transform, int x_dest, int y_dest,
                           int anchor) {
        if (src == null) throw new NullPointerException();
        if (transform < 0 || transform > 7) throw new IllegalArgumentException();
        if (x_src < 0 || y_src < 0 || width < 0 || height < 0 || x_src + width > src.w || y_src + height > src.h)
            throw new IllegalArgumentException();
        if (src.px == buf) throw new IllegalArgumentException();  // an image drawn onto itself
        if (width == 0 || height == 0) return;
        boolean swap = (transform & 4) != 0;
        int dw = swap ? height : width, dh = swap ? width : height;
        int x = x_dest + tx, y = y_dest + ty;
        if ((anchor & HCENTER) != 0) x -= dw / 2;
        else if ((anchor & RIGHT) != 0) x -= dw;
        if ((anchor & VCENTER) != 0) y -= dh / 2;
        else if ((anchor & BOTTOM) != 0) y -= dh;
        else if ((anchor & BASELINE) != 0) throw new IllegalArgumentException();
        Native.blit(buf, bw, bh, src.px, src.w, src.h, x_src, y_src, width, height, transform, x, y, src.alpha,
                clipX, clipY, clipW, clipH);
    }

    public void copyArea(int x_src, int y_src, int width, int height, int x_dest, int y_dest, int anchor) {
        if (width <= 0 || height <= 0) return;
        int[] tmp = new int[width * height];
        int sx = x_src + tx, sy = y_src + ty;
        if (sx < 0 || sy < 0 || sx + width > bw || sy + height > bh) throw new IllegalArgumentException();
        for (int r = 0; r < height; r++) System.arraycopy(buf, (sy + r) * bw + sx, tmp, r * width, width);
        int x = x_dest + tx, y = y_dest + ty;
        if ((anchor & HCENTER) != 0) x -= width / 2;
        else if ((anchor & RIGHT) != 0) x -= width;
        if ((anchor & VCENTER) != 0) y -= height / 2;
        else if ((anchor & BOTTOM) != 0) y -= height;
        Native.blit(buf, bw, bh, tmp, width, height, 0, 0, width, height, 0, x, y, false, clipX, clipY, clipW, clipH);
    }

    public void drawRGB(int[] rgbData, int offset, int scanlength, int x, int y, int width, int height, boolean processAlpha) {
        if (rgbData == null) throw new NullPointerException();
        if (width <= 0 || height <= 0) return;
        Native.rgb(buf, bw, bh, rgbData, offset, scanlength, x + tx, y + ty, width, height, processAlpha,
                clipX, clipY, clipW, clipH);
    }

    public int getDisplayColor(int color) { return color & 0xFFFFFF; }
}
