package java.lang;

public class Throwable {
  private String message;
  private Object trace;  // filled by the VM: where it was made
  public Throwable() { fillIn(); }
  public Throwable(String m) { message = m; fillIn(); }
  private native void fillIn();
  public String getMessage() { return message; }
  public String toString() { String n = getClass().getName(); return message == null ? n : n + ": " + message; }
  public native void printStackTrace();
}
