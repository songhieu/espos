package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Image;

/**
 * Sprite, TiledLayer and LayerManager logic, no drawing (runs with or without the device's natives). The expected
 * values are worked out by hand from MIDP 2.0's Sprite rules (see the comments by each check).
 */
public class GameTest {
    static int n, fails;

    static void eq(String what, long got, long want) {
        n++;
        if (got != want) {
            fails++;
            System.out.println("FAIL " + what + ": got " + got + " want " + want);
        }
    }

    static void ok(String what, boolean c) {
        n++;
        if (!c) {
            fails++;
            System.out.println("FAIL " + what);
        }
    }

    interface Run { void run() throws Exception; }

    static void throwsIt(String what, Class want, Run r) {
        n++;
        try {
            r.run();
            fails++;
            System.out.println("FAIL " + what + ": no exception, want " + want.getName());
        } catch (Throwable e) {
            if (!want.isInstance(e)) {
                fails++;
                System.out.println("FAIL " + what + ": got " + e + " want " + want.getName());
            }
        }
    }

    /** w x h ARGB image; solid: the listed pixels {x, y, ...} are opaque red, the rest clear (all opaque if null). */
    static Image img(int w, int h, int[] solid) {
        int[] p = new int[w * h];
        if (solid == null) {
            for (int i = 0; i < p.length; i++) p[i] = 0xFFFF0000;
            return Image.createRGBImage(p, w, h, false);
        }
        for (int i = 0; i < solid.length; i += 2) p[solid[i + 1] * w + solid[i]] = 0xFFFF0000;
        return Image.createRGBImage(p, w, h, true);
    }

    static void pos(String what, Sprite s, int x, int y) {
        eq(what + " x", s.getX(), x);
        eq(what + " y", s.getY(), y);
    }

    static void ref(String what, Sprite s, int x, int y) {
        eq(what + " refX", s.getRefPixelX(), x);
        eq(what + " refY", s.getRefPixelY(), y);
    }

    public static void main(String[] a) {
        transforms();
        collisionRects();
        collisions();
        frames();
        tiled();
        layers();
        if (fails == 0) System.out.println("GameTest: OK (" + n + " checks)");
        else System.out.println("GameTest: FAILED " + fails + " of " + n);
        System.exit(fails == 0 ? 0 : 1);
    }

    /** One 20 x 10 frame, reference pixel (5, 2), at (100, 50): every transform keeps the reference pixel put. */
    static void transforms() {
        Sprite s = new Sprite(img(20, 10, null));
        s.defineReferencePixel(5, 2);
        s.setPosition(100, 50);
        ref("none", s, 105, 52);
        // MIRROR: ref lands at (20-1-5, 2) = (14, 2): x = 100 + 5 - 14 = 91
        s.setTransform(Sprite.TRANS_MIRROR);
        pos("mirror", s, 91, 50);
        ref("mirror", s, 105, 52);
        eq("mirror w", s.getWidth(), 20);
        // ROT90: (10-1-2, 5) = (7, 5): x = 91 + 14 - 7 = 98, y = 50 + 2 - 5 = 47; 10 wide, 20 high
        s.setTransform(Sprite.TRANS_ROT90);
        pos("rot90", s, 98, 47);
        ref("rot90", s, 105, 52);
        eq("rot90 w", s.getWidth(), 10);
        eq("rot90 h", s.getHeight(), 20);
        // ROT270: (2, 20-1-5) = (2, 14): x = 98 + 7 - 2 = 103, y = 47 + 5 - 14 = 38
        s.setTransform(Sprite.TRANS_ROT270);
        pos("rot270", s, 103, 38);
        ref("rot270", s, 105, 52);
        // MIRROR_ROT90: (7, 14): x = 103 + 2 - 7 = 98, y = 38
        s.setTransform(Sprite.TRANS_MIRROR_ROT90);
        pos("mirror rot90", s, 98, 38);
        // MIRROR_ROT270: (2, 5): x = 98 + 7 - 2 = 103, y = 38 + 14 - 5 = 47
        s.setTransform(Sprite.TRANS_MIRROR_ROT270);
        pos("mirror rot270", s, 103, 47);
        eq("mirror rot270 h", s.getHeight(), 20);
        // ROT180: (14, 7): x = 103 + 2 - 14 = 91, y = 47 + 5 - 7 = 45
        s.setTransform(Sprite.TRANS_ROT180);
        pos("rot180", s, 91, 45);
        // MIRROR_ROT180: (5, 7): x = 91 + 14 - 5 = 100, y = 45
        s.setTransform(Sprite.TRANS_MIRROR_ROT180);
        pos("mirror rot180", s, 100, 45);
        ref("mirror rot180", s, 105, 52);
        // back to NONE: (5, 2): y = 45 + 7 - 2 = 50, the start again
        s.setTransform(Sprite.TRANS_NONE);
        pos("none again", s, 100, 50);
        // setRefPixelPosition under ROT90: x = 0 - 7, y = 0 - 5
        s.setTransform(Sprite.TRANS_ROT90);
        s.setRefPixelPosition(0, 0);
        pos("ref at 0,0 rot90", s, -7, -5);
        ref("ref at 0,0 rot90", s, 0, 0);
        // a reference pixel outside the frame works the same way: (-3, 12) under MIRROR -> (20-1+3, 12) = (22, 12)
        Sprite o = new Sprite(img(20, 10, null));
        o.defineReferencePixel(-3, 12);
        o.setRefPixelPosition(0, 0);
        pos("outside ref", o, 3, -12);
        o.setTransform(Sprite.TRANS_MIRROR);
        pos("outside ref mirror", o, -22, -12);
        throwsIt("transform 8", IllegalArgumentException.class, new Run() {
            public void run() { new Sprite(img(2, 2, null)).setTransform(8); }
        });
        // defining the reference pixel does not move the sprite
        Sprite m = new Sprite(img(20, 10, null));
        m.setPosition(4, 4);
        m.defineReferencePixel(10, 5);
        pos("define ref keeps position", m, 4, 4);
    }

