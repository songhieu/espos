package java.io;

public class DataInputStream extends InputStream implements DataInput {
  protected InputStream in;
  public DataInputStream(InputStream i) { in = i; }
  public int read() throws IOException { return in.read(); }
  public int read(byte[] b, int off, int len) throws IOException { return in.read(b, off, len); }
  public long skip(long n) throws IOException { return in.skip(n); }
  public int available() throws IOException { return in.available(); }
  public void close() throws IOException { in.close(); }
  public void mark(int l) { in.mark(l); }
  public void reset() throws IOException { in.reset(); }
  public boolean markSupported() { return in.markSupported(); }
  public final void readFully(byte[] b) throws IOException { readFully(b, 0, b.length); }
  public final void readFully(byte[] b, int off, int len) throws IOException {
    while (len > 0) { int n = in.read(b, off, len); if (n < 0) throw new EOFException(); off += n; len -= n; }
  }
  public final int skipBytes(int n) throws IOException { int i = 0; while (i < n && in.read() >= 0) i++; return i; }
  private int one() throws IOException { int c = in.read(); if (c < 0) throw new EOFException(); return c; }
  public final boolean readBoolean() throws IOException { return one() != 0; }
  public final byte readByte() throws IOException { return (byte) one(); }
  public final int readUnsignedByte() throws IOException { return one(); }
  public final short readShort() throws IOException { return (short) ((one() << 8) | one()); }
  public final int readUnsignedShort() throws IOException { return (one() << 8) | one(); }
  public final char readChar() throws IOException { return (char) ((one() << 8) | one()); }
  public final int readInt() throws IOException { return (one() << 24) | (one() << 16) | (one() << 8) | one(); }
  public final long readLong() throws IOException { return ((long) readInt() << 32) | (readInt() & 0xffffffffL); }
  public final float readFloat() throws IOException { return Float.intBitsToFloat(readInt()); }
  public final double readDouble() throws IOException { return Double.longBitsToDouble(readLong()); }
  public final String readUTF() throws IOException { return readUTF(this); }
  public static final String readUTF(DataInput in) throws IOException {
    int n = in.readUnsignedShort();
    byte[] b = new byte[n];
    in.readFully(b);
    char[] c = new char[n];
    int k = 0;
    for (int i = 0; i < n;) {
      int x = b[i++] & 255;
      if (x < 0x80) c[k++] = (char) x;
      else if ((x & 0xe0) == 0xc0) c[k++] = (char) (((x & 0x1f) << 6) | (b[i++] & 0x3f));
      else { c[k++] = (char) (((x & 0x0f) << 12) | ((b[i] & 0x3f) << 6) | (b[i + 1] & 0x3f)); i += 2; }
    }
    return new String(c, 0, k);
  }
}
