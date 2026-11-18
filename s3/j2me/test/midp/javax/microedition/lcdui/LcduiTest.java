package javax.microedition.lcdui;

import com.espos.j2me.Native;
import java.util.Date;
import java.util.TimeZone;
import java.util.Vector;

/**
 * Form, items, List, Alert, TextBox: focus, keys, commands on the soft keys, listeners, text input, and every screen
 * painted once (exceptions surface). HOST ONLY: it reads what the stand-in Native recorded (soft key labels, tones),
 * so run it with j2me/test/midp/run-host.sh. The stand-in measures text at 6 px a character, fonts 12 / 14 / 16 px.
 */
public class LcduiTest {
    static int n, fails;

    static void eq(String what, Object got, Object want) {
        n++;
        if (got == null ? want != null : !got.equals(want)) {
            fails++;
            System.out.println("FAIL " + what + ": got " + got + " want " + want);
        }
    }

    static void eq(String what, long got, long want) { eq(what, new Long(got), new Long(want)); }

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

    static Display d;
    static final Vector heard = new Vector();  // what the listeners heard, as strings

    static void press(Displayable s, int code) {
        s.key(1, code);
        s.key(2, code);
    }

    static void type(Displayable s, String keys) {
        for (int i = 0; i < keys.length(); i++) press(s, keys.charAt(i));
    }

    /** Paints the current screen into the display's pixels, letting exceptions through (paintPending eats them). */
    static void paint() {
        d.sg.reset();
        d.current.paintFrame(d.sg);
    }

    static String last() { return heard.isEmpty() ? null : (String) heard.lastElement(); }

    public static void main(String[] a) throws Exception {
        DisplayHost.start(new com.espos.j2me.Lifecycle() {
            public void pause() {}
            public void resume() {}
            public void destroy() {}
        });
        d = Display.instance;
        wrapping();
        form();
        textInput();
        list();
        alert();
        others();
        if (fails == 0) System.out.println("LcduiTest: OK (" + n + " checks)");
        else System.out.println("LcduiTest: FAILED " + fails + " of " + n);
        System.exit(fails == 0 ? 0 : 1);
    }

    static void wrapping() {
        Font f = Font.getDefaultFont();
        Vector l = Screen.wrap("abc def ghi", f, 40);  // 6 px a character: 6 fit in 40
        eq("wrap lines", l.size(), 3);
        eq("wrap line 2", l.elementAt(1), "def");
        Vector w = Screen.wrap("abcdefghijklmnop", f, 40);
        StringBuffer all = new StringBuffer();
        for (int i = 0; i < w.size(); i++) all.append((String) w.elementAt(i));
        eq("a long word is split, nothing lost", all.toString(), "abcdefghijklmnop");
        ok("no dots added", all.toString().indexOf("...") < 0 && all.toString().indexOf('…') < 0);
    }

