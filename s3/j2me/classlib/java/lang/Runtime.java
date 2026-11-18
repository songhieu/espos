package java.lang;

public class Runtime {
  private static final Runtime runtime = new Runtime();
  private Runtime() {}
  public static Runtime getRuntime() { return runtime; }
  public native void exit(int code);
  public native long freeMemory();
  public native long totalMemory();
  public native void gc();
}
