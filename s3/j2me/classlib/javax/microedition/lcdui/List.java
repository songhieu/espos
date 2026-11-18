package javax.microedition.lcdui;

import java.util.Vector;

/**
 * A full-screen list: rows as white cards, the highlighted one in the accent colour. Up / down move (wrapping round),
 * left / right jump a page, OK (or a tap) selects: IMPLICIT runs the select command, EXCLUSIVE picks, MULTIPLE ticks.
 */
public class List extends Screen implements Choice {
    public static final Command SELECT_COMMAND = new Command("Chọn", Command.SCREEN, 0);

    private static final int ROW_GAP = 4;

    final Choices ch;
    private Command selectCommand = SELECT_COMMAND;
    private int focus;              // the highlighted row
    private int scroll, top = -1;
    private boolean showFocus = true;
    private int[] rowY = new int[0], rowH = new int[0];
    private int total;
    // touch
    private int pressY, pressScroll, pressRow = -1;
    private boolean dragged;

    public List(String title, int listType) { this(title, listType, new String[0], null); }

    public List(String title, int listType, String[] stringElements, Image[] imageElements) {
        if (listType != IMPLICIT && listType != EXCLUSIVE && listType != MULTIPLE) throw new IllegalArgumentException();
        if (stringElements == null) throw new NullPointerException();
        if (imageElements != null && imageElements.length != stringElements.length) throw new IllegalArgumentException();
        this.title = title;
        ch = new Choices(listType);
        for (int i = 0; i < stringElements.length; i++)
            ch.insert(i, stringElements[i], imageElements != null ? imageElements[i] : null);
    }

    // ---- Choice
    public int append(String stringPart, Image imagePart) {
        int i = ch.insert(ch.size(), stringPart, imagePart);
        elementsChanged();
        return i;
    }

    public void insert(int elementNum, String stringPart, Image imagePart) {
        ch.insert(elementNum, stringPart, imagePart);
        if (ch.size() > 1 && focus >= elementNum) focus++;
        elementsChanged();
    }

    public void delete(int elementNum) {
        ch.delete(elementNum);
        if (focus > elementNum || focus >= ch.size()) focus--;
        if (focus < 0) focus = 0;
        if (ch.type == IMPLICIT && ch.size() > 0) ch.select(focus, true);
        elementsChanged();
    }

    public void deleteAll() {
        ch.deleteAll();
        focus = 0;
        scroll = 0;
        elementsChanged();
    }

    public void set(int elementNum, String stringPart, Image imagePart) {
        ch.set(elementNum, stringPart, imagePart);
        invalidate();
    }

    public String getString(int elementNum) { return ch.string(elementNum); }
    public Image getImage(int elementNum) { return ch.image(elementNum); }
    public int size() { return ch.size(); }
    public boolean isSelected(int elementNum) { return ch.selected(elementNum); }
    public int getSelectedIndex() { return ch.selectedIndex(); }
    public int getSelectedFlags(boolean[] selectedArray_return) { return ch.getFlags(selectedArray_return); }

    public void setSelectedIndex(int elementNum, boolean selected) {
        ch.select(elementNum, selected);
        if (selected && ch.exclusive()) focusTo(elementNum);
        invalidate();
    }

    public void setSelectedFlags(boolean[] selectedArray) {
        ch.setFlags(selectedArray);
        if (ch.exclusive() && ch.size() > 0) focusTo(ch.selectedIndex());
        invalidate();
    }

    public void setFitPolicy(int fitPolicy) {
        if (fitPolicy < TEXT_WRAP_DEFAULT || fitPolicy > TEXT_WRAP_OFF) throw new IllegalArgumentException();
        ch.fit = fitPolicy;
    }

    public int getFitPolicy() { return ch.fit; }

    public void setFont(int elementNum, Font font) {
        ch.setFont(elementNum, font);
        invalidate();
    }

    public Font getFont(int elementNum) { return ch.font(elementNum); }

    // ---- List's own
    public void setSelectCommand(Command command) {
        if (ch.type != IMPLICIT) return;
        selectCommand = command;
        if (command != null && command != SELECT_COMMAND) addCommand(command);
        changed();
    }

    public void removeCommand(Command cmd) {
        if (cmd != null && cmd == selectCommand) {
            selectCommand = null;
            changed();
        }
        super.removeCommand(cmd);
    }

    Vector softCommands() {
        if (ch.type != IMPLICIT || selectCommand != SELECT_COMMAND || ch.size() == 0) return commands;
        Vector v = new Vector();
        v.addElement(SELECT_COMMAND);
        for (int i = 0; i < commands.size(); i++) v.addElement(commands.elementAt(i));
        return v;
    }

    void fireCommand(Command c) {
        if (c == selectCommand && ch.type == IMPLICIT && ch.size() > 0) ch.select(focus, true);
        super.fireCommand(c);
    }