    /** Collision rectangle (2, 1, 6, 3) of a 20 x 10 frame under each transform (the pixels map as in Sprite's doc). */
    static void collisionRects() {
        Sprite s = new Sprite(img(20, 10, null));
        s.defineCollisionRectangle(2, 1, 6, 3);
        int[][] want = {
            {0, 2, 1, 6, 3},                              // NONE
            {Sprite.TRANS_MIRROR, 12, 1, 6, 3},           // x: 20 - (2 + 6)
            {Sprite.TRANS_MIRROR_ROT180, 2, 6, 6, 3},     // y: 10 - (1 + 3)
            {Sprite.TRANS_ROT180, 12, 6, 6, 3},
            {Sprite.TRANS_ROT90, 6, 2, 3, 6},             // pixel (px, py) -> (9 - py, px): x 6..8, y 2..7
            {Sprite.TRANS_ROT270, 1, 12, 3, 6},           // (py, 19 - px): x 1..3, y 12..17
            {Sprite.TRANS_MIRROR_ROT90, 6, 12, 3, 6},     // (9 - py, 19 - px)
            {Sprite.TRANS_MIRROR_ROT270, 1, 2, 3, 6},     // (py, px)
        };
        for (int i = 0; i < want.length; i++) {
            s.setTransform(want[i][0]);
            String t = "collision rect t" + want[i][0];
            eq(t + " x", s.tColX, want[i][1]);
            eq(t + " y", s.tColY, want[i][2]);
            eq(t + " w", s.tColW, want[i][3]);
            eq(t + " h", s.tColH, want[i][4]);
        }
        throwsIt("negative collision width", IllegalArgumentException.class, new Run() {
            public void run() { new Sprite(img(2, 2, null)).defineCollisionRectangle(0, 0, -1, 1); }
        });
    }