    static void form() {
        heard.removeAllElements();
        final Form f = new Form("Cài đặt");
        StringItem intro = new StringItem("Giới thiệu", "Một dòng chữ khá dài để thử xuống dòng mà không bị cắt bớt");
        final TextField tf = new TextField("Tên", "", 10, TextField.ANY);
        final ChoiceGroup cg = new ChoiceGroup("Âm thanh", Choice.EXCLUSIVE, new String[] {"Bật", "Tắt"}, null);
        final Gauge g = new Gauge("Âm lượng", true, 10, 5);
        final StringItem btn = new StringItem(null, "Lưu", Item.BUTTON);
        final Command save = new Command("Lưu", Command.ITEM, 1), back = new Command("Quay lại", Command.BACK, 1);
        btn.setDefaultCommand(save);
        btn.setItemCommandListener(new ItemCommandListener() {
            public void commandAction(Command c, Item item) { heard.addElement("item " + c.getLabel()); }
        });
        f.append(intro);
        f.append(tf);
        f.append(cg);
        f.append(g);
        f.append(btn);
        f.addCommand(back);
        f.setCommandListener(new CommandListener() {
            public void commandAction(Command c, Displayable s) { heard.addElement("form " + c.getLabel()); }
        });
        f.setItemStateListener(new ItemStateListener() {
            public void itemStateChanged(Item item) {
                heard.addElement("state " + (item == tf ? "tf" : item == cg ? "cg" : item == g ? "g" : "?"));
            }
        });
        ok("owner set", tf.owner == f);
        throwsIt("an item on two forms", IllegalStateException.class, new Run() {
            public void run() { new Form("x").append(tf); }
        });
        d.setCurrent(f);
        paint();
        ok("first interactive item in view has the focus", f.focused() == tf);
        eq("soft right: back", Native.softRight, "Quay lại");
        eq("soft left: nothing", Native.softLeft, "");
        press(f, Canvas.K_DOWN);
        ok("down: the choice group", f.focused() == cg);
        press(f, Canvas.K_DOWN);
        ok("down inside the choice group keeps it", f.focused() == cg);
        press(f, Canvas.K_FIRE);
        eq("OK picks row 2", cg.getSelectedIndex(), 1);
        eq("state listener: choice", last(), "state cg");
        press(f, Canvas.K_DOWN);
        ok("down leaves the group: gauge", f.focused() == g);
        press(f, Canvas.K_RIGHT);
        eq("right: gauge 6", g.getValue(), 6);
        eq("state listener: gauge", last(), "state g");
        press(f, Canvas.K_DOWN);
        ok("down: the button", f.focused() == btn);
        eq("the item's command on the left key", Native.softLeft, "Lưu");
        eq("back still on the right", Native.softRight, "Quay lại");
        press(f, Canvas.K_FIRE);
        eq("OK runs the default command", last(), "item Lưu");
        f.fireCommand(save);
        eq("soft key with the item's command", last(), "item Lưu");
        f.fireCommand(back);
        eq("soft key with the form's command", last(), "form Quay lại");
        press(f, Canvas.K_DOWN);
        ok("down at the end stays", f.focused() == btn);
        // up: gauge, the group (from its last row), its first row, the text field
        press(f, Canvas.K_UP);
        press(f, Canvas.K_UP);
        ok("up enters the group", f.focused() == cg);
        press(f, Canvas.K_UP);
        press(f, Canvas.K_UP);
        ok("up past the group's first row: text field", f.focused() == tf);
        eq("left key empty again", Native.softLeft, "");
        paint();
        // setCurrentItem
        d.setCurrentItem(g);
        ok("setCurrentItem focuses", f.focused() == g);
        // touch: a tap on the choice group's first row picks it
        paint();
        f.focusItem(cg);
        paint();
        int rowY = f.screenY(f.items.indexOf(cg)) + cg.labelHeight(Item.fullWidth()) + 3 + 5;
        f.pointerScreen(4, 40, rowY);
        f.pointerScreen(5, 40, rowY);
        eq("tap picks row 1", cg.getSelectedIndex(), 0);
        // a drag scrolls instead of tapping
        f.pointerScreen(4, 40, 300);
        f.pointerScreen(6, 40, 200);
        f.pointerScreen(5, 40, 200);
        eq("a drag does not pick", cg.getSelectedIndex(), 0);
        // delete and insert keep the focus right
        f.delete(0);
        ok("delete before the focus", f.focused() == cg && f.size() == 4);
        f.insert(0, new Spacer(10, 10));
        ok("insert before the focus", f.focused() == cg);
        throwsIt("spacer label", IllegalStateException.class, new Run() {
            public void run() { new Spacer(1, 1).setLabel("x"); }
        });
        f.deleteAll();
        ok("deleteAll frees the items", tf.owner == null && f.size() == 0 && f.focused() == null);
        paint();
    }

