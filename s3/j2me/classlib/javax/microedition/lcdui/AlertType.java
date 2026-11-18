package javax.microedition.lcdui;

import com.espos.j2me.Native;

/** The kinds of alert: each has a colour (Alert's mark) and a short sound. */
public class AlertType {
    public static final AlertType INFO = new AlertType(1), WARNING = new AlertType(2), ERROR = new AlertType(3),
            ALARM = new AlertType(4), CONFIRMATION = new AlertType(5);

    final int kind;

    protected AlertType() { kind = 0; }

    private AlertType(int kind) { this.kind = kind; }

    public boolean playSound(Display display) {
        if (display == null) throw new NullPointerException();
        int note, ms;
        switch (kind) {
            case 1: note = 76; ms = 90; break;     // info: a short high blip
            case 2: note = 69; ms = 160; break;    // warning
            case 3: note = 57; ms = 260; break;    // error: low and longer
            case 4: note = 84; ms = 320; break;    // alarm: high and long
            case 5: note = 79; ms = 110; break;    // confirmation
            default: return false;
        }
        try {
            Native.tone(note, ms, 70);
        } catch (Throwable e) {
            return false;
        }
        return true;
    }

    /** The mark's colour on an Alert. */
    int color() {
        switch (kind) {
            case 1: return 0x2F80ED;
            case 2: return 0xF2A900;
            case 3: return 0xE5484D;
            case 4: return 0x8E4EC6;
            case 5: return 0x30A46C;
        }
        return Screen.MUTED;
    }
}
