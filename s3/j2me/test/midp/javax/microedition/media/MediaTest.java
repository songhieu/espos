package javax.microedition.media;

import java.io.ByteArrayInputStream;
import java.util.Vector;
import javax.microedition.media.control.ToneControl;
import javax.microedition.media.control.VolumeControl;

/**
 * Players' states and events, content types, controls and tone sequences, with no sound (Native.tone is never
 * reached in a way that matters: the media code swallows its absence on a PC). Prints "MediaTest: OK (n checks)".
 */
public class MediaTest {
    static int checks, fails;

    static void check(boolean ok, String what) {
        checks++;
        if (!ok) {
            fails++;
            System.out.println("FAIL " + what);
        }
    }

    static void eq(long got, long want, String what) {
        checks++;
        if (got != want) {
            fails++;
            System.out.println("FAIL " + what + ": got " + got + " want " + want);
        }
    }

    static void eq(String got, String want, String what) {
        checks++;
        if (got == null ? want != null : !got.equals(want)) {
            fails++;
            System.out.println("FAIL " + what + ": got " + got + " want " + want);
        }
    }

    /** Collects events as "name name ...". */
    static final class Log implements PlayerListener {
        final Vector events = new Vector();

        public synchronized void playerUpdate(Player p, String event, Object data) { events.addElement(event); }

        synchronized String text() {
            StringBuffer sb = new StringBuffer();
            for (int i = 0; i < events.size(); i++) sb.append(i > 0 ? " " : "").append(events.elementAt(i));
            return sb.toString();
        }

        synchronized int count(String ev) {
            int n = 0;
            for (int i = 0; i < events.size(); i++) if (ev.equals(events.elementAt(i))) n++;
            return n;
        }

        /** Waits (up to 3 s) for n events named ev. */
        void await(String ev, int n) throws InterruptedException {
            for (int i = 0; i < 600 && count(ev) < n; i++) Thread.sleep(5);
        }
    }

    /** A PCM wav: 8000 bytes a second, 80 bytes of data = 10 ms. */
    static byte[] wav() {
        byte[] b = new byte[44 + 80];
        put(b, 0, "RIFF");
        le(b, 4, b.length - 8);
        put(b, 8, "WAVE");
        put(b, 12, "fmt ");
        le(b, 16, 16);
        b[20] = 1;   // PCM
        b[22] = 1;   // mono
        le(b, 24, 8000);  // sample rate
        le(b, 28, 8000);  // byte rate
        b[32] = 1;   // block align
        b[34] = 8;   // bits
        put(b, 36, "data");
        le(b, 40, 80);
        return b;
    }

    static void put(byte[] b, int p, String s) { for (int i = 0; i < s.length(); i++) b[p + i] = (byte) s.charAt(i); }

    static void le(byte[] b, int p, int v) {
        b[p] = (byte) v;
        b[p + 1] = (byte) (v >> 8);
        b[p + 2] = (byte) (v >> 16);
        b[p + 3] = (byte) (v >> 24);
    }

    static Player create(byte[] b, String type) throws Exception {
        return Manager.createPlayer(new ByteArrayInputStream(b), type);
    }

    public static void main(String[] a) throws Exception {
        states();
        loops();
        controls();
        types();
        tones();
        if (fails == 0) System.out.println("MediaTest: OK (" + checks + " checks)");
        else System.out.println("MediaTest: FAILED " + fails + " of " + checks);
        System.exit(fails == 0 ? 0 : 1);
    }

