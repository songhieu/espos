package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.LcduiBridge;

/**
 * Frames cut from an image, a frame sequence, a reference pixel and the 8 transforms, as MIDP 2.0 (and its reference
 * implementation) define them: the reference pixel is a pixel of the untransformed frame; setTransform keeps it at the
 * same place on the screen; the collision rectangle turns with the frame. Pixel-level collisions count a pixel as
 * solid when its alpha is not 0.
 *
 * Where a frame pixel (px, py) of a w x h frame lands in the transformed frame (pixel maps, hence the "- 1"):
 *   NONE (px, py)        MIRROR (w-1-px, py)        MIRROR_ROT180 (px, h-1-py)    ROT180 (w-1-px, h-1-py)
 *   ROT90 (h-1-py, px)   ROT270 (py, w-1-px)        MIRROR_ROT90 (h-1-py, w-1-px) MIRROR_ROT270 (py, px)
 * ROT* turn clockwise; MIRROR_* mirror about the vertical centre first. ROT90, ROT270 and both MIRROR_ROT90/270 swap
 * width and height (bit 4 of the value, as in Graphics.drawRegion).
 */
public class Sprite extends Layer {
    public static final int TRANS_NONE = 0, TRANS_ROT90 = 5, TRANS_ROT180 = 3, TRANS_ROT270 = 6, TRANS_MIRROR = 2,
            TRANS_MIRROR_ROT90 = 7, TRANS_MIRROR_ROT180 = 1, TRANS_MIRROR_ROT270 = 4;

    Image image;
    int frameW, frameH, rawFrames, perRow;
    private int[] seq;
    private boolean customSeq;
    private int seqIndex;
    int refX, refY;          // in the untransformed frame
    int transform;
    int colX, colY, colW, colH;      // the collision rectangle, untransformed frame coordinates
    int tColX, tColY, tColW, tColH;  // the same after the transform, from the sprite's top-left

    public Sprite(Image image) {
        super(image.getWidth(), image.getHeight());
        frames(image, image.getWidth(), image.getHeight());
        seq = defaultSequence(rawFrames);
        resetCollision();
    }

    public Sprite(Image image, int frameWidth, int frameHeight) {
        super(frameWidth, frameHeight);
        frames(image, frameWidth, frameHeight);
        seq = defaultSequence(rawFrames);
        resetCollision();
    }

    public Sprite(Sprite s) {
        super(s.width, s.height);
        image = s.image;
        frameW = s.frameW;
        frameH = s.frameH;
        rawFrames = s.rawFrames;
        perRow = s.perRow;
        seq = new int[s.seq.length];
        System.arraycopy(s.seq, 0, seq, 0, seq.length);
        customSeq = s.customSeq;
        seqIndex = s.seqIndex;
        refX = s.refX;
        refY = s.refY;
        transform = s.transform;
        colX = s.colX;
        colY = s.colY;
        colW = s.colW;
        colH = s.colH;
        tColX = s.tColX;
        tColY = s.tColY;
        tColW = s.tColW;
        tColH = s.tColH;
        x = s.x;
        y = s.y;
        visible = s.visible;
    }

    private void frames(Image img, int fw, int fh) {
        if (img == null) throw new NullPointerException();
        if (fw < 1 || fh < 1 || img.getWidth() % fw != 0 || img.getHeight() % fh != 0)
            throw new IllegalArgumentException();
        image = img;
        frameW = fw;
        frameH = fh;
        perRow = img.getWidth() / fw;
        rawFrames = perRow * (img.getHeight() / fh);
    }

    private static int[] defaultSequence(int n) {
        int[] s = new int[n];
        for (int i = 0; i < n; i++) s[i] = i;
        return s;
    }

    private void resetCollision() {
        colX = colY = 0;
        colW = frameW;
        colH = frameH;
        bounds();
    }

    // ---- the transforms

    /** Where frame pixel (px, py) of a w x h frame lands under transform t: x. */
    static int ptX(int px, int py, int t, int w, int h) {
        switch (t) {
            case TRANS_MIRROR: case TRANS_ROT180: return w - 1 - px;
            case TRANS_ROT90: case TRANS_MIRROR_ROT90: return h - 1 - py;
            case TRANS_ROT270: case TRANS_MIRROR_ROT270: return py;
            default: return px;  // NONE, MIRROR_ROT180
        }
    }

    /** ... and y. */
    static int ptY(int px, int py, int t, int w, int h) {
        switch (t) {
            case TRANS_MIRROR_ROT180: case TRANS_ROT180: return h - 1 - py;
            case TRANS_ROT90: case TRANS_MIRROR_ROT270: return px;
            case TRANS_ROT270: case TRANS_MIRROR_ROT90: return w - 1 - px;
            default: return py;  // NONE, MIRROR
        }
    }

