package javax.microedition.media;

import java.io.IOException;
import java.io.InputStream;

/**
 * Players for the game's sounds. Content types are matched loosely (case, parameters, common aliases: games pass
 * "audio/X-wav", "audio/mid", "audio/mp3"); any other "audio/..." type gets a silent player rather than an error.
 * Locators: device://tone, device://midi, resource:/file (from the jar); other protocols give MediaException.
 */
public final class Manager {
    public static final String TONE_DEVICE_LOCATOR = "device://tone";
    public static final String MIDI_DEVICE_LOCATOR = "device://midi";

    static final String WAV = "audio/x-wav", MIDI = "audio/midi", MPEG = "audio/mpeg", AMR = "audio/amr",
            TONES = "audio/x-tone-seq";

    private static final TimeBase SYSTEM = new TimeBase() {
        public long getTime() { return System.currentTimeMillis() * 1000; }
    };

    private Manager() {}

    public static String[] getSupportedContentTypes(String protocol) {
        if (protocol == null || protocol.equalsIgnoreCase("resource")) return new String[] {WAV, MIDI, MPEG, AMR, TONES};
        if (protocol.equalsIgnoreCase("device")) return new String[] {TONES, MIDI};
        return new String[0];
    }

    public static String[] getSupportedProtocols(String contentType) {
        if (contentType == null) return new String[] {"device", "resource"};
        String t = canonical(contentType);
        if (TONES.equals(t) || MIDI.equals(t)) return new String[] {"device", "resource"};
        if (WAV.equals(t) || MPEG.equals(t) || AMR.equals(t)) return new String[] {"resource"};
        return new String[0];
    }

    public static Player createPlayer(String locator) throws IOException, MediaException {
        if (locator == null) throw new IllegalArgumentException();
        String l = locator.trim(), low = l.toLowerCase();
        if (low.equals(TONE_DEVICE_LOCATOR)) return new BasePlayer(TONES, 0, 0, null);
        if (low.equals(MIDI_DEVICE_LOCATOR)) return new BasePlayer(MIDI, Player.TIME_UNKNOWN, guess(MIDI), null);
        if (low.startsWith("resource:")) {
            String path = l.substring(9);
            while (path.startsWith("//")) path = path.substring(1);
            if (!path.startsWith("/")) path = "/" + path;
            InputStream in = Manager.class.getResourceAsStream(path);
            if (in == null) throw new MediaException("no resource " + path);
            try {
                return createPlayer(in, byName(path));
            } finally {
                in.close();
            }
        }
        if (l.indexOf(':') < 0) throw new MediaException("not a locator: " + locator);
        throw new MediaException("not supported: " + locator);
    }

    public static Player createPlayer(InputStream stream, String type) throws IOException, MediaException {
        if (stream == null) throw new IllegalArgumentException();
        byte[] b = readAll(stream);
        String t = type != null ? canonical(type) : sniff(b);
        if (t == null) throw new MediaException("content type not supported: " + type);
        if (TONES.equals(t)) {
            try {
                return new BasePlayer(TONES, 0, 0, ToneSequence.parse(b));
            } catch (IllegalArgumentException e) {
                throw new MediaException("bad tone sequence: " + e.getMessage());
            }
        }
        long d = WAV.equals(t) ? wavDuration(b) : Player.TIME_UNKNOWN;
        return new BasePlayer(t, d, guess(t), null);
    }

    public static void playTone(int note, int duration, int volume) throws MediaException {
        if (note < 0 || note > 127 || duration <= 0) throw new IllegalArgumentException();
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;
        MediaQueue.tone(note, duration, volume);
    }

    public static TimeBase getSystemTimeBase() { return SYSTEM; }

    // ---- content types

