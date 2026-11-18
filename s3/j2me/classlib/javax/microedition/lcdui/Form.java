package javax.microedition.lcdui;

import java.util.Vector;

/**
 * Items one under another, full width, under the title. Up / down move the focus among the items that take it
 * (an item may use them first, e.g. a ChoiceGroup's rows); when the next one is more than a third of a screen away
 * they scroll instead, so long text can be read. Touch: a tap focuses and works the item, a drag scrolls. The focused
 * item's commands join the soft keys (before the form's).
 */
public class Form extends Screen {
    final Vector items = new Vector();
    private ItemStateListener stateListener;
    private int focus = -1;
    private boolean autoFocus = true;   // the user has not chosen: the first item in view gets the focus
    private boolean showFocus;          // scroll the focused item into view at the next paint
    private int scroll, top = -1, total;
    private int[] ys = new int[0], hs = new int[0];
    // touch
    private int pressY, pressScroll, pressItem = -1;
    private boolean dragged, itemDrag;

    public Form(String title) { this.title = title; }

    public Form(String title, Item[] items) {
        this(title);
        if (items != null) {
            for (int i = 0; i < items.length; i++) {
                if (items[i] == null) throw new NullPointerException();
                if (items[i].owner != null) throw new IllegalStateException();
            }
            for (int i = 0; i < items.length; i++) append(items[i]);
        }
    }

    public int append(Item item) {
        own(item);
        items.addElement(item);
        itemsChanged();
        return items.size() - 1;
    }

    public int append(String str) {
        if (str == null) throw new NullPointerException();
        return append(new StringItem(null, str));
    }

    public int append(Image img) {
        if (img == null) throw new NullPointerException();
        return append(new ImageItem(null, img, Item.LAYOUT_DEFAULT, null));
    }

    public void insert(int itemNum, Item item) {
        if (itemNum < 0 || itemNum > items.size()) throw new IndexOutOfBoundsException();
        own(item);
        items.insertElementAt(item, itemNum);
        if (focus >= itemNum) focus++;
        itemsChanged();
    }

    public void delete(int itemNum) {
        Item it = get(itemNum);
        items.removeElementAt(itemNum);
        it.owner = null;
        if (focus == itemNum) {
            it.focusOut();
            focus = -1;
            autoFocus = true;
        } else if (focus > itemNum) {
            focus--;
        }
        itemsChanged();
    }

    public void deleteAll() {
        for (int i = 0; i < items.size(); i++) ((Item) items.elementAt(i)).owner = null;
        items.removeAllElements();
        focus = -1;
        autoFocus = true;
        scroll = 0;
        itemsChanged();
    }

    public void set(int itemNum, Item item) {
        Item old = get(itemNum);
        if (item == old) return;
        own(item);
        old.owner = null;
        items.setElementAt(item, itemNum);
        if (focus == itemNum) {
            old.focusOut();
            if (item.focusable()) item.focusIn(0);
            else {
                focus = -1;
                autoFocus = true;
            }
        }
        itemsChanged();
    }

    public Item get(int itemNum) {
        if (itemNum < 0 || itemNum >= items.size()) throw new IndexOutOfBoundsException();
        return (Item) items.elementAt(itemNum);
    }

    public void setItemStateListener(ItemStateListener iListener) { stateListener = iListener; }

    public int size() { return items.size(); }

    public int getWidth() { return Item.fullWidth(); }

    public int getHeight() { return Display.H - top(); }

    // ---- inside the library
    private void own(Item item) {
        if (item == null) throw new NullPointerException();
        if (item.owner != null) throw new IllegalStateException("the item is on another screen");
        item.owner = this;
    }

    private Item item(int i) { return (Item) items.elementAt(i); }

    Item focused() { return focus >= 0 && focus < items.size() ? item(focus) : null; }

    void itemStateChanged(Item item) {
        if (stateListener != null) stateListener.itemStateChanged(item);
    }

    private void itemsChanged() { changed(); }

    private int top() { return top >= 0 ? top : Ui.contentTop(title); }

    private void setFocus(int i, int dir) {
        if (i == focus) return;
        Item old = focused();
        if (old != null) old.focusOut();
        focus = i;
        Item it = focused();
        if (it != null) it.focusIn(dir);
        changed();  // the soft keys show the item's commands
    }

    void focusItem(Item it) {
        int i = items.indexOf(it);
        if (i < 0) return;
        layout();
        if (it.focusable()) {
            autoFocus = false;
            setFocus(i, 0);
            showFocus = true;
        } else {
            scroll = ys[i] - 4;
        }
        invalidate();
    }

    void showing() {
        for (int i = 0; i < items.size(); i++) item(i).visible(true);
    }

    void hiding() {
        for (int i = 0; i < items.size(); i++) item(i).visible(false);
    }

    /** Places the items (ys, hs: from the content's top), fixes the focus if it went bad. */
    private void layout() {
        int n = items.size(), w = Item.fullWidth();
        if (ys.length < n) {
            ys = new int[n + 8];
            hs = new int[n + 8];
        }
        int y = 6;
        for (int i = 0; i < n; i++) {
            int h = item(i).height(w);
            ys[i] = y;
            hs[i] = h;
            if (h > 0) y += h + Ui.GAP;
        }
        total = y + 4;
        int vh = Display.H - top();
        if (focus >= n || (focus >= 0 && !item(focus).focusable())) {
            focus = -1;
            autoFocus = true;
            changed();
        }
        if (focus < 0 && autoFocus) {
            for (int i = 0; i < n; i++) {
                if (item(i).focusable() && ys[i] + hs[i] > scroll && ys[i] < scroll + vh) {
                    setFocus(i, 1);
                    break;
                }
            }
        }
    }

