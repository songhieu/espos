package java.io;

// System.out and System.err: the device's trace (src/j2me natives).
public class PrintStream extends OutputStream {
  private final int fd;
  private OutputStream out;
  public PrintStream(int fd) { this.fd = fd; }
  public PrintStream(OutputStream o) { fd = 0; out = o; }
  private static native void put(int fd, String s);
  public void write(int b) { if (out != null) { try { out.write(b); } catch (IOException e) {} } else put(fd, String.valueOf((char) (b & 255))); }
  public void print(String s) { if (s == null) s = "null"; if (out != null) { byte[] b = s.getBytes(); for (int i = 0; i < b.length; i++) write(b[i]); } else put(fd, s); }
  public void print(Object o) { print(String.valueOf(o)); }
  public void print(char[] c) { print(new String(c)); }
  public void print(boolean b) { print(String.valueOf(b)); }
  public void print(char c) { print(String.valueOf(c)); }
  public void print(int i) { print(String.valueOf(i)); }
  public void print(long l) { print(String.valueOf(l)); }
  public void print(float f) { print(String.valueOf(f)); }
  public void print(double d) { print(String.valueOf(d)); }
  public void println() { print("\n"); }
  public void println(String s) { print(s + "\n"); }
  public void println(Object o) { println(String.valueOf(o)); }
  public void println(char[] c) { println(new String(c)); }
  public void println(boolean b) { println(String.valueOf(b)); }
  public void println(char c) { println(String.valueOf(c)); }
  public void println(int i) { println(String.valueOf(i)); }
  public void println(long l) { println(String.valueOf(l)); }
  public void println(float f) { println(String.valueOf(f)); }
  public void println(double d) { println(String.valueOf(d)); }
  public boolean checkError() { return false; }
}