    /** One name for each kind of sound, or null when it is not sound. */
    static String canonical(String type) {
        String t = type.trim().toLowerCase();
        int semi = t.indexOf(';');
        if (semi >= 0) t = t.substring(0, semi).trim();
        if (t.equals(WAV) || t.equals("audio/wav") || t.equals("audio/wave") || t.equals("audio/vnd.wave")) return WAV;
        if (t.equals(MIDI) || t.equals("audio/mid") || t.equals("audio/x-midi") || t.equals("audio/x-mid")
                || t.equals("audio/sp-midi") || t.equals("audio/x-sp-midi")) return MIDI;
        if (t.equals(MPEG) || t.equals("audio/mp3") || t.equals("audio/mpeg3") || t.equals("audio/x-mp3")
                || t.equals("audio/x-mpeg")) return MPEG;
        if (t.equals(AMR) || t.equals("audio/x-amr")) return AMR;
        if (t.equals(TONES)) return TONES;
        if (t.startsWith("audio/")) return t;
        return null;
    }

    private static String sniff(byte[] b) {
        if (starts(b, "RIFF")) return WAV;
        if (starts(b, "MThd")) return MIDI;
        if (starts(b, "#!AMR")) return AMR;
        if (starts(b, "ID3") || (b.length > 1 && (b[0] & 0xFF) == 0xFF && (b[1] & 0xE0) == 0xE0)) return MPEG;
        if (b.length > 1 && b[0] == -2 && b[1] == 1) return TONES;
        return null;
    }

    private static String byName(String path) {
        String p = path.toLowerCase();
        if (p.endsWith(".wav")) return WAV;
        if (p.endsWith(".mid") || p.endsWith(".midi")) return MIDI;
        if (p.endsWith(".mp3")) return MPEG;
        if (p.endsWith(".amr")) return AMR;
        if (p.endsWith(".jts")) return TONES;
        return null;
    }

    private static boolean starts(byte[] b, String s) {
        if (b.length < s.length()) return false;
        for (int i = 0; i < s.length(); i++) if (b[i] != s.charAt(i)) return false;
        return true;
    }

    /** How long a sound of unknown length seems to play (us). */
    private static long guess(String t) { return MIDI.equals(t) ? 1000000 : 300000; }

    /** A wav's length from its header (us), or TIME_UNKNOWN. */
    static long wavDuration(byte[] b) {
        if (b.length < 12 || !starts(b, "RIFF") || b[8] != 'W' || b[9] != 'A' || b[10] != 'V' || b[11] != 'E')
            return Player.TIME_UNKNOWN;
        long byteRate = 0;
        int p = 12;
        while (p + 8 <= b.length) {
            long size = le(b, p + 4) & 0xFFFFFFFFL;
            boolean fmt = b[p] == 'f' && b[p + 1] == 'm' && b[p + 2] == 't';
            boolean data = b[p] == 'd' && b[p + 1] == 'a' && b[p + 2] == 't' && b[p + 3] == 'a';
            p += 8;
            if (fmt && p + 12 <= b.length) byteRate = le(b, p + 8) & 0xFFFFFFFFL;
            if (data) {
                long have = b.length - p;
                if (size == 0 || size > have) size = have;  // streamed or cut files: what is there
                return byteRate > 0 ? size * 1000000 / byteRate : Player.TIME_UNKNOWN;
            }
            p += (int) Math.min(size + (size & 1), b.length);
        }
        return Player.TIME_UNKNOWN;
    }

    private static int le(byte[] b, int p) {
        return (b[p] & 0xFF) | ((b[p + 1] & 0xFF) << 8) | ((b[p + 2] & 0xFF) << 16) | ((b[p + 3] & 0xFF) << 24);
    }

    private static byte[] readAll(InputStream in) throws IOException {
        byte[] buf = new byte[4096];
        int n = 0;
        for (;;) {
            if (n == buf.length) {
                byte[] b2 = new byte[buf.length * 2];
                System.arraycopy(buf, 0, b2, 0, n);
                buf = b2;
            }
            int r = in.read(buf, n, buf.length - n);
            if (r < 0) break;
            n += r;
        }
        byte[] b = new byte[n];
        System.arraycopy(buf, 0, b, 0, n);
        return b;
    }
}
