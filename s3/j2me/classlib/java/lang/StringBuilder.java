package java.lang;

public final class StringBuilder extends AbstractStringBuilder {
  public StringBuilder() { super(16); }
  public StringBuilder(int cap) { super(cap); }
  public StringBuilder(String s) { super(s.length() + 16); put(s); }
  public StringBuilder append(Object o) { put(String.valueOf(o)); return this; }
  public StringBuilder append(String s) { put(s); return this; }
  public StringBuilder append(char[] c) { putChars(c, 0, c.length); return this; }
  public StringBuilder append(char[] c, int o, int n) { putChars(c, o, n); return this; }
  public StringBuilder append(boolean b) { put(String.valueOf(b)); return this; }
  public StringBuilder append(char c) { putChar(c); return this; }
  public StringBuilder append(int i) { put(Integer.toString(i)); return this; }
  public StringBuilder append(long l) { put(Long.toString(l)); return this; }
  public StringBuilder append(float f) { put(Float.toString(f)); return this; }
  public StringBuilder append(double d) { put(Double.toString(d)); return this; }
  public StringBuilder insert(int at, Object o) { insertAt(at, String.valueOf(o)); return this; }
  public StringBuilder insert(int at, String s) { insertAt(at, s); return this; }
  public StringBuilder insert(int at, char[] c) { insertAt(at, new String(c)); return this; }
  public StringBuilder insert(int at, boolean b) { insertAt(at, String.valueOf(b)); return this; }
  public StringBuilder insert(int at, char c) { insertAt(at, String.valueOf(c)); return this; }
  public StringBuilder insert(int at, int i) { insertAt(at, Integer.toString(i)); return this; }
  public StringBuilder insert(int at, long l) { insertAt(at, Long.toString(l)); return this; }
  public StringBuilder insert(int at, float f) { insertAt(at, Float.toString(f)); return this; }
  public StringBuilder insert(int at, double d) { insertAt(at, Double.toString(d)); return this; }
  public StringBuilder delete(int b, int e) { deleteRange(b, e); return this; }
  public StringBuilder deleteCharAt(int i) { deleteRange(i, i + 1); return this; }
  public StringBuilder reverse() { reverseAll(); return this; }
  // declared here too (not only in AbstractStringBuilder) so javac binds calls to StringBuilder, as the JDK's does
  public String toString() { return new String(value, 0, count); }
  public int length() { return count; }
  public int capacity() { return value.length; }
  public char charAt(int i) { return super.charAt(i); }
  public void setCharAt(int i, char c) { super.setCharAt(i, c); }
  public void ensureCapacity(int n) { super.ensureCapacity(n); }
  public void setLength(int n) { super.setLength(n); }
  public void getChars(int b, int e, char[] dst, int d) { super.getChars(b, e, dst, d); }
}
