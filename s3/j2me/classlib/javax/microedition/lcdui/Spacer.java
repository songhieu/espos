package javax.microedition.lcdui;

/** Empty room on a Form (no label, no commands). */
public class Spacer extends Item {
    private int minW, minH;

    public Spacer(int minWidth, int minHeight) {
        super(null);
        setMinimumSize(minWidth, minHeight);
    }

    public void setMinimumSize(int minWidth, int minHeight) {
        if (minWidth < 0 || minHeight < 0) throw new IllegalArgumentException();
        minW = minWidth;
        minH = minHeight;
        changed();
    }

    public int getMinimumWidth() { return minW; }

    public void addCommand(Command cmd) { throw new IllegalStateException(); }

    public void setDefaultCommand(Command cmd) { throw new IllegalStateException(); }

    public void setLabel(String label) { throw new IllegalStateException(); }

    int contentHeight(int w) { return minH; }

    void paintContent(Graphics g, int x, int y, int w, int h, boolean focused) {}
}
