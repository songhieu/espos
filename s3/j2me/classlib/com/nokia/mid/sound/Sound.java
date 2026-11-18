package com.nokia.mid.sound;

import com.espos.j2me.Native;

/**
 * Nokia's Sound: tones (a frequency, or a Smart Messaging ringing tone in FORMAT_TONE) play through Native.tone,
 * note by note on a thread; wav sounds make no sound yet but take their time (their real length, else 300 ms).
 * A ringing tone that cannot be read plays silently.
 */
public class Sound {
    public static final int FORMAT_TONE = 1, FORMAT_WAV = 5;
    public static final int SOUND_PLAYING = 0, SOUND_STOPPED = 1, SOUND_UNINITIALIZED = 3;

    // Smart Messaging tempo codes (beats a minute)
    private static final int[] BPM = {25, 28, 31, 35, 40, 45, 50, 56, 63, 70, 80, 90, 100, 112, 125, 140, 160, 180,
            200, 225, 250, 285, 320, 355, 400, 450, 500, 565, 635, 715, 800, 900};

    private int state = SOUND_UNINITIALIZED;
    private int gain = 255;
    private SoundListener listener;
    // what plays: notes (MIDI, -1 = rest) with their length (ms) and volume (0..100)
    private int[] notes = new int[0], ms = new int[0], vols = new int[0];
    private volatile int token;
    // where a stopped play was (for resume)
    private int pos, loopsLeft, loopTotal;

    public Sound(byte[] data, int type) { init(data, type); }

    public Sound(int freq, long duration) { init(freq, duration); }

    public void init(byte[] data, int type) {
        if (data == null) throw new NullPointerException();
        if (type != FORMAT_TONE && type != FORMAT_WAV) throw new IllegalArgumentException("format " + type);
        halt(false);
        if (type == FORMAT_WAV) {
            set(new int[] {-1}, new int[] {wavMs(data)}, new int[] {0});
        } else {
            Ott o = new Ott(data);
            try {
                o.read();
            } catch (RuntimeException e) {
                o.n = 0;  // not a ringing tone we can read: silent
            }
            int[] n = new int[o.n], m = new int[o.n], v = new int[o.n];
            System.arraycopy(o.notes, 0, n, 0, o.n);
            System.arraycopy(o.ms, 0, m, 0, o.n);
            System.arraycopy(o.vols, 0, v, 0, o.n);
            set(n, m, v);
        }
    }

    public void init(int freq, long duration) {
        if (freq < 0 || duration <= 0) throw new IllegalArgumentException();
        halt(false);
        set(new int[] {note(freq)}, new int[] {(int) Math.min(duration, 600000)}, new int[] {100});
    }

    private synchronized void set(int[] n, int[] m, int[] v) {
        notes = n;
        ms = m;
        vols = v;
        pos = 0;
        loopsLeft = 0;
        state = SOUND_STOPPED;
    }

    public static int getConcurrentSoundCount(int type) { return 1; }

    public static int[] getSupportedFormats() { return new int[] {FORMAT_TONE, FORMAT_WAV}; }

    public int getGain() { return gain; }

    public void setGain(int gain) { this.gain = gain < 0 ? 0 : gain > 255 ? 255 : gain; }

    public int getState() { return state; }

    public void setSoundListener(SoundListener listener) { this.listener = listener; }

    /** loop: how many times (1..255); 0 plays until stop. */
    public void play(int loop) {
        if (loop < 0) throw new IllegalArgumentException("loop " + loop);
        int tk;
        synchronized (this) {
            if (state == SOUND_UNINITIALIZED) return;
            tk = ++token;
            pos = 0;
            loopTotal = loop;
            loopsLeft = loop;
        }
        begin(tk);
    }

    /** Goes on from where stop left it. */
    public void resume() {
        int tk;
        synchronized (this) {
            if (state != SOUND_STOPPED || (pos == 0 && loopsLeft == 0 && loopTotal != 0)) return;
            tk = ++token;
        }
        begin(tk);
    }

    public void stop() { halt(true); }