    static void collisions() {
        Sprite a = new Sprite(img(10, 10, null)), b = new Sprite(img(10, 10, null));
        b.setPosition(9, 9);
        ok("overlap by one pixel", a.collidesWith(b, false) && a.collidesWith(b, true) && b.collidesWith(a, true));
        b.setPosition(10, 0);
        ok("touching edges do not collide", !a.collidesWith(b, false));
        b.setPosition(0, 0);
        b.setVisible(false);
        ok("hidden sprite does not collide", !a.collidesWith(b, false));
        b.setVisible(true);
        a.defineCollisionRectangle(0, 0, 5, 5);
        b.setPosition(5, 5);
        ok("outside the collision rectangle", !a.collidesWith(b, false));
        b.setPosition(4, 4);
        ok("inside the collision rectangle", a.collidesWith(b, false));

        // pixel level: only pixel (9, 9) of c is solid
        Sprite c = new Sprite(img(10, 10, new int[] {9, 9}));
        Sprite probe = new Sprite(img(4, 4, null));
        probe.setPosition(5, 5);  // covers 5..8: rectangles overlap, c's pixels there are clear
        ok("rect level sees the overlap", c.collidesWith(probe, false));
        ok("pixel level sees clear pixels", !c.collidesWith(probe, true));
        probe.setPosition(9, 9);
        ok("pixel level hits the solid pixel", c.collidesWith(probe, true) && probe.collidesWith(c, true));

        // the solid pixel (0, 0) under MIRROR shows at (9, 0)
        Sprite d = new Sprite(img(10, 10, new int[] {0, 0}));
        d.setTransform(Sprite.TRANS_MIRROR);
        d.setPosition(0, 0);
        Sprite dot = new Sprite(img(1, 1, null));
        dot.setPosition(9, 0);
        ok("mirrored solid pixel at (9, 0)", d.collidesWith(dot, true));
        dot.setPosition(0, 0);
        ok("mirrored: (0, 0) now clear", !d.collidesWith(dot, true));

        // a 4 x 2 frame, solid at (3, 0): ROT90 puts it at (2 - 1 - 0, 3) = (1, 3) of a 2 x 4 box
        Sprite e = new Sprite(img(4, 2, new int[] {3, 0}));
        e.setTransform(Sprite.TRANS_ROT90);
        e.setPosition(0, 0);
        dot.setPosition(1, 3);
        ok("rot90 solid pixel at (1, 3)", e.collidesWith(dot, true));
        dot.setPosition(1, 0);
        ok("rot90 (1, 0) clear", !e.collidesWith(dot, true));
        // ROT270: (py, w - 1 - px) = (0, 0)
        e.setTransform(Sprite.TRANS_ROT270);
        e.setPosition(0, 0);
        dot.setPosition(0, 0);
        ok("rot270 solid pixel at (0, 0)", e.collidesWith(dot, true));
        // MIRROR_ROT90: (h - 1 - py, w - 1 - px) = (1, 0)
        e.setTransform(Sprite.TRANS_MIRROR_ROT90);
        e.setPosition(0, 0);
        dot.setPosition(1, 0);
        ok("mirror rot90 solid pixel at (1, 0)", e.collidesWith(dot, true));
        dot.setPosition(1, 3);
        ok("mirror rot90 (1, 3) clear", !e.collidesWith(dot, true));

        // a collision rectangle larger than the frame: pixel level only looks inside the frame
        Sprite f = new Sprite(img(4, 4, null));
        f.defineCollisionRectangle(-4, -4, 12, 12);
        dot.setPosition(-2, -2);
        ok("big rect level", f.collidesWith(dot, false));
        ok("big rect, pixel level outside the frame", !f.collidesWith(dot, true));

        // images
        Image solidCorner = img(3, 3, new int[] {0, 0});  // only its top-left pixel is solid
        Sprite g = new Sprite(img(2, 2, null));            // covers 10..11
        g.setPosition(10, 10);
        ok("image rect level", g.collidesWith(solidCorner, 9, 9, false));
        ok("image pixel level clear", !g.collidesWith(solidCorner, 9, 9, true));  // the solid pixel at (9, 9)
        ok("image pixel level solid", g.collidesWith(solidCorner, 11, 11, true));
        ok("image apart", !g.collidesWith(solidCorner, 12, 10, false));
    }

