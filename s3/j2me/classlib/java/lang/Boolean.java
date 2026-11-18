package java.lang;

public final class Boolean {
  private final boolean value;
  public Boolean(boolean v) { value = v; }
  public boolean booleanValue() { return value; }
  public String toString() { return String.valueOf(value); }
  public boolean equals(Object o) { return o instanceof Boolean && ((Boolean) o).value == value; }
  public int hashCode() { return value ? 1231 : 1237; }
  public static final Boolean TRUE = new Boolean(true), FALSE = new Boolean(false);
  public static Boolean valueOf(boolean b) { return b ? TRUE : FALSE; }


}