    static void textInput() throws Exception {
        heard.removeAllElements();
        Form f = new Form("Nhập");
        TextField tf = new TextField("Tên", "", 10, TextField.ANY);
        f.append(tf);
        d.setCurrent(f);
        paint();
        type(f, "44");
        eq("4 4: h", tf.getString(), "h");
        type(f, "333");
        eq("3 3 3: f", tf.getString(), "hf");
        press(f, '#');
        type(f, "2");
        eq("# then 2: A", tf.getString(), "hfA");
        press(f, '*');
        eq("* deletes", tf.getString(), "hf");
        press(f, '#');
        type(f, "7");
        eq("123 mode: digit", tf.getString(), "hf7");
        press(f, Canvas.K_LEFT);
        eq("left moves the caret", tf.getCaretPosition(), 2);
        press(f, '*');
        eq("* deletes before the caret", tf.getString(), "h7");
        press(f, '#');  // back to abc
        type(f, "0");
        eq("0: space", tf.getString(), "h 7");
        // the same key after the pause: a new letter
        type(f, "2");
        Thread.sleep(TextEditor.TAP_MS + 50);
        type(f, "2");
        eq("pause then the same key: two letters", tf.getString(), "h aa7");
        // held key: the digit
        tf.setString("");
        press(f, '5');
        f.key(3, '5');
        eq("held 5: digit 5", tf.getString(), "5");
        // maxSize
        tf.setString("");
        press(f, '#');
        press(f, '#');  // 123
        type(f, "123456789012");
        eq("maxSize 10", tf.getString(), "1234567890");
        throwsIt("setString past maxSize", IllegalArgumentException.class, new Run() {
            public void run() { new TextField(null, "", 3, TextField.ANY).setString("abcd"); }
        });
        // NUMERIC
        TextField num = new TextField("Số", "", 5, TextField.NUMERIC);
        f.append(num);
        f.focusItem(num);
        type(f, "12");
        press(f, '#');
        eq("numeric: # adds the minus", num.getString(), "-12");
        press(f, '#');
        eq("numeric: # takes it away", num.getString(), "12");
        throwsIt("numeric refuses letters", IllegalArgumentException.class, new Run() {
            public void run() { new TextField(null, "12a", 5, TextField.NUMERIC); }
        });
        TextField dec = new TextField(null, "", 8, TextField.DECIMAL);
        f.append(dec);
        f.focusItem(dec);
        type(f, "3#14#");
        eq("decimal: point then sign", dec.getString(), "-3.14");
        // PASSWORD shows stars once the tap is over
        TextField pw = new TextField(null, "abc", 8, TextField.ANY | TextField.PASSWORD);
        eq("password shown as stars", pw.ed.shown(), "***");
        eq("password keeps the text", pw.getString(), "abc");
        // UNEDITABLE takes no letters
        TextField ro = new TextField(null, "x", 8, TextField.ANY | TextField.UNEDITABLE);
        f.append(ro);
        f.focusItem(ro);
        ok("uneditable cannot take the focus by itself", !ro.focusable() || ro.getString().equals("x"));
        // the editor's lines keep every character so the caret maps: "hello world" at 40 px -> "hello " + "world"
        TextEditor ed = new TextEditor("hello world", 50, TextField.ANY);
        Font font = Font.getDefaultFont();
        eq("two lines", ed.height(font, 40), 2 * font.getHeight());
        eq("caret on line 2", ed.caretY(), font.getHeight());
        ok("caret up", ed.lineMove(-1));
        eq("caret up keeps the x", ed.caret, 5);
        ok("no line above", !ed.lineMove(-1));
        // TextBox
        TextBox tb = new TextBox("Ghi chú", "xin chao", 50, TextField.ANY);
        d.setCurrent(tb);
        paint();
        type(tb, "#2");
        eq("text box typing", tb.getString(), "xin chaoA");
        press(tb, Canvas.K_UP);
        paint();
        eq("text box size", tb.size(), 9);
        tb.insert("!", 0);
        eq("text box insert", tb.getString(), "!xin chaoA");
        tb.delete(0, 4);
        eq("text box delete", tb.getString(), " chaoA");
    }