    static void frames() {
        Sprite s = new Sprite(img(30, 10, null), 10, 10);
        eq("raw frames", s.getRawFrameCount(), 3);
        eq("sequence length", s.getFrameSequenceLength(), 3);
        s.nextFrame();
        s.nextFrame();
        eq("frame after 2 next", s.getFrame(), 2);
        eq("frame 2 source x", s.frameSrcX(), 20);
        s.nextFrame();
        eq("next wraps", s.getFrame(), 0);
        s.prevFrame();
        eq("prev wraps", s.getFrame(), 2);
        s.setFrameSequence(new int[] {2, 0});
        eq("custom sequence starts at 0", s.getFrame(), 0);
        eq("custom frame 0 is raw 2", s.frameSrcX(), 20);
        s.nextFrame();
        eq("custom frame 1 is raw 0", s.frameSrcX(), 0);
        final Sprite t = s;
        throwsIt("sequence with frame 3", ArrayIndexOutOfBoundsException.class, new Run() {
            public void run() { t.setFrameSequence(new int[] {3}); }
        });
        throwsIt("empty sequence", IllegalArgumentException.class, new Run() {
            public void run() { t.setFrameSequence(new int[0]); }
        });
        throwsIt("setFrame past the sequence", IndexOutOfBoundsException.class, new Run() {
            public void run() { t.setFrame(2); }
        });
        throwsIt("frame width not dividing", IllegalArgumentException.class, new Run() {
            public void run() { new Sprite(img(30, 10, null), 7, 10); }
        });
        s.setFrameSequence(null);
        eq("default sequence back", s.getFrameSequenceLength(), 3);
        // a 2-row sheet: frame 4 of 3 x 2 frames of 10 x 10 is at (10, 10)
        Sprite sheet = new Sprite(img(30, 20, null), 10, 10);
        sheet.setFrame(4);
        eq("sheet frame 4 x", sheet.frameSrcX(), 10);
        eq("sheet frame 4 y", sheet.frameSrcY(), 10);
        // copy
        sheet.setPosition(7, 8);
        sheet.defineReferencePixel(1, 2);
        sheet.setTransform(Sprite.TRANS_ROT90);
        Sprite copy = new Sprite(sheet);
        ok("copy keeps position, frame, ref", copy.getX() == sheet.getX() && copy.getY() == sheet.getY()
                && copy.getFrame() == 4 && copy.getRefPixelX() == sheet.getRefPixelX() && copy.getWidth() == 10);
        // setImage with a new frame size keeps the reference pixel on the screen; fewer frames reset the sequence
        Sprite r = new Sprite(img(30, 10, null), 10, 10);
        r.defineReferencePixel(5, 5);
        r.setRefPixelPosition(50, 50);
        r.setFrame(2);
        r.setImage(img(20, 20, null), 20, 20);
        ref("setImage keeps ref", r, 50, 50);
        pos("setImage moves top-left", r, 45, 45);
        eq("setImage fewer frames resets", r.getFrame(), 0);
        eq("setImage new collision w", r.tColW, 20);
    }

