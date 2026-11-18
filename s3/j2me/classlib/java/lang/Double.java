package java.lang;

public final class Double {
  public static final double POSITIVE_INFINITY = 1.0 / 0.0, NEGATIVE_INFINITY = -1.0 / 0.0, NaN = 0.0d / 0.0;
  public static final double MAX_VALUE = 1.7976931348623157e+308, MIN_VALUE = 4.9e-324;
  private final double value;
  public Double(double v) { value = v; }
  public double doubleValue() { return value; }
  public float floatValue() { return (float) value; }
  public int intValue() { return (int) value; }
  public byte byteValue() { return (byte) value; }
  public short shortValue() { return (short) value; }
  public long longValue() { return (long) value; }
  public boolean isNaN() { return value != value; }
  public static boolean isNaN(double v) { return v != v; }
  public boolean isInfinite() { return isInfinite(value); }
  public static boolean isInfinite(double v) { return v == POSITIVE_INFINITY || v == NEGATIVE_INFINITY; }
  public int hashCode() { long b = doubleToLongBits(value); return (int) (b ^ (b >>> 32)); }
  public boolean equals(Object o) { return o instanceof Double && doubleToLongBits(((Double) o).value) == doubleToLongBits(value); }
  public String toString() { return toString(value); }
  public static native String toString(double d);
  public static Double valueOf(double d) { return new Double(d); }
  public static Double valueOf(String s) throws NumberFormatException { return new Double(parseDouble(s)); }
  public static native double parseDouble(String s) throws NumberFormatException;
  public static native long doubleToLongBits(double d);
  public static native double longBitsToDouble(long l);
}
