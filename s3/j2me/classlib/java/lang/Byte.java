package java.lang;

public final class Byte {
  private final byte value;
  public Byte(byte v) { value = v; }
  public byte byteValue() { return value; }
  public String toString() { return String.valueOf(value); }
  public boolean equals(Object o) { return o instanceof Byte && ((Byte) o).value == value; }
  public int hashCode() { return value; }

  public static final byte MIN_VALUE = -128, MAX_VALUE = 127;
  public static byte parseByte(String s) { return (byte) Integer.parseInt(s); }
  public static byte parseByte(String s, int r) { return (byte) Integer.parseInt(s, r); }

}
