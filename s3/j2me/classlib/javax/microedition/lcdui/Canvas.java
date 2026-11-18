package javax.microedition.lcdui;

/**
 * Key codes as Nokia and Sony Ericsson phones send them: digits, * and #, the arrows -1..-4, OK -5, soft keys
 * -6 (left) and -7 (right). The device's on-screen keypad sends these (src/j2me/app_j2me.cpp).
 */
public abstract class Canvas extends Displayable {
    public static final int UP = 1, DOWN = 6, LEFT = 2, RIGHT = 5, FIRE = 8;
    public static final int GAME_A = 9, GAME_B = 10, GAME_C = 11, GAME_D = 12;
    public static final int KEY_NUM0 = 48, KEY_NUM1 = 49, KEY_NUM2 = 50, KEY_NUM3 = 51, KEY_NUM4 = 52, KEY_NUM5 = 53,
            KEY_NUM6 = 54, KEY_NUM7 = 55, KEY_NUM8 = 56, KEY_NUM9 = 57, KEY_STAR = 42, KEY_POUND = 35;

    static final int K_UP = -1, K_DOWN = -2, K_LEFT = -3, K_RIGHT = -4, K_FIRE = -5, K_SOFT_LEFT = -6, K_SOFT_RIGHT = -7;

    boolean fullScreen;
    boolean suppressKeys;  // GameCanvas(true)

    protected Canvas() {}

    public boolean isDoubleBuffered() { return true; }
    public boolean hasPointerEvents() { return true; }
    public boolean hasPointerMotionEvents() { return true; }
    public boolean hasRepeatEvents() { return true; }

    public int getKeyCode(int gameAction) {
        switch (gameAction) {
            case UP: return K_UP;
            case DOWN: return K_DOWN;
            case LEFT: return K_LEFT;
            case RIGHT: return K_RIGHT;
            case FIRE: return K_FIRE;
            case GAME_A: return KEY_NUM1;
            case GAME_B: return KEY_NUM3;
            case GAME_C: return KEY_NUM7;
            case GAME_D: return KEY_NUM9;
        }
        throw new IllegalArgumentException();
    }

    public String getKeyName(int keyCode) {
        if (keyCode >= KEY_NUM0 && keyCode <= KEY_NUM9) return String.valueOf((char) keyCode);
        switch (keyCode) {
            case KEY_STAR: return "*";
            case KEY_POUND: return "#";
            case K_UP: return "Up";
            case K_DOWN: return "Down";
            case K_LEFT: return "Left";
            case K_RIGHT: return "Right";
            case K_FIRE: return "Select";
            case K_SOFT_LEFT: return "Soft1";
            case K_SOFT_RIGHT: return "Soft2";
        }
        throw new IllegalArgumentException();
    }

    public int getGameAction(int keyCode) { return action(keyCode); }

    static int action(int keyCode) {
        switch (keyCode) {
            case K_UP: case KEY_NUM2: return UP;
            case K_DOWN: case KEY_NUM8: return DOWN;
            case K_LEFT: case KEY_NUM4: return LEFT;
            case K_RIGHT: case KEY_NUM6: return RIGHT;
            case K_FIRE: case KEY_NUM5: return FIRE;
            case KEY_NUM1: return GAME_A;
            case KEY_NUM3: return GAME_B;
            case KEY_NUM7: return GAME_C;
            case KEY_NUM9: return GAME_D;
        }
        return 0;
    }

    public void setFullScreenMode(boolean mode) {
        // The soft keys are on the device's keypad, outside the game's screen: full screen or not, the size stays the
        // same, so sizeChanged is not called (games call this in their constructor, before their fields are set).
        if (fullScreen != mode) {
            fullScreen = mode;
            if (isShown()) repaint();
        }
    }

    protected void keyPressed(int keyCode) {}
    protected void keyRepeated(int keyCode) {}
    protected void keyReleased(int keyCode) {}
    protected void pointerPressed(int x, int y) {}
    protected void pointerReleased(int x, int y) {}
    protected void pointerDragged(int x, int y) {}
    protected void showNotify() {}
    protected void hideNotify() {}

    protected abstract void paint(Graphics g);

    public final void repaint(int x, int y, int width, int height) {
        Display d = Display.instance;
        if (d != null && width > 0 && height > 0) d.requestRepaint(this, x, y, width, height);
    }

    public final void repaint() { repaint(0, 0, getWidth(), getHeight()); }

    public final void serviceRepaints() {
        Display d = Display.instance;
        if (d != null) d.serviceRepaints(this);
    }

    // ---- the display loop
    void paintFrame(Graphics g) { paint(g); }

    void key(int type, int code) {
        if (suppressKeys && action(code) != 0) return;  // game keys only through getKeyStates
        if (type == 1) keyPressed(code);
        else if (type == 2) keyReleased(code);
        else keyRepeated(code);
    }

    void pointer(int type, int x, int y) {
        if (type == 4) pointerPressed(x, y);
        else if (type == 5) pointerReleased(x, y);
        else pointerDragged(x, y);
    }

    void showing() { showNotify(); }
    void hiding() { hideNotify(); }
}
