package javax.microedition.lcdui;

import java.util.Vector;

public abstract class Displayable {
    String title;
    Ticker ticker;
    final Vector commands = new Vector();
    CommandListener listener;
    boolean shown;

    Displayable() {}

    public String getTitle() { return title; }

    public void setTitle(String s) {
        title = s;
        changed();
    }

    public Ticker getTicker() { return ticker; }
    public void setTicker(Ticker t) { ticker = t; }
    public boolean isShown() { return shown && Display.instance != null && Display.instance.current == this; }

    public void addCommand(Command cmd) {
        if (cmd == null) throw new NullPointerException();
        if (!commands.contains(cmd)) {
            // kept in priority order (a lower number first), as phones place them
            int i = 0;
            while (i < commands.size() && ((Command) commands.elementAt(i)).getPriority() <= cmd.getPriority()) i++;
            commands.insertElementAt(cmd, i);
            changed();
        }
    }

    public void removeCommand(Command cmd) {
        if (commands.removeElement(cmd)) changed();
    }

    public void setCommandListener(CommandListener l) { listener = l; }

    public int getWidth() { return Display.W; }
    public int getHeight() { return Display.H; }

    protected void sizeChanged(int w, int h) {}

    // ---- what the display loop calls (Canvas and Screen fill these in)

    /** Paints the whole screen into g (a full-screen Graphics, reset). */
    abstract void paintFrame(Graphics g);

    /** A key (Native.poll types 1 pressed, 2 released, 3 repeated) the soft keys did not take. */
    void key(int type, int code) {}

    /** A touch on the screen (4 pressed, 5 released, 6 dragged). */
    void pointer(int type, int x, int y) {}

    void showing() {}
    void hiding() {}

    /** The commands the soft keys offer now (Screens add their own, e.g. a focused item's). */
    Vector softCommands() { return commands; }

    /** A soft key or the command menu chose c. */
    void fireCommand(Command c) {
        if (listener != null) listener.commandAction(c, this);
    }

    /** Commands or the title changed: the soft key labels and the screen are drawn again. */
    void changed() {
        Display d = Display.instance;
        if (d != null && d.current == this) d.commandsChanged();
    }
}
