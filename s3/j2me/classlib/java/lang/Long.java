package java.lang;

public final class Long {
  public static final long MIN_VALUE = 0x8000000000000000L, MAX_VALUE = 0x7fffffffffffffffL;
  private final long value;
  public Long(long v) { value = v; }
  public long longValue() { return value; }
  public int intValue() { return (int) value; }
  public float floatValue() { return value; }
  public double doubleValue() { return value; }
  public int hashCode() { return (int) (value ^ (value >>> 32)); }
  public boolean equals(Object o) { return o instanceof Long && ((Long) o).value == value; }
  public String toString() { return toString(value); }
  public static String toString(long i) { return toString(i, 10); }
  public static String toString(long i, int radix) {
    if (radix < 2 || radix > 36) radix = 10;
    if (i == 0) return "0";
    char[] buf = new char[65];
    int at = 65;
    boolean neg = i < 0;
    if (!neg) i = -i;
    while (i != 0) { buf[--at] = Character.forDigit((int) -(i % radix), radix); i /= radix; }
    if (neg) buf[--at] = '-';
    return new String(buf, at, 65 - at);
  }
  public static long parseLong(String s) throws NumberFormatException { return parseLong(s, 10); }
  public static long parseLong(String s, int radix) throws NumberFormatException {
    if (s == null || s.length() == 0) throw new NumberFormatException();
    int i = 0, n = s.length();
    boolean neg = false;
    if (s.charAt(0) == '-' || s.charAt(0) == '+') { neg = s.charAt(0) == '-'; i++; if (n == 1) throw new NumberFormatException(s); }
    long v = 0;
    for (; i < n; i++) {
      int d = Character.digit(s.charAt(i), radix);
      if (d < 0) throw new NumberFormatException(s);
      v = v * radix - d;
    }
    return neg ? v : -v;
  }
  public static Long valueOf(long v) { return new Long(v); }
}