    private void keepFocusVisible() {
        Item it = focused();
        if (it == null) return;
        int vh = Display.H - top(), w = Item.fullWidth();
        int fy = ys[focus] + it.focusTop(w), fh = Math.min(it.focusHeight(w), vh - 8);
        if (fy - 4 < scroll) scroll = fy - 4;
        else if (fy + fh + 4 > scroll + vh) scroll = fy + fh + 4 - vh;
        scroll = Ui.clampScroll(scroll, total, vh);
    }

    void paintScreen(Graphics g, int w, int h) {
        top = paintTitle(g, title, w);
        int vh = h - top;
        layout();
        if (showFocus) {
            keepFocusVisible();
            showFocus = false;
        }
        scroll = Ui.clampScroll(scroll, total, vh);
        int iw = Item.fullWidth();
        for (int i = 0; i < items.size(); i++) {
            int y = top + ys[i] - scroll;
            if (hs[i] <= 0 || y + hs[i] <= top || y >= h) continue;
            g.setClip(0, top, w, vh);
            item(i).paint(g, Ui.M, y, iw, i == focus);
        }
        g.setClip(0, 0, w, h);
        Ui.scrollbar(g, top, vh, total, scroll);
    }

    boolean keyScreen(int type, int code) {
        layout();
        Item it = focused();
        if (it != null && it.keyItem(type, code)) {
            showFocus = true;
            return true;
        }
        if (type == 2) return false;
        int a = Canvas.action(code);
        if (a == Canvas.DOWN) return move(1);
        if (a == Canvas.UP) return move(-1);
        return false;
    }

    /** Up (-1) or down (1): the next item that takes the focus, or a scroll when it is far. */
    private boolean move(int dir) {
        int n = items.size(), vh = Display.H - top();
        int next = -1;
        if (focus >= 0) {
            for (int i = focus + dir; i >= 0 && i < n; i += dir) {
                if (item(i).focusable()) {
                    next = i;
                    break;
                }
            }
        } else {  // no focus: the first (last) one still ahead in view or below (above) it
            for (int i = dir > 0 ? 0 : n - 1; i >= 0 && i < n; i += dir) {
                if (!item(i).focusable()) continue;
                if (dir > 0 ? ys[i] + hs[i] > scroll : ys[i] < scroll + vh) {
                    next = i;
                    break;
                }
            }
        }
        if (next >= 0 && (dir > 0 ? ys[next] < scroll + vh + vh / 3 : ys[next] + hs[next] > scroll - vh / 3)) {
            autoFocus = false;
            setFocus(next, dir);
            showFocus = true;
            return true;
        }
        int s = Ui.clampScroll(scroll + dir * (vh * 2 / 3), total, vh);
        if (s == scroll) {
            if (next < 0) return false;
            autoFocus = false;
            setFocus(next, dir);
            showFocus = true;
            return true;
        }
        scroll = s;
        if (focus >= 0 && (ys[focus] + hs[focus] <= scroll || ys[focus] >= scroll + vh)) {
            setFocus(-1, 0);  // scrolled away from it: the next key starts from what is in view
            autoFocus = false;
        }
        return true;
    }

    /** Where item i starts on the screen now. */
    int screenY(int i) {
        layout();
        return top() + ys[i] - scroll;
    }

    private int itemAt(int y) {
        int cy = y - top() + scroll;
        for (int i = 0; i < items.size(); i++) if (hs[i] > 0 && cy >= ys[i] && cy < ys[i] + hs[i]) return i;
        return -1;
    }

    void pointerScreen(int type, int x, int y) {
        layout();
        int w = Item.fullWidth();
        if (type == 4) {
            pressY = y;
            pressScroll = scroll;
            dragged = false;
            itemDrag = false;
            pressItem = itemAt(y);
            if (pressItem >= 0) {
                Item it = item(pressItem);
                if (it.focusable()) {
                    autoFocus = false;
                    setFocus(pressItem, 0);
                }
                itemDrag = it.takesDrag();
                it.pointerItem(4, x - Ui.M, y - (top() + ys[pressItem] - scroll), w);
            }
        } else if (type == 6) {
            if (itemDrag && pressItem >= 0 && pressItem < items.size()) {
                item(pressItem).pointerItem(6, x - Ui.M, y - (top() + ys[pressItem] - scroll), w);
            } else {
                if (!dragged && Math.abs(y - pressY) > 8) dragged = true;
                if (dragged) scroll = Ui.clampScroll(pressScroll - (y - pressY), total, Display.H - top());
            }
        } else {
            if (pressItem >= 0 && pressItem < items.size() && (itemDrag || (!dragged && itemAt(y) == pressItem)))
                item(pressItem).pointerItem(5, x - Ui.M, y - (top() + ys[pressItem] - scroll), w);
            pressItem = -1;
        }
        invalidate();
    }

    Vector softCommands() {
        Item it = focused();
        if (it == null || it.itemCommands.isEmpty()) return commands;
        Vector v = new Vector();
        for (int i = 0; i < it.itemCommands.size(); i++) v.addElement(it.itemCommands.elementAt(i));
        for (int i = 0; i < commands.size(); i++) if (!v.contains(commands.elementAt(i))) v.addElement(commands.elementAt(i));
        return v;
    }

    void fireCommand(Command c) {
        Item it = focused();
        if (it != null && it.itemCommands.contains(c)) {
            if (it.commandListener != null) it.commandListener.commandAction(c, it);
            return;
        }
        super.fireCommand(c);
    }
}
