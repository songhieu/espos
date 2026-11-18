package javax.microedition.lcdui.game;

import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.LcduiBridge;

/**
 * A grid of cells, each showing a tile of the image (1..n, row by row; 0 empty) or an animated tile (-1, -2, ...:
 * whatever static tile it is set to now). paint draws only the cells inside the Graphics' clip: games draw it every
 * frame.
 */
public class TiledLayer extends Layer {
    private final int cols, rows;
    private final int[] cells;
    Image image;
    int cellW, cellH;
    private int tiles, perRow;       // static tiles: 1..tiles
    private int[] anim = new int[4]; // animated tile -(i+1) shows anim[i]
    private int animCount;

    public TiledLayer(int columns, int rows, Image image, int tileWidth, int tileHeight) {
        super(columns < 1 || tileWidth < 1 ? 0 : columns * tileWidth, rows < 1 || tileHeight < 1 ? 0 : rows * tileHeight);
        if (image == null) throw new NullPointerException();
        if (columns < 1 || rows < 1) throw new IllegalArgumentException();
        tileSet(image, tileWidth, tileHeight);
        cols = columns;
        this.rows = rows;
        cells = new int[columns * rows];
    }

    private void tileSet(Image img, int tw, int th) {
        if (tw < 1 || th < 1 || img.getWidth() % tw != 0 || img.getHeight() % th != 0)
            throw new IllegalArgumentException();
        image = img;
        cellW = tw;
        cellH = th;
        perRow = img.getWidth() / tw;
        tiles = perRow * (img.getHeight() / th);
    }

    public int createAnimatedTile(int staticTileIndex) {
        if (staticTileIndex < 0 || staticTileIndex > tiles) throw new IndexOutOfBoundsException();
        if (animCount == anim.length) {
            int[] a = new int[anim.length * 2];
            System.arraycopy(anim, 0, a, 0, animCount);
            anim = a;
        }
        anim[animCount++] = staticTileIndex;
        return -animCount;
    }

    public void setAnimatedTile(int animatedTileIndex, int staticTileIndex) {
        if (staticTileIndex < 0 || staticTileIndex > tiles) throw new IndexOutOfBoundsException();
        int i = -animatedTileIndex - 1;
        if (i < 0 || i >= animCount) throw new IndexOutOfBoundsException();
        anim[i] = staticTileIndex;
    }

    public int getAnimatedTile(int animatedTileIndex) {
        int i = -animatedTileIndex - 1;
        if (i < 0 || i >= animCount) throw new IndexOutOfBoundsException();
        return anim[i];
    }

    private void checkTile(int t) {
        if (t > tiles || t < -animCount) throw new IndexOutOfBoundsException();
    }

    public void setCell(int col, int row, int tileIndex) {
        if (col < 0 || col >= cols || row < 0 || row >= rows) throw new IndexOutOfBoundsException();
        checkTile(tileIndex);
        cells[row * cols + col] = tileIndex;
    }

    public int getCell(int col, int row) {
        if (col < 0 || col >= cols || row < 0 || row >= rows) throw new IndexOutOfBoundsException();
        return cells[row * cols + col];
    }

    public void fillCells(int col, int row, int numCols, int numRows, int tileIndex) {
        if (numCols < 0 || numRows < 0) throw new IllegalArgumentException();
        if (col < 0 || row < 0 || col + numCols > cols || row + numRows > rows) throw new IndexOutOfBoundsException();
        checkTile(tileIndex);
        for (int r = row; r < row + numRows; r++)
            for (int c = col; c < col + numCols; c++) cells[r * cols + c] = tileIndex;
    }

    public final int getCellWidth() { return cellW; }
    public final int getCellHeight() { return cellH; }
    public final int getColumns() { return cols; }
    public final int getRows() { return rows; }

    /** A new image: with at least as many tiles the cells and animated tiles stay, else they are all cleared. */
    public void setStaticTileSet(Image image, int tileWidth, int tileHeight) {
        if (image == null) throw new NullPointerException();
        int old = tiles;
        tileSet(image, tileWidth, tileHeight);
        size(cols * tileWidth, rows * tileHeight);
        if (tiles < old) {
            for (int i = 0; i < cells.length; i++) cells[i] = 0;
            anim = new int[4];
            animCount = 0;
        }
    }

    /** The static tile cell (col, row) shows now (animated tiles resolved), 0 for none. */
    int staticTileAt(int col, int row) {
        int t = cells[row * cols + col];
        return t >= 0 ? t : anim[-t - 1];
    }

    /** Pixel (px, py) of static tile t is solid (alpha not 0). */
    boolean solidTile(int t, int px, int py) {
        if (!LcduiBridge.hasAlpha(image)) return true;
        int sx = ((t - 1) % perRow) * cellW, sy = ((t - 1) / perRow) * cellH;
        return (LcduiBridge.pixels(image)[(sy + py) * image.getWidth() + sx + px] >>> 24) != 0;
    }

    private static int floorDiv(int a, int b) {
        int q = a / b;
        return (a % b != 0 && a < 0) ? q - 1 : q;
    }

    public final void paint(Graphics g) {
        if (g == null) throw new NullPointerException();
        if (!visible) return;
        int cw = g.getClipWidth(), ch = g.getClipHeight();
        if (cw <= 0 || ch <= 0) return;
        int clx = g.getClipX() - x, cly = g.getClipY() - y;
        int c0 = Math.max(0, floorDiv(clx, cellW)), c1 = Math.min(cols - 1, floorDiv(clx + cw - 1, cellW));
        int r0 = Math.max(0, floorDiv(cly, cellH)), r1 = Math.min(rows - 1, floorDiv(cly + ch - 1, cellH));
        for (int r = r0; r <= r1; r++) {
            int base = r * cols, dy = y + r * cellH;
            for (int c = c0; c <= c1; c++) {
                int t = cells[base + c];
                if (t < 0) t = anim[-t - 1];
                if (t == 0) continue;
                t--;
                g.drawRegion(image, (t % perRow) * cellW, (t / perRow) * cellH, cellW, cellH, 0, x + c * cellW, dy,
                        Graphics.TOP | Graphics.LEFT);
            }
        }
    }
}
