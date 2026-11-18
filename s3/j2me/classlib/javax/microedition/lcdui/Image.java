package javax.microedition.lcdui;

import com.espos.j2me.Native;
import java.io.IOException;
import java.io.InputStream;

/** Pixels ARGB, one int each. Mutable images (createImage(w, h)) are opaque and start white. */
public class Image {
    final int[] px;
    final int w, h;
    final boolean mutable;
    final boolean alpha;  // has transparent pixels (drawn blended)

    Image(int[] px, int w, int h, boolean mutable, boolean alpha) {
        this.px = px;
        this.w = w;
        this.h = h;
        this.mutable = mutable;
        this.alpha = alpha;
    }

    public static Image createImage(int width, int height) {
        if (width <= 0 || height <= 0) throw new IllegalArgumentException();
        int[] p = new int[width * height];
        for (int i = 0; i < p.length; i++) p[i] = 0xFFFFFFFF;
        return new Image(p, width, height, true, false);
    }

    public static Image createImage(Image source) {
        if (!source.mutable) return source;
        int[] p = new int[source.px.length];
        System.arraycopy(source.px, 0, p, 0, p.length);
        return new Image(p, source.w, source.h, false, false);
    }

    public static Image createImage(String name) throws IOException {
        if (name == null) throw new NullPointerException();
        InputStream in = Image.class.getResourceAsStream(name.startsWith("/") ? name : "/" + name);
        if (in == null) throw new IOException("no image " + name);
        try {
            return createImage(in);
        } finally {
            in.close();
        }
    }

    public static Image createImage(byte[] imageData, int imageOffset, int imageLength) {
        if (imageOffset < 0 || imageLength < 0 || imageOffset + imageLength > imageData.length)
            throw new ArrayIndexOutOfBoundsException();
        int[] r = Native.decodeImage(imageData, imageOffset, imageLength);
        if (r == null) throw new IllegalArgumentException("not an image");
        return fromDecoded(r);
    }

    public static Image createImage(InputStream stream) throws IOException {
        if (stream == null) throw new NullPointerException();
        byte[] buf = new byte[4096];
        int n = 0;
        for (;;) {
            if (n == buf.length) {
                byte[] b2 = new byte[buf.length * 2];
                System.arraycopy(buf, 0, b2, 0, n);
                buf = b2;
            }
            int r = stream.read(buf, n, buf.length - n);
            if (r < 0) break;
            n += r;
        }
        int[] r = Native.decodeImage(buf, 0, n);
        if (r == null) throw new IOException("not an image");
        return fromDecoded(r);
    }

    private static Image fromDecoded(int[] r) {
        int w = r[0], h = r[1];
        int[] p = new int[w * h];
        System.arraycopy(r, 2, p, 0, p.length);
        boolean a = false;
        for (int i = 0; i < p.length && !a; i++) a = (p[i] >>> 24) != 0xFF;
        return new Image(p, w, h, false, a);
    }

    public static Image createImage(Image image, int x, int y, int width, int height, int transform) {
        if (width <= 0 || height <= 0 || x < 0 || y < 0 || x + width > image.w || y + height > image.h)
            throw new IllegalArgumentException();
        if (transform < 0 || transform > 7) throw new IllegalArgumentException();
        boolean swap = (transform & 4) != 0;
        int dw = swap ? height : width, dh = swap ? width : height;
        int[] p = new int[dw * dh];
        Native.blit(p, dw, dh, image.px, image.w, image.h, x, y, width, height, transform, 0, 0, false, 0, 0, dw, dh);
        return new Image(p, dw, dh, false, image.alpha);
    }

    public static Image createRGBImage(int[] rgb, int width, int height, boolean processAlpha) {
        if (rgb == null) throw new NullPointerException();
        if (width <= 0 || height <= 0) throw new IllegalArgumentException();
        if (width * height > rgb.length) throw new ArrayIndexOutOfBoundsException();
        int[] p = new int[width * height];
        boolean a = false;
        for (int i = 0; i < p.length; i++) {
            int c = rgb[i];
            if (!processAlpha) c |= 0xFF000000;
            else if ((c >>> 24) != 0xFF) a = true;
            p[i] = c;
        }
        return new Image(p, width, height, false, a);
    }

    public Graphics getGraphics() {
        if (!mutable) throw new IllegalStateException();
        return new Graphics(px, w, h);
    }

    public int getWidth() { return w; }
    public int getHeight() { return h; }
    public boolean isMutable() { return mutable; }

    public void getRGB(int[] rgbData, int offset, int scanlength, int x, int y, int width, int height) {
        if (x < 0 || y < 0 || x + width > w || y + height > h) throw new IllegalArgumentException();
        if (Math.abs(scanlength) < width) throw new IllegalArgumentException();
        for (int r = 0; r < height; r++) {
            int s = (y + r) * w + x, d = offset + r * scanlength;
            for (int c = 0; c < width; c++) rgbData[d + c] = px[s + c];
        }
    }
}
