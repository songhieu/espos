package java.lang.ref;

// CLDC 1.1's references. The VM's collector does not clear them: get() returns the referent until clear().
public abstract class Reference {
  private Object referent;
  Reference(Object o) { referent = o; }
  public Object get() { return referent; }
  public void clear() { referent = null; }
}