    static void states() throws Exception {
        Player p = create(wav(), "audio/X-wav");
        eq(p.getState(), Player.UNREALIZED, "new: unrealized");
        eq(p.getContentType(), "audio/x-wav", "content type");
        eq(p.getDuration(), 10000, "wav duration from its header (us)");
        try {
            p.setMediaTime(0);
            check(false, "setMediaTime unrealized: no exception");
        } catch (IllegalStateException e) {
            check(true, "");
        }
        p.realize();
        eq(p.getState(), Player.REALIZED, "realize");
        p.prefetch();
        eq(p.getState(), Player.PREFETCHED, "prefetch");
        p.deallocate();
        eq(p.getState(), Player.REALIZED, "deallocate");
        p.realize();
        eq(p.getState(), Player.REALIZED, "realize again: no change");
        Log log = new Log();
        p.addPlayerListener(log);
        p.setLoopCount(-1);  // until stopped
        p.start();
        eq(p.getState(), Player.STARTED, "start from realized");
        p.start();  // again: nothing
        eq(p.getState(), Player.STARTED, "start while started");
        try {
            p.setLoopCount(2);
            check(false, "setLoopCount while started: no exception");
        } catch (IllegalStateException e) {
            check(true, "");
        }
        log.await(PlayerListener.END_OF_MEDIA, 3);
        check(log.count(PlayerListener.END_OF_MEDIA) >= 3, "endless loop: END_OF_MEDIA each time round");
        eq(p.getState(), Player.STARTED, "endless loop: still started");
        p.stop();
        eq(p.getState(), Player.PREFETCHED, "stop");
        log.await(PlayerListener.STOPPED, 1);
        eq(log.count(PlayerListener.STOPPED), 1, "STOPPED event");
        p.stop();  // again: nothing
        p.deallocate();
        eq(p.getState(), Player.REALIZED, "deallocate after stop");
        p.close();
        eq(p.getState(), Player.CLOSED, "close");
        log.await(PlayerListener.CLOSED, 1);
        eq(log.count(PlayerListener.CLOSED), 1, "CLOSED event");
        p.close();  // again: nothing
        try {
            p.start();
            check(false, "start after close: no exception");
        } catch (IllegalStateException e) {
            check(true, "");
        }
        try {
            p.realize();
            check(false, "realize after close: no exception");
        } catch (IllegalStateException e) {
            check(true, "");
        }
        // deallocate while started stops it first
        Player q = create(wav(), "audio/x-wav");
        Log ql = new Log();
        q.addPlayerListener(ql);
        q.setLoopCount(-1);
        q.start();
        q.deallocate();
        eq(q.getState(), Player.REALIZED, "deallocate while started");
        ql.await(PlayerListener.STOPPED, 1);
        eq(ql.count(PlayerListener.STOPPED), 1, "deallocate while started: STOPPED");
        q.close();
    }

