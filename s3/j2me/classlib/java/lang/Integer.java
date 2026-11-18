package java.lang;

public final class Integer {
  public static final int MIN_VALUE = 0x80000000, MAX_VALUE = 0x7fffffff;
  private final int value;
  public Integer(int v) { value = v; }
  public int intValue() { return value; }
  public long longValue() { return value; }
  public float floatValue() { return value; }
  public double doubleValue() { return value; }
  public byte byteValue() { return (byte) value; }
  public short shortValue() { return (short) value; }
  public int hashCode() { return value; }
  public boolean equals(Object o) { return o instanceof Integer && ((Integer) o).value == value; }
  public String toString() { return toString(value); }
  public static Integer valueOf(int v) { return new Integer(v); }
  public static Integer valueOf(String s) throws NumberFormatException { return new Integer(parseInt(s, 10)); }
  public static Integer valueOf(String s, int r) throws NumberFormatException { return new Integer(parseInt(s, r)); }
  public static String toString(int i) { return toString(i, 10); }
  public static String toString(int i, int radix) { return Long.toString(i, radix); }
  public static String toHexString(int i) { return unsigned(i, 4); }
  public static String toOctalString(int i) { return unsigned(i, 3); }
  public static String toBinaryString(int i) { return unsigned(i, 1); }
  private static String unsigned(int i, int shift) {
    char[] buf = new char[32];
    int at = 32, mask = (1 << shift) - 1;
    do { buf[--at] = Character.forDigit(i & mask, 1 << shift); i >>>= shift; } while (i != 0);
    return new String(buf, at, 32 - at);
  }
  public static int parseInt(String s) throws NumberFormatException { return parseInt(s, 10); }
  public static int parseInt(String s, int radix) throws NumberFormatException {
    long v = Long.parseLong(s, radix);
    if (v < MIN_VALUE || v > MAX_VALUE) throw new NumberFormatException(s);
    return (int) v;
  }
}