    /** The transformed size and collision rectangle (the reference implementation's table). */
    private void bounds() {
        int w = frameW, h = frameH;
        switch (transform) {
            case TRANS_NONE: set(colX, colY, colW, colH); break;
            case TRANS_MIRROR: set(w - (colX + colW), colY, colW, colH); break;
            case TRANS_MIRROR_ROT180: set(colX, h - (colY + colH), colW, colH); break;
            case TRANS_ROT180: set(w - (colX + colW), h - (colY + colH), colW, colH); break;
            case TRANS_ROT90: set(h - (colY + colH), colX, colH, colW); break;
            case TRANS_ROT270: set(colY, w - (colX + colW), colH, colW); break;
            case TRANS_MIRROR_ROT90: set(h - (colY + colH), w - (colX + colW), colH, colW); break;
            default: set(colY, colX, colH, colW); break;  // MIRROR_ROT270
        }
        if ((transform & 4) != 0) {
            width = h;
            height = w;
        } else {
            width = w;
            height = h;
        }
    }

    private void set(int x, int y, int w, int h) {
        tColX = x;
        tColY = y;
        tColW = w;
        tColH = h;
    }

    public void setTransform(int transform) {
        if (transform < 0 || transform > 7) throw new IllegalArgumentException();
        x += ptX(refX, refY, this.transform, frameW, frameH) - ptX(refX, refY, transform, frameW, frameH);
        y += ptY(refX, refY, this.transform, frameW, frameH) - ptY(refX, refY, transform, frameW, frameH);
        this.transform = transform;
        bounds();
    }

    public void defineReferencePixel(int x, int y) {
        refX = x;
        refY = y;
    }

    public void setRefPixelPosition(int x, int y) {
        this.x = x - ptX(refX, refY, transform, frameW, frameH);
        this.y = y - ptY(refX, refY, transform, frameW, frameH);
    }

    public int getRefPixelX() { return x + ptX(refX, refY, transform, frameW, frameH); }

    public int getRefPixelY() { return y + ptY(refX, refY, transform, frameW, frameH); }

    public void defineCollisionRectangle(int x, int y, int width, int height) {
        if (width < 0 || height < 0) throw new IllegalArgumentException();
        colX = x;
        colY = y;
        colW = width;
        colH = height;
        bounds();
    }

    // ---- frames
    public void setImage(Image img, int frameWidth, int frameHeight) {
        if (img == null) throw new NullPointerException();
        if (frameWidth < 1 || frameHeight < 1 || img.getWidth() % frameWidth != 0 || img.getHeight() % frameHeight != 0)
            throw new IllegalArgumentException();
        int n = (img.getWidth() / frameWidth) * (img.getHeight() / frameHeight);
        boolean keep = n >= rawFrames;
        if (frameWidth != frameW || frameHeight != frameH) {
            int rx = getRefPixelX(), ry = getRefPixelY();  // the reference pixel stays where it is on the screen
            frames(img, frameWidth, frameHeight);
            colX = colY = 0;
            colW = frameW;
            colH = frameH;
            bounds();
            x = rx - ptX(refX, refY, transform, frameW, frameH);
            y = ry - ptY(refX, refY, transform, frameW, frameH);
        } else {
            frames(img, frameWidth, frameHeight);
        }
        if (!keep) {
            seq = defaultSequence(rawFrames);
            customSeq = false;
            seqIndex = 0;
        } else if (!customSeq) {
            seq = defaultSequence(rawFrames);
        }
    }

    public void setFrameSequence(int[] sequence) {
        if (sequence == null) {
            seq = defaultSequence(rawFrames);
            customSeq = false;
            seqIndex = 0;
            return;
        }
        if (sequence.length < 1) throw new IllegalArgumentException();
        for (int i = 0; i < sequence.length; i++)
            if (sequence[i] < 0 || sequence[i] >= rawFrames) throw new ArrayIndexOutOfBoundsException();
        seq = new int[sequence.length];
        System.arraycopy(sequence, 0, seq, 0, seq.length);
        customSeq = true;
        seqIndex = 0;
    }

    public final int getFrame() { return seqIndex; }

    public int getRawFrameCount() { return rawFrames; }

    public int getFrameSequenceLength() { return seq.length; }

    public void setFrame(int sequenceIndex) {
        if (sequenceIndex < 0 || sequenceIndex >= seq.length) throw new IndexOutOfBoundsException();
        seqIndex = sequenceIndex;
    }

    public void nextFrame() { seqIndex = (seqIndex + 1) % seq.length; }

    public void prevFrame() { seqIndex = seqIndex == 0 ? seq.length - 1 : seqIndex - 1; }

    /** The current frame's top-left in the image. */
    int frameSrcX() { return (seq[seqIndex] % perRow) * frameW; }

    int frameSrcY() { return (seq[seqIndex] / perRow) * frameH; }

    public final void paint(Graphics g) {
        if (g == null) throw new NullPointerException();
        if (!visible) return;
        g.drawRegion(image, frameSrcX(), frameSrcY(), frameW, frameH, transform, x, y, Graphics.TOP | Graphics.LEFT);
    }

    // ---- collisions

