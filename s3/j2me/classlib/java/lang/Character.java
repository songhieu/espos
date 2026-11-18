package java.lang;

public final class Character {
  public static final int MIN_RADIX = 2, MAX_RADIX = 36;
  public static final char MIN_VALUE = 0, MAX_VALUE = 0xffff;
  private final char value;
  public Character(char c) { value = c; }
  public char charValue() { return value; }
  public int hashCode() { return value; }
  public boolean equals(Object o) { return o instanceof Character && ((Character) o).value == value; }
  public String toString() { return String.valueOf(value); }
  public static Character valueOf(char c) { return new Character(c); }
  public static boolean isLowerCase(char c) { return (c >= 'a' && c <= 'z') || (c >= 0xdf && c <= 0xff && c != 0xf7); }
  public static boolean isUpperCase(char c) { return (c >= 'A' && c <= 'Z') || (c >= 0xc0 && c <= 0xde && c != 0xd7); }
  public static boolean isDigit(char c) { return c >= '0' && c <= '9'; }
  public static boolean isLetter(char c) { return isLowerCase(c) || isUpperCase(c) || c > 0xff; }
  public static boolean isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
  public static boolean isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
  public static boolean isWhitespace(char c) { return isSpace(c); }
  public static char toLowerCase(char c) { return isUpperCase(c) ? (char) (c + 32) : c; }
  public static char toUpperCase(char c) { return isLowerCase(c) && c != 0xdf && c != 0xff ? (char) (c - 32) : c; }
  public static int digit(char c, int radix) {
    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : -1;
    return d < radix ? d : -1;
  }
  public static char forDigit(int d, int radix) {
    if (d < 0 || d >= radix) return 0;
    return (char) (d < 10 ? '0' + d : 'a' + d - 10);
  }
}
