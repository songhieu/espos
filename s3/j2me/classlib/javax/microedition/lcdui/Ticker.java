package javax.microedition.lcdui;

/** Kept, not scrolled across the screen (EspOS shows no ticker line). */
public class Ticker {
    private String text;

    public Ticker(String str) {
        if (str == null) throw new NullPointerException();
        text = str;
    }

    public void setString(String str) {
        if (str == null) throw new NullPointerException();
        text = str;
    }

    public String getString() { return text; }
}