    public void release() {
        halt(false);
        synchronized (this) {
            state = SOUND_UNINITIALIZED;
            notes = ms = vols = new int[0];
        }
    }

    private void halt(boolean tell) {
        boolean was;
        synchronized (this) {
            token++;
            was = state == SOUND_PLAYING;
            if (was) state = SOUND_STOPPED;
        }
        if (was) {
            tone(-1, 0, 0);
            if (tell) changed(SOUND_STOPPED);
        }
    }

    private void begin(final int tk) {
        synchronized (this) {
            state = SOUND_PLAYING;
        }
        changed(SOUND_PLAYING);
        new Thread("sound") {
            public void run() { playing(tk); }
        }.start();
    }

    /** The play thread: notes from pos, loop after loop, until the end or another play (token) cuts it. */
    private void playing(int tk) {
        int[] n = notes, m = ms, v = vols;
        long total = 0;
        for (int i = 0; i < m.length; i++) total += m[i];
        for (;;) {
            int i;
            synchronized (this) {
                if (token != tk) return;
                if (pos >= n.length) {
                    pos = 0;
                    if (loopsLeft > 0) loopsLeft--;
                    // loopTotal 0 = endless (but not when there is nothing to wait for)
                    if (loopsLeft == 0 && (loopTotal != 0 || total == 0)) {
                        state = SOUND_STOPPED;
                        break;
                    }
                }
                i = pos++;
            }
            int vol = v[i] * gain / 255;
            if (n[i] >= 0 && vol > 0) tone(n[i], m[i], vol);
            try {
                Thread.sleep(m[i]);
            } catch (InterruptedException e) {
            }
        }
        changed(SOUND_STOPPED);
    }

    private void changed(int event) {
        SoundListener l = listener;
        if (l == null) return;
        try {
            l.soundStateChanged(this, event);
        } catch (Throwable e) {
            e.printStackTrace();
        }
    }

    private static void tone(int note, int ms, int vol) {
        try {
            Native.tone(note, ms, vol);
        } catch (Throwable e) {
        }
    }

    /** The MIDI note nearest a frequency (Hz); -1 (rest) for 0. */
    static int note(int freq) {
        if (freq <= 0) return -1;
        final double step = 1.0594630943592953, half = 1.029302236643492;  // 2^(1/12), 2^(1/24)
        double f = 440.0;
        int n = 69;
        if (freq >= 440) {
            while (n < 127 && f * half < freq) {
                f *= step;
                n++;
            }
        } else {
            while (n > 0 && f / half > freq) {
                f /= step;
                n--;
            }
        }
        return n;
    }

    /** A wav's length (ms) from its header, else 300. */
    private static int wavMs(byte[] b) {
        if (b.length < 12 || b[0] != 'R' || b[1] != 'I' || b[2] != 'F' || b[3] != 'F') return 300;
        long rate = 0;
        int p = 12;
        while (p + 8 <= b.length) {
            long size = le(b, p + 4) & 0xFFFFFFFFL;
            boolean fmt = b[p] == 'f' && b[p + 1] == 'm' && b[p + 2] == 't';
            boolean data = b[p] == 'd' && b[p + 1] == 'a' && b[p + 2] == 't' && b[p + 3] == 'a';
            p += 8;
            if (fmt && p + 12 <= b.length) rate = le(b, p + 8) & 0xFFFFFFFFL;
            if (data) {
                if (size == 0 || size > b.length - p) size = b.length - p;
                return rate > 0 ? (int) Math.min(600000, size * 1000 / rate) : 300;
            }
            p += (int) Math.min(size + (size & 1), b.length);
        }
        return 300;
    }

    private static int le(byte[] b, int p) {
        return (b[p] & 0xFF) | ((b[p + 1] & 0xFF) << 8) | ((b[p + 2] & 0xFF) << 16) | ((b[p + 3] & 0xFF) << 24);
    }

