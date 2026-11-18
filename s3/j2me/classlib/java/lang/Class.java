package java.lang;

public final class Class {
  private long vmClass;  // the VM's class (src/j2me): set when the VM makes this mirror
  private Class() {}
  public native String getName();
  public native boolean isInstance(Object o);
  public native boolean isArray();
  public native boolean isInterface();
  public native boolean isAssignableFrom(Class c);
  public static native Class forName(String name) throws ClassNotFoundException;
  public native Object newInstance() throws InstantiationException, IllegalAccessException;
  public native java.io.InputStream getResourceAsStream(String name);
  public String toString() { return (isInterface() ? "interface " : "class ") + getName(); }
}