    static void loops() throws Exception {
        Player p = create(wav(), "audio/wav");
        Log log = new Log();
        p.addPlayerListener(log);
        try {
            p.setLoopCount(0);
            check(false, "loop count 0: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        p.setLoopCount(2);
        p.start();  // from unrealized: realizes and prefetches by itself
        log.await(PlayerListener.END_OF_MEDIA, 2);
        Thread.sleep(60);  // a third would come by now if it were wrong
        eq(log.text(), "started endOfMedia endOfMedia", "loop count 2: events in order");
        eq(p.getState(), Player.PREFETCHED, "after the last loop: prefetched");
        eq(p.getMediaTime(), 10000, "media time at the end");
        p.start();  // at the end: plays again from the start
        log.await(PlayerListener.END_OF_MEDIA, 4);
        eq(log.count(PlayerListener.STARTED), 2, "start again after the end");
        eq(log.count(PlayerListener.END_OF_MEDIA), 4, "loop count kept for the next start");
        p.setMediaTime(4000);
        eq(p.getMediaTime(), 4000, "setMediaTime");
        eq(p.setMediaTime(99999999), 10000, "setMediaTime past the end: the end");
        p.close();
        // many starts in a row (a game firing): cheap, no exception
        Player s = create(wav(), "audio/x-wav");
        s.realize();
        s.prefetch();
        for (int i = 0; i < 200; i++) s.start();
        s.close();
    }

    static void controls() throws Exception {
        Player p = create(wav(), "audio/x-wav");
        Control v = p.getControl("VolumeControl");
        check(v instanceof VolumeControl, "VolumeControl (unrealized too)");
        check(p.getControl("javax.microedition.media.control.VolumeControl") == v, "VolumeControl by its full name");
        check(p.getControl("ToneControl") == null, "no ToneControl on a wav");
        check(p.getControl("FramePositioningControl") == null, "unknown control: null");
        eq(p.getControls().length, 1, "getControls of a wav");
        VolumeControl vc = (VolumeControl) v;
        Log log = new Log();
        p.addPlayerListener(log);
        eq(vc.setLevel(150), 100, "setLevel above 100");
        eq(vc.setLevel(40), 40, "setLevel");
        eq(vc.getLevel(), 40, "getLevel");
        vc.setMute(true);
        check(vc.isMuted(), "muted");
        log.await(PlayerListener.VOLUME_CHANGED, 2);
        eq(log.count(PlayerListener.VOLUME_CHANGED), 2, "VOLUME_CHANGED for level and mute");
        try {
            p.getControl(null);
            check(false, "getControl(null): no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        p.close();
        try {
            p.getControl("VolumeControl");
            check(false, "getControl after close: no exception");
        } catch (IllegalStateException e) {
            check(true, "");
        }
        Player t = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
        t.realize();
        check(t.getControl("ToneControl") instanceof ToneControl, "tone player: ToneControl");
        eq(t.getControls().length, 2, "tone player: two controls");
        t.close();
    }

    static void types() throws Exception {
        eq(Manager.canonical("AUDIO/MIDI"), "audio/midi", "type case");
        eq(Manager.canonical("audio/mid"), "audio/midi", "audio/mid");
        eq(Manager.canonical("audio/mp3"), "audio/mpeg", "audio/mp3");
        eq(Manager.canonical(" audio/x-wav; codec=1 "), "audio/x-wav", "parameters and spaces");
        eq(Manager.canonical("audio/amr"), "audio/amr", "amr");
        check(Manager.canonical("video/mp4") == null, "video is not sound");
        String[] kinds = {"audio/midi", "audio/mpeg", "audio/amr", "audio/x-wav", "audio/x-mmf"};
        for (int i = 0; i < kinds.length; i++) {
            Player p = create(new byte[16], kinds[i]);
            p.realize();
            eq(p.getContentType(), kinds[i], "player for " + kinds[i]);
            check(i == 3 || p.getDuration() == Player.TIME_UNKNOWN, kinds[i] + ": duration unknown");
            p.close();
        }
        try {
            create(new byte[16], "video/mp4");
            check(false, "video: no exception");
        } catch (MediaException e) {
            check(true, "");
        }
        eq(create(wav(), null).getContentType(), "audio/x-wav", "no type: sniffed wav");
        byte[] mid = {'M', 'T', 'h', 'd', 0, 0, 0, 6};
        eq(create(mid, null).getContentType(), "audio/midi", "no type: sniffed midi");
        try {
            create(new byte[] {1, 2, 3}, null);
            check(false, "no type, unknown bytes: no exception");
        } catch (MediaException e) {
            check(true, "");
        }
        eq(Manager.createPlayer("device://midi").getContentType(), "audio/midi", "device://midi");
        try {
            Manager.createPlayer("http://example.com/a.wav");
            check(false, "http: no exception");
        } catch (MediaException e) {
            check(true, "");
        }
        try {
            Manager.createPlayer((String) null);
            check(false, "null locator: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        check(has(Manager.getSupportedContentTypes(null), "audio/x-wav"), "supported types: wav");
        check(has(Manager.getSupportedContentTypes("device"), "audio/x-tone-seq"), "device: tones");
        check(has(Manager.getSupportedProtocols("audio/x-tone-seq"), "device"), "tones: device protocol");
        try {
            Manager.playTone(128, 100, 50);
            check(false, "playTone note 128: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        try {
            Manager.playTone(60, 0, 50);
            check(false, "playTone 0 ms: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        Manager.playTone(60, 50, 150);  // volume clamps; no sound here, no exception
        check(Manager.getSystemTimeBase().getTime() > 0, "system time base");
    }

    static boolean has(String[] a, String s) {
        for (int i = 0; i < a.length; i++) if (a[i].equals(s)) return true;
        return false;
    }

    static final byte V = ToneControl.VERSION, T = ToneControl.TEMPO, R = ToneControl.RESOLUTION,
            BS = ToneControl.BLOCK_START, BE = ToneControl.BLOCK_END, PB = ToneControl.PLAY_BLOCK,
            SV = ToneControl.SET_VOLUME, RP = ToneControl.REPEAT, C4 = ToneControl.C4, S = ToneControl.SILENCE;

    static void bad(byte[] seq, String what) {
        try {
            ToneSequence.parse(seq);
            check(false, "bad tone sequence accepted: " + what);
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
    }

    static void tones() throws Exception {
        // tempo 30 (120 bpm), resolution 64: a unit is 60000 / (64 * 30) = 31.25 ms
        byte[] seq = {V, 1, T, 30, R, 64,
                BS, 0, C4, 8, C4 + 2, 8, BE, 0,  // block 0: two eighths = 16 units
                PB, 0,                            // 16
                SV, 50, RP, 3, C4 + 4, 16,        // 3 x 16 = 48
                S, 8};                            // 8: 72 units = 2250 ms
        ToneSequence t = ToneSequence.parse(seq);
        eq(t.durationUs, 2250000, "tone sequence length");
        eq(t.n, 6, "tones laid out");
        eq(t.notes[0] * 1000 + t.notes[1], 60062, "block's notes");
        eq(t.ms[0], 250, "an eighth: 250 ms");
        eq(t.vols[2], 50, "SET_VOLUME");
        eq(t.notes[5], -1, "silence");
        eq(ToneSequence.parse(new byte[] {V, 1, C4, 64}).durationUs, 2000000, "defaults: tempo 120, resolution 64");
        bad(new byte[] {V, 2, C4, 8}, "version 2");
        bad(new byte[] {V, 1}, "no tones");
        bad(new byte[] {C4, 8}, "no version");
        bad(new byte[] {V, 1, C4, 0}, "zero length");
        bad(new byte[] {V, 1, C4}, "cut short");
        bad(new byte[] {V, 1, PB, 3}, "undefined block");
        bad(new byte[] {V, 1, BS, 0, C4, 8}, "unterminated block");
        bad(new byte[] {V, 1, BS, 0, C4, 8, BE, 1, PB, 0}, "block ends with another number");
        bad(new byte[] {V, 1, BS, 1, PB, 1, BE, 1, PB, 1}, "block plays itself");
        bad(new byte[] {V, 1, -100, 8}, "unknown code");
        bad(new byte[] {V, 1, SV, 101, C4, 8}, "volume 101");
        bad(new byte[] {V, 1, RP, 1, C4, 8}, "repeat once");
        bad(new byte[] {V, 1, T, 4, C4, 8}, "tempo 4");
        // through the player
        Player p = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
        p.realize();
        eq(p.getDuration(), 0, "tone player with no sequence");
        ToneControl tc = (ToneControl) p.getControl("ToneControl");
        tc.setSequence(seq);
        eq(p.getDuration(), 2250000, "tone player duration");
        try {
            tc.setSequence(new byte[] {V, 1});
            check(false, "bad sequence through ToneControl: no exception");
        } catch (IllegalArgumentException e) {
            check(true, "");
        }
        p.start();
        try {
            tc.setSequence(seq);
            check(false, "setSequence while started: no exception");
        } catch (IllegalStateException e) {
            check(true, "");
        }
        p.close();
        Player q = create(seq, "audio/x-tone-seq");
        eq(q.getDuration(), 2250000, "tone sequence from a stream");
        q.close();
        try {
            create(new byte[] {V, 1}, "audio/x-tone-seq");
            check(false, "bad tone stream: no exception");
        } catch (MediaException e) {
            check(true, "");
        }
        // an empty tone player started: ends at once, even looping forever
        Player e = Manager.createPlayer(Manager.TONE_DEVICE_LOCATOR);
        Log log = new Log();
        e.addPlayerListener(log);
        e.setLoopCount(-1);
        e.start();
        log.await(PlayerListener.END_OF_MEDIA, 1);
        Thread.sleep(30);
        eq(log.count(PlayerListener.END_OF_MEDIA), 1, "nothing to play: one END_OF_MEDIA");
        eq(e.getState(), Player.PREFETCHED, "nothing to play: prefetched");
        e.close();
    }
}
