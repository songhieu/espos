"""ctypes binding of fe/wake_fe.c (host/libwake.dylib): the exact front end the board runs."""
import ctypes
import os

import numpy as np

LIB = ctypes.CDLL(os.path.join(os.path.dirname(__file__), "..", "host", "libwake.dylib"))
BANDS, HOP = 40, 160
_STATE = 64 * 1024  # bigger than sizeof(wfe_t)


class FE:
    def __init__(self, floor_=0.0, off=0.0, scale=0.0):
        self.buf = ctypes.create_string_buffer(_STATE)
        LIB.wfe_init(self.buf, ctypes.c_float(floor_), ctypes.c_float(off), ctypes.c_float(scale))

    def run(self, pcm16, reset=True):
        """int16 array -> (frames, 40) int8."""
        x = np.ascontiguousarray(pcm16, dtype=np.int16)
        n = len(x) // HOP
        out = np.zeros((n, BANDS), dtype=np.int8)
        if reset:
            LIB.wfe_reset(self.buf)
        LIB.wfe_run(self.buf, x.ctypes.data_as(ctypes.c_void_p), ctypes.c_int(len(x)), out.ctypes.data_as(ctypes.c_void_p))
        return out

    def run_float(self, pcm16, reset=True):
        x = np.ascontiguousarray(pcm16, dtype=np.int16)
        n = len(x) // HOP
        out = np.zeros((n, BANDS), dtype=np.float32)
        if reset:
            LIB.wfe_reset(self.buf)
        for f in range(n):
            LIB.wfe_frame_f(self.buf, x[f * HOP:].ctypes.data_as(ctypes.c_void_p), out[f:].ctypes.data_as(ctypes.c_void_p))
        return out