    private void elementsChanged() {
        if (focus >= ch.size()) focus = Math.max(0, ch.size() - 1);
        changed();  // the select command comes and goes with the first / last element
    }

    private void focusTo(int i) {
        if (i < 0 || i >= ch.size()) return;
        focus = i;
        showFocus = true;
        if (ch.type == IMPLICIT) ch.select(i, true);
    }

    /** OK or a tap on row i. */
    private void activate(int i) {
        if (i < 0 || i >= ch.size()) return;
        focusTo(i);
        if (ch.type == IMPLICIT) {
            if (selectCommand != null) fireCommand(selectCommand);
        } else if (ch.type == EXCLUSIVE) {
            ch.select(i, true);
        } else {
            ch.select(i, !ch.selected(i));
        }
    }

    // ---- layout and drawing
    private boolean marks() { return ch.type != IMPLICIT; }

    private int top() { return top >= 0 ? top : Ui.contentTop(title); }

    private void layout() {
        int n = ch.size(), w = Display.W - 2 * Ui.M;
        if (rowY.length < n) {
            rowY = new int[n + 8];
            rowH = new int[n + 8];
        }
        int y = 4;
        for (int i = 0; i < n; i++) {
            rowY[i] = y;
            rowH[i] = ch.rowHeight(i, w, marks());
            y += rowH[i] + ROW_GAP;
        }
        total = y + 4;
    }

    private void keepFocusVisible() {
        if (!showFocus || focus < 0 || focus >= ch.size()) return;
        int vh = Display.H - top();
        int y = rowY[focus], h = Math.min(rowH[focus], vh - 8);
        if (y - 4 < scroll) scroll = y - 4;
        else if (y + h + 4 > scroll + vh) scroll = y + h + 4 - vh;
        scroll = Ui.clampScroll(scroll, total, vh);
        showFocus = false;
    }

    void paintScreen(Graphics g, int w, int h) {
        top = paintTitle(g, title, w);
        int vh = h - top;
        layout();
        keepFocusVisible();
        scroll = Ui.clampScroll(scroll, total, vh);
        g.setClip(0, top, w, vh);
        int rw = w - 2 * Ui.M;
        for (int i = 0; i < ch.size(); i++) {
            int y = top + rowY[i] - scroll;
            if (y + rowH[i] <= top || y >= h) continue;
            boolean hi = i == focus;
            if (!hi) {
                g.setColor(Screen.CARD);
                g.fillRoundRect(Ui.M, y, rw, rowH[i], 10, 10);
            }
            ch.paintRow(g, i, Ui.M, y, rw, rowH[i], marks(), hi);
        }
        g.setClip(0, 0, w, h);
        Ui.scrollbar(g, top, vh, total, scroll);
    }

    boolean keyScreen(int type, int code) {
        int n = ch.size();
        if (type == 2 || n == 0) return false;
        layout();
        int a = Canvas.action(code);
        if (a == Canvas.UP || a == Canvas.DOWN) {
            int d = a == Canvas.UP ? -1 : 1, next = focus + d;
            if (next < 0 || next >= n) {
                if (type == 3) return false;  // held: stops at the end
                next = (next + n) % n;
            }
            focusTo(next);
            return true;
        }
        if (a == Canvas.LEFT || a == Canvas.RIGHT) {
            int vh = Display.H - top(), d = a == Canvas.LEFT ? -1 : 1, i = focus, moved = 0;
            while (i + d >= 0 && i + d < n && moved + rowH[i + d] + ROW_GAP <= vh) {
                i += d;
                moved += rowH[i] + ROW_GAP;
            }
            if (i == focus && i + d >= 0 && i + d < n) i += d;
            focusTo(i);
            return true;
        }
        if (a == Canvas.FIRE && type == 1) {
            activate(focus);
            return true;
        }
        return false;
    }

    private int rowAt(int y) {
        int cy = y - top() + scroll;
        for (int i = 0; i < ch.size(); i++) if (cy >= rowY[i] && cy < rowY[i] + rowH[i]) return i;
        return -1;
    }

    void pointerScreen(int type, int x, int y) {
        layout();
        if (type == 4) {
            pressY = y;
            pressScroll = scroll;
            dragged = false;
            pressRow = rowAt(y);
            if (pressRow >= 0) focusTo(pressRow);
        } else if (type == 6) {
            if (!dragged && Math.abs(y - pressY) > 8) dragged = true;
            if (dragged) scroll = Ui.clampScroll(pressScroll - (y - pressY), total, Display.H - top());
            showFocus = false;
        } else {
            if (!dragged && pressRow >= 0 && rowAt(y) == pressRow) activate(pressRow);
            pressRow = -1;
            showFocus = false;
        }
        invalidate();
    }
}
