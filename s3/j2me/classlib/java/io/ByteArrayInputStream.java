package java.io;

public class ByteArrayInputStream extends InputStream {
  protected byte[] buf;
  protected int pos, mark, count;
  public ByteArrayInputStream(byte[] b) { this(b, 0, b.length); }
  public ByteArrayInputStream(byte[] b, int off, int len) { buf = b; pos = off; mark = off; count = Math.min(off + len, b.length); }
  public synchronized int read() { return pos < count ? buf[pos++] & 255 : -1; }
  public synchronized int read(byte[] b, int off, int len) {
    if (pos >= count) return -1;
    if (len > count - pos) len = count - pos;
    System.arraycopy(buf, pos, b, off, len); pos += len; return len;
  }
  public synchronized long skip(long n) { if (n > count - pos) n = count - pos; if (n < 0) return 0; pos += (int) n; return n; }
  public synchronized int available() { return count - pos; }
  public boolean markSupported() { return true; }
  public synchronized void mark(int limit) { mark = pos; }
  public synchronized void reset() { pos = mark; }
}