    static void list() {
        heard.removeAllElements();
        final List l = new List("Trò chơi", List.IMPLICIT, new String[] {"Chơi", "Điểm cao", "Thoát"}, null);
        Command exit = new Command("Thoát", Command.EXIT, 1);
        l.addCommand(exit);
        l.setCommandListener(new CommandListener() {
            public void commandAction(Command c, Displayable s) {
                heard.addElement((c == List.SELECT_COMMAND ? "select " : c.getLabel() + " ") + l.getSelectedIndex());
            }
        });
        d.setCurrent(l);
        paint();
        eq("implicit: select on the left key", Native.softLeft, "Chọn");
        eq("exit on the right key", Native.softRight, "Thoát");
        press(l, Canvas.K_DOWN);
        eq("down selects (implicit)", l.getSelectedIndex(), 1);
        press(l, Canvas.K_FIRE);
        eq("OK fires select", last(), "select 1");
        press(l, Canvas.K_UP);
        press(l, Canvas.K_UP);
        eq("up wraps round", l.getSelectedIndex(), 2);
        l.fireCommand(List.SELECT_COMMAND);
        eq("left soft key fires select", last(), "select 2");
        l.setSelectCommand(null);
        eq("no select command: left key empty", Native.softLeft, "");
        int before = heard.size();
        press(l, Canvas.K_FIRE);
        eq("no select command: OK does nothing", heard.size(), before);
        l.delete(2);
        eq("delete the selected: selection moves", l.getSelectedIndex(), 1);
        l.append("Mới", Image.createImage(8, 8));
        paint();
        // tap on row 1
        l.pointerScreen(4, 50, Ui.contentTop("Trò chơi") + 4 + 5);
        l.pointerScreen(5, 50, Ui.contentTop("Trò chơi") + 4 + 5);
        eq("tap selects row 1", l.getSelectedIndex(), 0);

        List m = new List("Chọn nhiều", List.MULTIPLE, new String[] {"A", "B", "C"}, null);
        d.setCurrent(m);
        paint();
        press(m, Canvas.K_FIRE);
        press(m, Canvas.K_DOWN);
        press(m, Canvas.K_DOWN);
        press(m, Canvas.K_FIRE);
        boolean[] flags = new boolean[3];
        eq("multiple: two ticked", m.getSelectedFlags(flags), 2);
        ok("multiple: A and C", flags[0] && !flags[1] && flags[2]);
        eq("multiple: no selected index", m.getSelectedIndex(), -1);
        press(m, Canvas.K_FIRE);
        ok("multiple: OK again unticks", !m.isSelected(2));

        List x = new List("Một", List.EXCLUSIVE);
        eq("exclusive empty: none", x.getSelectedIndex(), -1);
        x.append("A", null);
        eq("exclusive: first append selected", x.getSelectedIndex(), 0);
        x.append("B", null);
        x.setSelectedIndex(1, true);
        x.setSelectedIndex(1, false);
        eq("exclusive: unselect ignored", x.getSelectedIndex(), 1);
        x.setSelectedFlags(new boolean[] {false, false});
        eq("exclusive: no true flag picks the first", x.getSelectedIndex(), 0);
        throwsIt("flags too short", IllegalArgumentException.class, new Run() {
            public void run() { new List("x", List.MULTIPLE, new String[] {"a", "b"}, null).getSelectedFlags(new boolean[1]); }
        });
        throwsIt("list type POPUP", IllegalArgumentException.class, new Run() {
            public void run() { new List("x", Choice.POPUP); }
        });
        throwsIt("null element", NullPointerException.class, new Run() {
            public void run() { new List("x", List.IMPLICIT).append(null, null); }
        });
        throwsIt("bad index", IndexOutOfBoundsException.class, new Run() {
            public void run() { new List("x", List.IMPLICIT).getString(0); }
        });
    }

