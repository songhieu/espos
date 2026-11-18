package java.lang;

public class ArrayIndexOutOfBoundsException extends IndexOutOfBoundsException {
  public ArrayIndexOutOfBoundsException() { super(); }
  public ArrayIndexOutOfBoundsException(String m) { super(m); }
  public ArrayIndexOutOfBoundsException(int i) { super("Index: " + i); }
}