    /**
     * A Smart Messaging ringing tone (Nokia OTA), read bit by bit (most significant first):
     *   command count (8); commands (7 bits each): 0100101 ringing tone programming, 0100010 unicode (both then
     *   padded to a byte), 0011101 sound: song type (3: 001 basic with a title of 4-bit length + 8 or 16-bit
     *   characters, 010 temporary), pattern count (8), patterns: 000, id (2), loop (4), instruction count (8), and
     *   instructions: 001 note (value 4, length 3, dots 2), 010 scale (2), 011 style (2), 100 tempo (5), 101 volume (4).
     * Notes: 0 rest, 1..12 C..B; scale 0..3 has A at 440, 880, 1760, 3520 Hz (default 880); lengths whole to 1/32.
     */
    private static final class Ott {
        final byte[] d;
        int bit;
        int n;
        int[] notes = new int[32], ms = new int[32], vols = new int[32];
        private final int[][] patternFrom = new int[4][];  // pattern id -> {first note, count}

        Ott(byte[] d) { this.d = d; }

        int bits(int k) {
            int v = 0;
            for (int i = 0; i < k; i++, bit++) {
                if ((bit >> 3) >= d.length) throw new IllegalArgumentException("ringing tone ends early");
                v = (v << 1) | ((d[bit >> 3] >> (7 - (bit & 7))) & 1);
            }
            return v;
        }

        void read() {
            int commands = bits(8);
            boolean unicode = false;
            for (int c = 0; c < commands; c++) {
                int cmd = bits(7);
                if (cmd == 0x25 || cmd == 0x22) {  // ringing tone programming, unicode
                    if (cmd == 0x22) unicode = true;
                    bit = (bit + 7) & ~7;
                } else if (cmd == 0x1D) {  // sound
                    song(unicode);
                    return;
                } else {
                    return;
                }
            }
        }

        private void song(boolean unicode) {
            int type = bits(3);
            if (type == 1) bits(bits(4) * (unicode ? 16 : 8));  // the title, skipped
            else if (type != 2) throw new IllegalArgumentException("song type " + type);
            int patterns = bits(8);
            int scale = 1, bpm = 63, vol = 7;
            for (int p = 0; p < patterns; p++) {
                if (bits(3) != 0) throw new IllegalArgumentException("pattern header");
                int id = bits(2), loop = bits(4), count = bits(8), first = n;
                if (count == 0 && patternFrom[id] != null) {  // plays a pattern given before
                    int[] f = patternFrom[id];
                    for (int i = 0; i < f[1]; i++) add(notes[f[0] + i], ms[f[0] + i], vols[f[0] + i]);
                }
                for (int i = 0; i < count; i++) {
                    int ins = bits(3);
                    if (ins == 1) {
                        int v = bits(4), len = bits(3), dots = bits(2);
                        long t = 240000L / ((long) bpm << Math.min(len, 5));
                        if (dots == 1) t = t * 3 / 2;
                        else if (dots == 2) t = t * 7 / 4;
                        else if (dots == 3) t = t * 2 / 3;
                        add(v >= 1 && v <= 12 ? 69 + 12 * scale + v - 10 : -1, (int) t, vol * 100 / 15);
                    } else if (ins == 2) {
                        scale = bits(2);
                    } else if (ins == 3) {
                        bits(2);
                    } else if (ins == 4) {
                        bpm = BPM[bits(5)];
                    } else if (ins == 5) {
                        vol = bits(4);
                    } else {
                        throw new IllegalArgumentException("instruction " + ins);
                    }
                }
                int len = n - first;
                if (count > 0) patternFrom[id] = new int[] {first, len};
                for (int r = 0; r < loop && loop < 15; r++)
                    for (int i = 0; i < len; i++) add(notes[first + i], ms[first + i], vols[first + i]);
            }
        }

        private void add(int note, int t, int v) {
            if (n >= 5000) return;
            if (n == notes.length) {
                notes = grow(notes);
                ms = grow(ms);
                vols = grow(vols);
            }
            notes[n] = note;
            ms[n] = t;
            vols[n] = v;
            n++;
        }

        private static int[] grow(int[] a) {
            int[] b = new int[a.length * 2];
            System.arraycopy(a, 0, b, 0, a.length);
            return b;
        }
    }
}
