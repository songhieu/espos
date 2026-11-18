package java.lang;

public final class String {
  final char[] value;
  final int offset, count;
  private int hash;

  public String() { value = new char[0]; offset = 0; count = 0; }
  public String(char[] v) { this(v, 0, v.length); }
  public String(char[] v, int off, int n) {
    if (off < 0 || n < 0 || off + n > v.length) throw new StringIndexOutOfBoundsException();
    value = new char[n]; System.arraycopy(v, off, value, 0, n); offset = 0; count = n;
  }
  String(int off, int n, char[] v) { value = v; offset = off; count = n; }  // shares v
  public String(String s) { value = s.value; offset = s.offset; count = s.count; }
  public String(byte[] b) { this(b, 0, b.length); }
  public String(byte[] b, int off, int n) { String s = decode(b, off, n, null); value = s.value; offset = s.offset; count = s.count; }
  public String(byte[] b, String enc) throws java.io.UnsupportedEncodingException { this(b, 0, b.length, enc); }
  public String(byte[] b, int off, int n, String enc) throws java.io.UnsupportedEncodingException {
    String s = decode(b, off, n, enc); value = s.value; offset = s.offset; count = s.count;
  }
  public String(StringBuffer sb) { this(sb.toString()); }
  static native String decode(byte[] b, int off, int n, String enc);  // UTF-8 when enc names it, else ISO-8859-1
  public native byte[] getBytes();
  public byte[] getBytes(String enc) throws java.io.UnsupportedEncodingException { return getBytes0(enc); }
  native byte[] getBytes0(String enc);

  public int length() { return count; }
  public char charAt(int i) {
    if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i);
    return value[offset + i];
  }
  public void getChars(int b, int e, char[] dst, int d) { System.arraycopy(value, offset + b, dst, d, e - b); }
  public char[] toCharArray() { char[] c = new char[count]; System.arraycopy(value, offset, c, 0, count); return c; }
  public boolean equals(Object o) {
    if (this == o) return true;
    if (!(o instanceof String)) return false;
    String s = (String) o;
    if (s.count != count) return false;
    for (int i = 0; i < count; i++) if (value[offset + i] != s.value[s.offset + i]) return false;
    return true;
  }
  public boolean equalsIgnoreCase(String s) {
    if (s == null || s.count != count) return false;
    for (int i = 0; i < count; i++)
      if (Character.toLowerCase(value[offset + i]) != Character.toLowerCase(s.value[s.offset + i])) return false;
    return true;
  }
  public int hashCode() {
    int h = hash;
    if (h == 0) { for (int i = 0; i < count; i++) h = 31 * h + value[offset + i]; hash = h; }
    return h;
  }
  public int compareTo(String s) {
    int n = Math.min(count, s.count);
    for (int i = 0; i < n; i++) { int d = value[offset + i] - s.value[s.offset + i]; if (d != 0) return d; }
    return count - s.count;
  }
  public boolean startsWith(String p, int at) {
    if (at < 0 || at + p.count > count) return false;
    for (int i = 0; i < p.count; i++) if (value[offset + at + i] != p.value[p.offset + i]) return false;
    return true;
  }
  public boolean startsWith(String p) { return startsWith(p, 0); }
  public boolean regionMatches(boolean ignoreCase, int to, String other, int oo, int len) {
    if (oo < 0 || to < 0 || to > (long) count - len || oo > (long) other.count - len) return false;
    for (int i = 0; i < len; i++) {
      char a = value[offset + to + i], b = other.value[other.offset + oo + i];
      if (a == b) continue;
      if (ignoreCase && (Character.toUpperCase(a) == Character.toUpperCase(b) || Character.toLowerCase(a) == Character.toLowerCase(b))) continue;
      return false;
    }
    return true;
  }
  public boolean regionMatches(int to, String other, int oo, int len) { return regionMatches(false, to, other, oo, len); }
  public boolean endsWith(String s) { return startsWith(s, count - s.count); }
  public int indexOf(int c) { return indexOf(c, 0); }
  public int indexOf(int c, int from) {
    for (int i = Math.max(from, 0); i < count; i++) if (value[offset + i] == c) return i;
    return -1;
  }
  public int lastIndexOf(int c) { return lastIndexOf(c, count - 1); }
  public int lastIndexOf(int c, int from) {
    for (int i = Math.min(from, count - 1); i >= 0; i--) if (value[offset + i] == c) return i;
    return -1;
  }
  public int indexOf(String s) { return indexOf(s, 0); }
  public int indexOf(String s, int from) {
    for (int i = Math.max(from, 0); i + s.count <= count; i++) if (startsWith(s, i)) return i;
    return -1;
  }
  public int lastIndexOf(String s) {
    for (int i = count - s.count; i >= 0; i--) if (startsWith(s, i)) return i;
    return -1;
  }
  public String substring(int b) { return substring(b, count); }
  public String substring(int b, int e) {
    if (b < 0 || e > count || b > e) throw new StringIndexOutOfBoundsException();
    return b == 0 && e == count ? this : new String(offset + b, e - b, value);
  }
  public String concat(String s) {
    if (s.count == 0) return this;
    char[] c = new char[count + s.count];
    getChars(0, count, c, 0); s.getChars(0, s.count, c, count);
    return new String(0, c.length, c);
  }
  public String replace(char a, char b) {
    if (a == b || indexOf(a) < 0) return this;
    char[] c = toCharArray();
    for (int i = 0; i < c.length; i++) if (c[i] == a) c[i] = b;
    return new String(0, c.length, c);
  }
  public String toLowerCase() {
    char[] c = toCharArray();
    for (int i = 0; i < c.length; i++) c[i] = Character.toLowerCase(c[i]);
    return new String(0, c.length, c);
  }
  public String toUpperCase() {
    char[] c = toCharArray();
    for (int i = 0; i < c.length; i++) c[i] = Character.toUpperCase(c[i]);
    return new String(0, c.length, c);
  }
  public String trim() {
    int b = 0, e = count;
    while (b < e && value[offset + b] <= ' ') b++;
    while (e > b && value[offset + e - 1] <= ' ') e--;
    return substring(b, e);
  }
  public String toString() { return this; }
  public native String intern();
  public static String valueOf(Object o) { return o == null ? "null" : o.toString(); }
  public static String valueOf(char[] c) { return new String(c); }
  public static String valueOf(char[] c, int o, int n) { return new String(c, o, n); }
  public static String valueOf(boolean b) { return b ? "true" : "false"; }
  public static String valueOf(char c) { return new String(0, 1, new char[] {c}); }
  public static String valueOf(int i) { return Integer.toString(i); }
  public static String valueOf(long l) { return Long.toString(l); }
  public static String valueOf(float f) { return Float.toString(f); }
  public static String valueOf(double d) { return Double.toString(d); }
}
