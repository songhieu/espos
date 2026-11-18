package com.nokia.mid.ui;

import com.espos.j2me.Native;

/** Lights are the device's own (left as they are); the vibrator is Native.vibrate. */
public class DeviceControl {
    private DeviceControl() {}

    public static void setLights(int num, int level) {
        if (level < 0 || level > 100) throw new IllegalArgumentException();
    }

    public static void flashLights(long duration) {
        if (duration < 0) throw new IllegalArgumentException();
    }

    public static void startVibra(int freq, long duration) {
        if (freq < 0 || freq > 100 || duration < 0) throw new IllegalArgumentException();
        Native.vibrate(freq == 0 ? 0 : (int) Math.min(duration, 60000));
    }

    public static void stopVibra() { Native.vibrate(0); }
}