    /** The pixel at (tx, ty) of the transformed w x h region (rx, ry) of an image's pixels (width iw): solid? */
    static boolean solid(int[] px, int iw, int rx, int ry, int w, int h, int t, int tx, int ty) {
        int sx, sy;
        switch (t) {
            case TRANS_MIRROR_ROT180: sx = tx; sy = h - 1 - ty; break;
            case TRANS_MIRROR: sx = w - 1 - tx; sy = ty; break;
            case TRANS_ROT180: sx = w - 1 - tx; sy = h - 1 - ty; break;
            case TRANS_MIRROR_ROT270: sx = ty; sy = tx; break;
            case TRANS_ROT90: sx = ty; sy = h - 1 - tx; break;
            case TRANS_ROT270: sx = w - 1 - ty; sy = tx; break;
            case TRANS_MIRROR_ROT90: sx = w - 1 - ty; sy = h - 1 - tx; break;
            default: sx = tx; sy = ty; break;
        }
        return (px[(ry + sy) * iw + rx + sx] >>> 24) != 0;
    }

    /** This sprite's pixel at (wx, wy) (inside its bounds) is solid. */
    boolean solidAt(int wx, int wy) {
        if (!LcduiBridge.hasAlpha(image)) return true;
        return solid(LcduiBridge.pixels(image), image.getWidth(), frameSrcX(), frameSrcY(), frameW, frameH, transform,
                wx - x, wy - y);
    }

    private static boolean overlap(int l1, int t1, int r1, int b1, int l2, int t2, int r2, int b2) {
        return l1 < r2 && l2 < r1 && t1 < b2 && t2 < b1;
    }

    /** The collision rectangle on the screen {left, top, right, bottom}; inside the frame only when frameOnly. */
    private void colRect(int[] r, boolean frameOnly) {
        r[0] = x + tColX;
        r[1] = y + tColY;
        r[2] = r[0] + tColW;
        r[3] = r[1] + tColH;
        if (frameOnly) {
            if (r[0] < x) r[0] = x;
            if (r[1] < y) r[1] = y;
            if (r[2] > x + width) r[2] = x + width;
            if (r[3] > y + height) r[3] = y + height;
        }
    }

    public final boolean collidesWith(Sprite s, boolean pixelLevel) {
        if (!(s.visible && visible)) return false;
        int[] a = new int[4], b = new int[4];
        colRect(a, false);
        s.colRect(b, false);
        if (!overlap(a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3])) return false;
        if (!pixelLevel) return true;
        colRect(a, true);
        s.colRect(b, true);
        int l = Math.max(a[0], b[0]), t = Math.max(a[1], b[1]), r = Math.min(a[2], b[2]), bt = Math.min(a[3], b[3]);
        if (l >= r || t >= bt) return false;
        if (!LcduiBridge.hasAlpha(image) && !LcduiBridge.hasAlpha(s.image)) return true;
        for (int wy = t; wy < bt; wy++)
            for (int wx = l; wx < r; wx++)
                if (solidAt(wx, wy) && s.solidAt(wx, wy)) return true;
        return false;
    }

    public final boolean collidesWith(TiledLayer tl, boolean pixelLevel) {
        if (!(tl.visible && visible)) return false;
        int[] a = new int[4];
        colRect(a, pixelLevel);
        int ll = tl.x, lt = tl.y;
        int l = Math.max(a[0], ll), t = Math.max(a[1], lt);
        int r = Math.min(a[2], ll + tl.width), b = Math.min(a[3], lt + tl.height);
        if (l >= r || t >= b) return false;
        int cw = tl.cellW, ch = tl.cellH;
        int c0 = (l - ll) / cw, c1 = (r - 1 - ll) / cw, r0 = (t - lt) / ch, r1 = (b - 1 - lt) / ch;
        for (int row = r0; row <= r1; row++) {
            for (int col = c0; col <= c1; col++) {
                int tile = tl.staticTileAt(col, row);
                if (tile == 0) continue;
                if (!pixelLevel) return true;
                int cx = ll + col * cw, cy = lt + row * ch;
                int ql = Math.max(l, cx), qt = Math.max(t, cy), qr = Math.min(r, cx + cw), qb = Math.min(b, cy + ch);
                for (int wy = qt; wy < qb; wy++)
                    for (int wx = ql; wx < qr; wx++)
                        if (solidAt(wx, wy) && tl.solidTile(tile, wx - cx, wy - cy)) return true;
            }
        }
        return false;
    }

    public final boolean collidesWith(Image img, int x, int y, boolean pixelLevel) {
        if (img == null) throw new NullPointerException();
        if (!visible) return false;
        int[] a = new int[4];
        colRect(a, pixelLevel);
        int l = Math.max(a[0], x), t = Math.max(a[1], y);
        int r = Math.min(a[2], x + img.getWidth()), b = Math.min(a[3], y + img.getHeight());
        if (l >= r || t >= b) return false;
        if (!pixelLevel) return true;
        boolean ia = LcduiBridge.hasAlpha(img);
        if (!ia && !LcduiBridge.hasAlpha(image)) return true;
        int[] px = LcduiBridge.pixels(img);
        int iw = img.getWidth();
        for (int wy = t; wy < b; wy++)
            for (int wx = l; wx < r; wx++)
                if (solidAt(wx, wy) && (!ia || (px[(wy - y) * iw + (wx - x)] >>> 24) != 0)) return true;
        return false;
    }
}
