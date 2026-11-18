package java.lang;

public final class Math {
  public static final double E = 2.718281828459045, PI = 3.141592653589793;
  private Math() {}
  public static int abs(int a) { return a < 0 ? -a : a; }
  public static long abs(long a) { return a < 0 ? -a : a; }
  public static float abs(float a) { return a <= 0.0f ? 0.0f - a : a; }
  public static double abs(double a) { return a <= 0.0 ? 0.0 - a : a; }
  public static int max(int a, int b) { return a >= b ? a : b; }
  public static long max(long a, long b) { return a >= b ? a : b; }
  public static float max(float a, float b) {
    if (a != a) return a;
    if (a == 0.0f && b == 0.0f && Float.floatToIntBits(a) == 0x80000000) return b;
    return a >= b ? a : b;
  }
  public static double max(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && Double.doubleToLongBits(a) == 0x8000000000000000L) return b;
    return a >= b ? a : b;
  }
  public static int min(int a, int b) { return a <= b ? a : b; }
  public static long min(long a, long b) { return a <= b ? a : b; }
  public static float min(float a, float b) {
    if (a != a) return a;
    if (a == 0.0f && b == 0.0f && Float.floatToIntBits(b) == 0x80000000) return b;
    return a <= b ? a : b;
  }
  public static double min(double a, double b) {
    if (a != a) return a;
    if (a == 0.0 && b == 0.0 && Double.doubleToLongBits(b) == 0x8000000000000000L) return b;
    return a <= b ? a : b;
  }
  public static native double sin(double a);
  public static native double cos(double a);
  public static native double tan(double a);
  public static native double sqrt(double a);
  public static native double ceil(double a);
  public static native double floor(double a);
  public static double toRadians(double d) { return d / 180.0 * PI; }
  public static double toDegrees(double r) { return r * 180.0 / PI; }
}