    static void tiled() {
        // tiles: 1 solid 4 x 4; 2 clear but for its (0, 0)
        int[] p = new int[8 * 4];
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) p[y * 8 + x] = 0xFF00FF00;
        p[4] = 0xFF0000FF;
        Image tiles = Image.createRGBImage(p, 8, 4, true);
        TiledLayer t = new TiledLayer(3, 3, tiles, 4, 4);
        eq("layer width", t.getWidth(), 12);
        eq("columns", t.getColumns(), 3);
        t.setCell(1, 1, 1);
        eq("getCell", t.getCell(1, 1), 1);
        Sprite s = new Sprite(img(2, 2, null));
        s.setPosition(5, 5);
        ok("sprite on tile 1", s.collidesWith(t, false) && s.collidesWith(t, true));
        s.setPosition(0, 0);
        ok("sprite on an empty cell", !s.collidesWith(t, false));
        t.setCell(2, 2, 2);  // cell origin (8, 8), solid only at (8, 8)
        s.setPosition(9, 9);
        ok("tile 2 rect level", s.collidesWith(t, false));
        ok("tile 2 clear pixels", !s.collidesWith(t, true));
        t.setCell(1, 1, 0);   // so that only cell (2, 2) is under the sprite at (7, 7)
        s.setPosition(7, 7);
        ok("tile 2 solid pixel", s.collidesWith(t, true));
        t.setPosition(100, 0);
        ok("layer moved away", !s.collidesWith(t, false));
        t.setPosition(0, 0);
        // animated tiles
        int an = t.createAnimatedTile(2);
        eq("first animated tile", an, -1);
        t.setCell(0, 0, an);
        eq("animated shows 2", t.staticTileAt(0, 0), 2);
        t.setAnimatedTile(an, 1);
        eq("animated now 1", t.staticTileAt(0, 0), 1);
        eq("getAnimatedTile", t.getAnimatedTile(-1), 1);
        s.setPosition(1, 1);
        ok("collides through the animated tile", s.collidesWith(t, true));
        t.setAnimatedTile(an, 0);
        ok("animated tile set to empty", !s.collidesWith(t, false));
        eq("second animated tile", t.createAnimatedTile(0), -2);
        final TiledLayer tl = t;
        throwsIt("tile 3 of 2", IndexOutOfBoundsException.class, new Run() {
            public void run() { tl.setCell(0, 0, 3); }
        });
        throwsIt("animated -3 of 2", IndexOutOfBoundsException.class, new Run() {
            public void run() { tl.setCell(0, 0, -3); }
        });
        throwsIt("cell outside", IndexOutOfBoundsException.class, new Run() {
            public void run() { tl.setCell(3, 0, 1); }
        });
        throwsIt("animated from tile 3", IndexOutOfBoundsException.class, new Run() {
            public void run() { tl.createAnimatedTile(3); }
        });
        throwsIt("fillCells negative", IllegalArgumentException.class, new Run() {
            public void run() { tl.fillCells(0, 0, -1, 1, 1); }
        });
        throwsIt("fillCells too far", IndexOutOfBoundsException.class, new Run() {
            public void run() { tl.fillCells(1, 1, 3, 1, 1); }
        });
        t.fillCells(0, 1, 3, 2, 2);
        ok("fillCells", t.getCell(0, 1) == 2 && t.getCell(2, 2) == 2 && t.getCell(0, 0) == -1);
        // a smaller tile set clears everything; a bigger one keeps the cells
        t.setStaticTileSet(img(16, 4, null), 4, 4);
        ok("bigger tile set keeps cells", t.getCell(2, 2) == 2 && t.getAnimatedTile(-1) == 0);
        t.setStaticTileSet(img(4, 4, null), 4, 4);
        ok("smaller tile set clears cells", t.getCell(2, 2) == 0 && t.getCell(0, 0) == 0);
        throwsIt("animated tiles gone", IndexOutOfBoundsException.class, new Run() {
            public void run() { tl.getAnimatedTile(-1); }
        });
        t.setStaticTileSet(img(16, 16, null), 8, 8);
        eq("new cell size width", t.getWidth(), 24);
    }

    static void layers() {
        LayerManager m = new LayerManager();
        Sprite a = new Sprite(img(2, 2, null)), b = new Sprite(img(2, 2, null)), c = new Sprite(img(2, 2, null));
        m.append(a);
        m.append(b);
        ok("append order", m.getSize() == 2 && m.getLayerAt(0) == a && m.getLayerAt(1) == b);
        m.append(a);
        ok("append again moves it last", m.getSize() == 2 && m.getLayerAt(0) == b && m.getLayerAt(1) == a);
        m.insert(c, 0);
        ok("insert on top", m.getLayerAt(0) == c && m.getSize() == 3);
        m.insert(c, 2);
        ok("insert existing moves it", m.getLayerAt(2) == c && m.getLayerAt(0) == b);
        final LayerManager fm = m;
        final Sprite fc = c;
        throwsIt("insert existing at size", IndexOutOfBoundsException.class, new Run() {
            public void run() { fm.insert(fc, 3); }
        });
        throwsIt("insert past the end", IndexOutOfBoundsException.class, new Run() {
            public void run() { fm.insert(new Sprite(img(1, 1, null)), 4); }
        });
        m.remove(b);
        ok("remove", m.getSize() == 2 && m.getLayerAt(0) == a);
        throwsIt("getLayerAt 2", IndexOutOfBoundsException.class, new Run() {
            public void run() { fm.getLayerAt(2); }
        });
        throwsIt("negative view window", IllegalArgumentException.class, new Run() {
            public void run() { fm.setViewWindow(0, 0, -1, 10); }
        });
    }
}
