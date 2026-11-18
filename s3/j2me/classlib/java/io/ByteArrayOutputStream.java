package java.io;

public class ByteArrayOutputStream extends OutputStream {
  protected byte[] buf;
  protected int count;
  public ByteArrayOutputStream() { this(32); }
  public ByteArrayOutputStream(int n) { buf = new byte[n]; }
  public synchronized void write(int b) { grow(1); buf[count++] = (byte) b; }
  public synchronized void write(byte[] b, int off, int len) { grow(len); System.arraycopy(b, off, buf, count, len); count += len; }
  private void grow(int n) { if (count + n > buf.length) { byte[] b = new byte[Math.max(buf.length * 2, count + n)]; System.arraycopy(buf, 0, b, 0, count); buf = b; } }
  public synchronized byte[] toByteArray() { byte[] b = new byte[count]; System.arraycopy(buf, 0, b, 0, count); return b; }
  public int size() { return count; }
  public synchronized void reset() { count = 0; }
  public String toString() { return new String(buf, 0, count); }
}
