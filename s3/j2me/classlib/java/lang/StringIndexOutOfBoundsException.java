package java.lang;

public class StringIndexOutOfBoundsException extends IndexOutOfBoundsException {
  public StringIndexOutOfBoundsException() { super(); }
  public StringIndexOutOfBoundsException(String m) { super(m); }
  public StringIndexOutOfBoundsException(int i) { super("Index: " + i); }
}