    static void alert() {
        heard.removeAllElements();
        Form home = new Form("Nhà");
        d.setCurrent(home);
        paint();
        int tones = Native.tones;
        Alert al = new Alert("Lỗi", "Không mở được tệp", null, AlertType.ERROR);
        d.setCurrent(al, home);
        ok("after is home", al.after == home);
        ok("the error sound played", Native.tones == tones + 1);
        eq("dismiss on the left key", Native.softLeft, "Đóng");
        paint();
        int dim = 0xFF000000 | ((Screen.BG >> 1) & 0x7F7F7F);
        eq("the screen behind dimmed once", d.screen[0], dim);
        paint();
        eq("not dimmed twice", d.screen[0], dim);
        al.dismiss();
        ok("dismiss goes back", d.getCurrent() == home);
        // OK key dismisses too
        d.setCurrent(al);
        ok("setCurrent(alert): after is the current", al.after == home && d.getCurrent() == al);
        press(al, Canvas.K_FIRE);
        ok("OK dismisses", d.getCurrent() == home);
        // with a listener: it hears DISMISS_COMMAND and the alert stays
        al.setCommandListener(new CommandListener() {
            public void commandAction(Command c, Displayable s) {
                heard.addElement(c == Alert.DISMISS_COMMAND ? "dismiss" : c.getLabel());
            }
        });
        d.setCurrent(al);
        al.dismiss();
        eq("listener hears dismiss", last(), "dismiss");
        ok("the listener decides", d.getCurrent() == al);
        // commands
        al.addCommand(Alert.DISMISS_COMMAND);
        eq("adding DISMISS_COMMAND does nothing", al.softCommandsCount(), 0);
        Command yes = new Command("Có", Command.OK, 1), no = new Command("Không", Command.CANCEL, 1);
        al.addCommand(yes);
        eq("one command: the timeout stays", al.getTimeout(), al.getDefaultTimeout());
        al.addCommand(no);
        eq("two commands: modal", al.getTimeout(), Alert.FOREVER);
        d.commandsChanged();
        eq("yes on the left", Native.softLeft, "Có");
        eq("no on the right", Native.softRight, "Không");
        al.fireCommand(yes);
        eq("listener hears yes", last(), "Có");
        throwsIt("timeout 0", IllegalArgumentException.class, new Run() {
            public void run() { new Alert("x").setTimeout(0); }
        });
        // an indicator: no label, not interactive
        final Alert busy = new Alert("Đang tải", "Chờ một chút", null, null);
        busy.setIndicator(new Gauge(null, false, Gauge.INDEFINITE, Gauge.CONTINUOUS_RUNNING));
        throwsIt("interactive indicator", IllegalArgumentException.class, new Run() {
            public void run() { busy.setIndicator(new Gauge(null, true, 10, 1)); }
        });
        throwsIt("labelled indicator", IllegalArgumentException.class, new Run() {
            public void run() { busy.setIndicator(new Gauge("x", false, 10, 1)); }
        });
        d.setCurrent(busy, home);
        paint();
        // a long text scrolls with the keys
        StringBuffer sb = new StringBuffer();
        for (int i = 0; i < 80; i++) sb.append("chữ dài ");
        Alert big = new Alert("Dài", sb.toString(), Image.createImage(20, 20), AlertType.INFO);
        big.setTimeout(Alert.FOREVER);
        d.setCurrent(big, home);
        paint();
        ok("long text scrolls", big.keyScreen(1, Canvas.K_DOWN));
        paint();
    }

