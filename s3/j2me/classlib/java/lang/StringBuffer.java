package java.lang;

public final class StringBuffer extends AbstractStringBuilder {
  public StringBuffer() { super(16); }
  public StringBuffer(int cap) { super(cap); }
  public StringBuffer(String s) { super(s.length() + 16); put(s); }
  public synchronized StringBuffer append(Object o) { put(String.valueOf(o)); return this; }
  public synchronized StringBuffer append(String s) { put(s); return this; }
  public synchronized StringBuffer append(char[] c) { putChars(c, 0, c.length); return this; }
  public synchronized StringBuffer append(char[] c, int o, int n) { putChars(c, o, n); return this; }
  public synchronized StringBuffer append(boolean b) { put(String.valueOf(b)); return this; }
  public synchronized StringBuffer append(char c) { putChar(c); return this; }
  public synchronized StringBuffer append(int i) { put(Integer.toString(i)); return this; }
  public synchronized StringBuffer append(long l) { put(Long.toString(l)); return this; }
  public synchronized StringBuffer append(float f) { put(Float.toString(f)); return this; }
  public synchronized StringBuffer append(double d) { put(Double.toString(d)); return this; }
  public synchronized StringBuffer insert(int at, Object o) { insertAt(at, String.valueOf(o)); return this; }
  public synchronized StringBuffer insert(int at, String s) { insertAt(at, s); return this; }
  public synchronized StringBuffer insert(int at, char[] c) { insertAt(at, new String(c)); return this; }
  public synchronized StringBuffer insert(int at, boolean b) { insertAt(at, String.valueOf(b)); return this; }
  public synchronized StringBuffer insert(int at, char c) { insertAt(at, String.valueOf(c)); return this; }
  public synchronized StringBuffer insert(int at, int i) { insertAt(at, Integer.toString(i)); return this; }
  public synchronized StringBuffer insert(int at, long l) { insertAt(at, Long.toString(l)); return this; }
  public synchronized StringBuffer insert(int at, float f) { insertAt(at, Float.toString(f)); return this; }
  public synchronized StringBuffer insert(int at, double d) { insertAt(at, Double.toString(d)); return this; }
  public synchronized StringBuffer delete(int b, int e) { deleteRange(b, e); return this; }
  public synchronized StringBuffer deleteCharAt(int i) { deleteRange(i, i + 1); return this; }
  public synchronized StringBuffer reverse() { reverseAll(); return this; }
  // declared here too (not only in AbstractStringBuilder) so javac binds calls to StringBuffer, as the JDK's does
  public synchronized String toString() { return new String(value, 0, count); }
  public synchronized int length() { return count; }
  public synchronized int capacity() { return value.length; }
  public synchronized char charAt(int i) { return super.charAt(i); }
  public synchronized void setCharAt(int i, char c) { super.setCharAt(i, c); }
  public synchronized void ensureCapacity(int n) { super.ensureCapacity(n); }
  public synchronized void setLength(int n) { super.setLength(n); }
  public synchronized void getChars(int b, int e, char[] dst, int d) { super.getChars(b, e, dst, d); }
}
