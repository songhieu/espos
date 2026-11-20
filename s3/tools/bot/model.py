# PlatformIO pre-script (env bot128): the wake model the firmware builds in. The model is the "Gâu" package's own
# file (wake/pkg/dev.espos.wake.gau/gau.ewm); it is never in the public repository (scripts/sync_oss_s3.sh drops every
# *.ewm), so a copy goes to .pio/bot/gau.ewm, and an empty one when the package's file is missing: the firmware then
# builds and runs without the wake word (ear.cpp logs "model refused"; BOOT still talks).
import os
import shutil

Import("env")  # noqa: F821

proj = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(proj, "wake", "pkg", "dev.espos.wake.gau", "gau.ewm")
dst_dir = os.path.join(proj, ".pio", "bot")
dst = os.path.join(dst_dir, "gau.ewm")
os.makedirs(dst_dir, exist_ok=True)
if os.path.isfile(src):
    if not os.path.isfile(dst) or open(src, "rb").read() != open(dst, "rb").read():
        shutil.copyfile(src, dst)
else:
    print("bot128: no wake model (wake/pkg/dev.espos.wake.gau/gau.ewm): building without the wake word")
    open(dst, "wb").write(b"\0")  # one byte, not a model: objcopy refuses an empty file