    static void others() {
        // Gauge rules
        throwsIt("interactive indefinite", IllegalArgumentException.class, new Run() {
            public void run() { new Gauge(null, true, Gauge.INDEFINITE, 0); }
        });
        throwsIt("indefinite with a bad state", IllegalArgumentException.class, new Run() {
            public void run() { new Gauge(null, false, Gauge.INDEFINITE, 5); }
        });
        Gauge g = new Gauge(null, false, 10, 50);
        eq("value clamped", g.getValue(), 10);
        g.setMaxValue(Gauge.INDEFINITE);
        eq("to indefinite: idle", g.getValue(), Gauge.CONTINUOUS_IDLE);
        // DateField
        TimeZone gmt = TimeZone.getTimeZone("GMT");
        DateField df = new DateField("Ngày", DateField.DATE, gmt);
        ok("unset date", df.getDate() == null);
        df.setDate(new Date(365L * 86400000L + 5 * 3600000L));  // 1971-01-01 05:00
        eq("DATE drops the time", df.getDate().getTime(), 365L * 86400000L);
        DateField tf = new DateField("Giờ", DateField.TIME, gmt);
        tf.setDate(new Date(10 * 86400000L + 5 * 3600000L + 7 * 60000L));
        eq("TIME keeps 05:07 on 1 Jan 1970", tf.getDate().getTime(), 5 * 3600000L + 7 * 60000L);
        eq("days in Feb 2024", DateField.daysIn(2024, 1), 29);
        eq("days in Feb 2100", DateField.daysIn(2100, 1), 28);
        Form f = new Form("Ngày");
        f.append(df);
        d.setCurrent(f);
        paint();
        press(f, Canvas.K_FIRE);  // edit
        press(f, Canvas.K_UP);    // day + 1
        eq("day up", df.getDate().getTime(), 366L * 86400000L);
        press(f, Canvas.K_DOWN);
        press(f, Canvas.K_DOWN);  // day - 2 from the 2nd: wraps to the 31st of the same month
        eq("day wraps in its month", df.getDate().getTime(), (365L + 30) * 86400000L);
        press(f, Canvas.K_RIGHT);
        press(f, Canvas.K_UP);    // month + 1: 31 Jan -> 28 Feb 1971
        eq("month up clamps the day", df.getDate().getTime(), (365L + 31 + 27) * 86400000L);
        press(f, Canvas.K_FIRE);  // done
        paint();
        // CustomItem
        final int[] got = new int[4];
        CustomItem ci = new CustomItem("Vẽ") {
            protected int getMinContentWidth() { return 10; }
            protected int getMinContentHeight() { return 10; }
            protected int getPrefContentWidth(int h) { return 50; }
            protected int getPrefContentHeight(int w) { return 30; }
            protected void paint(Graphics g, int w, int h) {
                got[0] = w;
                got[1] = h;
                g.translate(5, 5);  // the form puts it back
            }
            protected void keyPressed(int k) { got[2] = k; }
            protected boolean traverse(int dir, int vw, int vh, int[] r) { return got[3]++ == 0; }
        };
        Form cf = new Form("Tự vẽ");
        cf.append(ci);
        cf.append(new StringItem(null, "Sau", Item.BUTTON));
        ((StringItem) cf.get(1)).addCommand(new Command("Đi", Command.ITEM, 1));
        d.setCurrent(cf);
        paint();
        ok("custom item painted at its size", got[0] == 50 && got[1] == 30);
        ok("translation restored", d.sg.getTranslateX() == 0 && d.sg.getTranslateY() == 0);
        press(cf, '7');
        eq("custom item key", got[2], '7');
        press(cf, Canvas.K_DOWN);  // traverse said false (2nd call): the focus moves on
        ok("traverse false moves on", cf.focused() == cf.get(1));
        // ImageItem, Spacer, hyperlink, popup: painted once
        Form all = new Form("Mọi thứ");
        all.append(new ImageItem("Ảnh", Image.createImage(300, 40), Item.LAYOUT_CENTER, "ảnh"));
        all.append(new ImageItem(null, null, Item.LAYOUT_DEFAULT, "không có ảnh"));
        all.append(new Spacer(10, 20));
        StringItem link = new StringItem(null, "liên kết", Item.HYPERLINK);
        link.addCommand(new Command("Mở", Command.ITEM, 1));
        all.append(link);
        final ChoiceGroup pop = new ChoiceGroup("Chọn", Choice.POPUP, new String[] {"Một", "Hai", "Ba"}, null);
        all.append(pop);
        all.append(new Gauge("Tiến độ", false, 100, 40));
        all.append("chữ thường");
        d.setCurrent(all);
        paint();
        all.focusItem(pop);
        press(all, Canvas.K_RIGHT);
        eq("popup: right steps", pop.getSelectedIndex(), 1);
        press(all, Canvas.K_FIRE);  // open
        paint();
        press(all, Canvas.K_DOWN);
        press(all, Canvas.K_FIRE);  // pick 3, close
        eq("popup: open, down, OK", pop.getSelectedIndex(), 2);
        press(all, Canvas.K_DOWN);
        paint();
        for (int i = 0; i < 6; i++) press(all, Canvas.K_DOWN);
        paint();
        for (int i = 0; i < 8; i++) press(all, Canvas.K_UP);
        paint();
        ok("painted everything", true);
    }
}
