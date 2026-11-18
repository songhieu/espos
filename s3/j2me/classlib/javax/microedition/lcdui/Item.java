package javax.microedition.lcdui;

import java.util.Vector;

/**
 * An item of a Form (or an Alert's indicator). Forms here lay items out one under another, full width: the label
 * (small, bold) on its own line(s), the content under it. Interactive items sit on a white card; the focused one gets
 * an accent border and its commands join the screen's soft keys.
 */
public abstract class Item {
    public static final int LAYOUT_DEFAULT = 0, LAYOUT_LEFT = 1, LAYOUT_RIGHT = 2, LAYOUT_CENTER = 3, LAYOUT_TOP = 0x10,
            LAYOUT_BOTTOM = 0x20, LAYOUT_VCENTER = 0x30, LAYOUT_NEWLINE_BEFORE = 0x100, LAYOUT_NEWLINE_AFTER = 0x200,
            LAYOUT_SHRINK = 0x400, LAYOUT_EXPAND = 0x800, LAYOUT_VSHRINK = 0x1000, LAYOUT_VEXPAND = 0x2000,
            LAYOUT_2 = 0x4000;
    public static final int PLAIN = 0, HYPERLINK = 1, BUTTON = 2;

    private static final int LAYOUT_BITS = 0x3 | 0x30 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000;

    String label;
    int layout;
    final Vector itemCommands = new Vector();
    Command defaultCommand;
    ItemCommandListener commandListener;
    int prefW = -1, prefH = -1;
    /** The Form (or Alert) holding it; null when free. */
    Screen owner;

    private Vector labelLines;
    private int labelW = -1;

    Item(String label) { this.label = label; }

    public String getLabel() { return label; }

    public void setLabel(String label) {
        if (owner instanceof Alert) throw new IllegalStateException();
        this.label = label;
        labelLines = null;
        changed();
    }

    public int getLayout() { return layout; }

    public void setLayout(int layout) {
        if (owner instanceof Alert) throw new IllegalStateException();
        if ((layout & ~LAYOUT_BITS) != 0) throw new IllegalArgumentException();
        this.layout = layout;
        changed();
    }

    public void addCommand(Command cmd) {
        if (cmd == null) throw new NullPointerException();
        if (owner instanceof Alert) throw new IllegalStateException();
        if (!itemCommands.contains(cmd)) {
            itemCommands.addElement(cmd);
            commandsChanged();
        }
    }

    public void removeCommand(Command cmd) {
        if (itemCommands.removeElement(cmd)) {
            if (cmd == defaultCommand) defaultCommand = null;
            commandsChanged();
        }
    }

    public void setItemCommandListener(ItemCommandListener l) {
        if (owner instanceof Alert) throw new IllegalStateException();
        commandListener = l;
    }

    public void setDefaultCommand(Command cmd) {
        if (owner instanceof Alert) throw new IllegalStateException();
        defaultCommand = cmd;
        if (cmd != null) addCommand(cmd);
        else commandsChanged();
    }

    public int getPreferredWidth() {
        return prefW >= 0 ? Math.max(prefW, getMinimumWidth()) : fullWidth();
    }

    public int getPreferredHeight() {
        return prefH >= 0 ? Math.max(prefH, getMinimumHeight()) : height(getPreferredWidth());
    }

    public int getMinimumWidth() { return Math.min(fullWidth(), 40); }

    public int getMinimumHeight() { return height(fullWidth()); }

    public void setPreferredSize(int width, int height) {
        if (owner instanceof Alert) throw new IllegalStateException();
        if (width < -1 || height < -1) throw new IllegalArgumentException();
        prefW = width;
        prefH = height;
        changed();
    }

    /** The app says the item's state changed: its Form's ItemStateListener hears it. */
    public void notifyStateChanged() {
        if (!(owner instanceof Form)) throw new IllegalStateException();
        ((Form) owner).itemStateChanged(this);
    }

    // ---- inside the library

    /** The width items get on a Form. */
    static int fullWidth() { return Display.W - 2 * Ui.M; }

    /** The user changed the item (a choice, a gauge, text): the Form's listener hears it. */
    void stateChanged() {
        if (owner instanceof Form) ((Form) owner).itemStateChanged(this);
    }

    /** Its look or size changed: the screen lays out and paints again. */
    void changed() {
        if (owner != null) owner.invalidate();
    }

    /** Its commands changed: the soft keys too (when it has the focus). */
    void commandsChanged() {
        if (owner != null) owner.changed();
    }

    private Vector labelLines(int w) {
        if (labelLines == null || labelW != w) {
            labelLines = Screen.wrap(label, Ui.label(), w - 4);
            labelW = w;
        }
        return labelLines;
    }

    int labelHeight(int w) {
        if (label == null || label.length() == 0) return 0;
        return Ui.height(labelLines(w), Ui.label()) + 3;
    }

    /** The whole item's height at width w: label and content. */
    int height(int w) {
        int h = labelHeight(w) + contentHeight(w);
        return prefH > h ? prefH : h;
    }

    /** Draws the label then the content (x, y: its top-left; w: its width). */
    void paint(Graphics g, int x, int y, int w, boolean focused) {
        int lh = labelHeight(w);
        if (lh > 0) Ui.drawLines(g, labelLines(w), Ui.label(), x + 2, y, focused ? Screen.ACCENT : Screen.MUTED);
        paintContent(g, x, y + lh, w, contentHeight(w), focused);
    }

    /** The content's height (under the label) at width w. */
    abstract int contentHeight(int w);

    abstract void paintContent(Graphics g, int x, int y, int w, int h, boolean focused);

    /** Can take the focus: interactive, or has commands. */
    boolean focusable() { return !itemCommands.isEmpty(); }

    /** A key while focused (1 pressed, 2 released, 3 repeated); true when used. OK runs the default command. */
    boolean keyItem(int type, int code) {
        if (type == 1 && Canvas.action(code) == Canvas.FIRE) return fireDefault();
        return false;
    }

    /** The default command, or the only command: to its ItemCommandListener. */
    boolean fireDefault() {
        Command c = defaultCommand;
        if (c == null && itemCommands.size() == 1) c = (Command) itemCommands.elementAt(0);
        if (c == null) return false;
        if (commandListener != null) commandListener.commandAction(c, this);
        return true;
    }

    /**
     * A touch on the item: x, y from its top-left (the label included), w its width; 4 pressed, 5 released (a tap: the
     * Form only sends it when the finger did not scroll), 6 dragged (only when takesDrag).
     */
    void pointerItem(int type, int x, int y, int w) {
        if (type == 5 && focusable()) fireDefault();
    }

    /** A drag starting on it belongs to it (an interactive Gauge) rather than scrolling the Form. */
    boolean takesDrag() { return false; }

    /** Took the focus, moving down (dir 1), up (-1), or by touch / the app (0). */
    void focusIn(int dir) {}

    void focusOut() {}

    /** Shown or hidden with its screen (CustomItem's showNotify / hideNotify). */
    void visible(boolean on) {}

    /** The part to keep in view while focused, from the item's top: its top, then (focusHeight) its height. */
    int focusTop(int w) { return 0; }

    int focusHeight(int w) { return height(w); }
}
