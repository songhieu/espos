package javax.microedition.lcdui.game;

import com.espos.j2me.Native;
import javax.microedition.lcdui.Canvas;
import javax.microedition.lcdui.Graphics;
import javax.microedition.lcdui.Image;
import javax.microedition.lcdui.LcduiBridge;

/** An off-screen buffer the size of the screen; flushGraphics shows it at once. */
public abstract class GameCanvas extends Canvas {
    public static final int UP_PRESSED = 1 << UP, DOWN_PRESSED = 1 << DOWN, LEFT_PRESSED = 1 << LEFT,
            RIGHT_PRESSED = 1 << RIGHT, FIRE_PRESSED = 1 << FIRE, GAME_A_PRESSED = 1 << GAME_A,
            GAME_B_PRESSED = 1 << GAME_B, GAME_C_PRESSED = 1 << GAME_C, GAME_D_PRESSED = 1 << GAME_D;

    private final Image buffer;
    private final boolean suppress;

    protected GameCanvas(boolean suppressKeyEvents) {
        suppress = suppressKeyEvents;
        // the screen's size, not getWidth(): games override it in subclasses whose fields are not set yet here
        // (one game's Canvas subclass returned 0, so the buffer could not be made)
        buffer = Image.createImage(Native.screenWidth(), Native.screenHeight());
        LcduiBridge.suppressKeys(this, suppressKeyEvents);
    }

    protected Graphics getGraphics() { return buffer.getGraphics(); }

    public int getKeyStates() { return Native.keyStates(); }

    public void paint(Graphics g) { g.drawImage(buffer, 0, 0, Graphics.LEFT | Graphics.TOP); }

    public void flushGraphics(int x, int y, int width, int height) {
        LcduiBridge.flush(this, buffer, x, y, width, height);
    }

    public void flushGraphics() { flushGraphics(0, 0, buffer.getWidth(), buffer.getHeight()); }
}
