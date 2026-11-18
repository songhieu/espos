package java.lang;

public final class Short {
  private final short value;
  public Short(short v) { value = v; }
  public short shortValue() { return value; }
  public String toString() { return String.valueOf(value); }
  public boolean equals(Object o) { return o instanceof Short && ((Short) o).value == value; }
  public int hashCode() { return value; }


  public static final short MIN_VALUE = -32768, MAX_VALUE = 32767;
  public static short parseShort(String s) { return (short) Integer.parseInt(s); }
  public static short parseShort(String s, int r) { return (short) Integer.parseInt(s, r); }
}
