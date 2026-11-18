package java.lang;

// StringBuffer (CLDC) and StringBuilder (what newer javac emits for "a" + b) share this.
abstract class AbstractStringBuilder {
  char[] value;
  int count;
  AbstractStringBuilder(int cap) { value = new char[cap < 1 ? 16 : cap]; }
  public int length() { return count; }
  public int capacity() { return value.length; }
  public char charAt(int i) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); return value[i]; }
  public void setCharAt(int i, char c) { if (i < 0 || i >= count) throw new StringIndexOutOfBoundsException(i); value[i] = c; }
  public void ensureCapacity(int n) {
    if (n > value.length) {
      int c = Math.max(n, value.length * 2 + 2);
      char[] v = new char[c]; System.arraycopy(value, 0, v, 0, count); value = v;
    }
  }
  public void setLength(int n) {
    ensureCapacity(n);
    for (int i = count; i < n; i++) value[i] = 0;
    count = n;
  }
  void put(String s) {
    if (s == null) s = "null";
    ensureCapacity(count + s.count);
    System.arraycopy(s.value, s.offset, value, count, s.count);
    count += s.count;
  }
  void putChar(char c) { ensureCapacity(count + 1); value[count++] = c; }
  void putChars(char[] c, int o, int n) { ensureCapacity(count + n); System.arraycopy(c, o, value, count, n); count += n; }
  void insertAt(int at, String s) {
    if (at < 0 || at > count) throw new StringIndexOutOfBoundsException(at);
    if (s == null) s = "null";
    ensureCapacity(count + s.count);
    System.arraycopy(value, at, value, at + s.count, count - at);
    System.arraycopy(s.value, s.offset, value, at, s.count);
    count += s.count;
  }
  void deleteRange(int b, int e) {
    if (e > count) e = count;
    if (b < 0 || b > e) throw new StringIndexOutOfBoundsException(b);
    System.arraycopy(value, e, value, b, count - e);
    count -= e - b;
  }
  void reverseAll() {
    for (int i = 0, j = count - 1; i < j; i++, j--) { char t = value[i]; value[i] = value[j]; value[j] = t; }
  }
  public void getChars(int b, int e, char[] dst, int d) { System.arraycopy(value, b, dst, d, e - b); }
  public String toString() { return new String(value, 0, count); }
}
