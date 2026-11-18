package java.util;

public class Vector {
  protected Object[] elementData;
  protected int elementCount, capacityIncrement;
  public Vector() { this(10); }
  public Vector(int cap) { this(cap, 0); }
  public Vector(int cap, int inc) { elementData = new Object[Math.max(cap, 1)]; capacityIncrement = inc; }
  public synchronized void copyInto(Object[] a) { System.arraycopy(elementData, 0, a, 0, elementCount); }
  public synchronized void trimToSize() { Object[] a = new Object[elementCount]; System.arraycopy(elementData, 0, a, 0, elementCount); elementData = a; }
  public synchronized void ensureCapacity(int n) {
    if (n > elementData.length) {
      int c = capacityIncrement > 0 ? elementData.length + capacityIncrement : elementData.length * 2;
      Object[] a = new Object[Math.max(c, n)]; System.arraycopy(elementData, 0, a, 0, elementCount); elementData = a;
    }
  }
  public synchronized void setSize(int n) { ensureCapacity(n); for (int i = n; i < elementCount; i++) elementData[i] = null; elementCount = n; }
  public int capacity() { return elementData.length; }
  public int size() { return elementCount; }
  public boolean isEmpty() { return elementCount == 0; }
  public synchronized Enumeration elements() {
    return new Enumeration() {
      int i = 0;
      public boolean hasMoreElements() { return i < elementCount; }
      public Object nextElement() { if (i >= elementCount) throw new NoSuchElementException(); return elementData[i++]; }
    };
  }
  public boolean contains(Object o) { return indexOf(o, 0) >= 0; }
  public int indexOf(Object o) { return indexOf(o, 0); }
  public synchronized int indexOf(Object o, int from) {
    for (int i = from; i < elementCount; i++) if (o == null ? elementData[i] == null : o.equals(elementData[i])) return i;
    return -1;
  }
  public int lastIndexOf(Object o) { return lastIndexOf(o, elementCount - 1); }
  public synchronized int lastIndexOf(Object o, int from) {
    for (int i = from; i >= 0; i--) if (o == null ? elementData[i] == null : o.equals(elementData[i])) return i;
    return -1;
  }
  public synchronized Object elementAt(int i) { if (i >= elementCount) throw new ArrayIndexOutOfBoundsException(i); return elementData[i]; }
  public synchronized Object firstElement() { if (elementCount == 0) throw new NoSuchElementException(); return elementData[0]; }
  public synchronized Object lastElement() { if (elementCount == 0) throw new NoSuchElementException(); return elementData[elementCount - 1]; }
  public synchronized void setElementAt(Object o, int i) { if (i >= elementCount) throw new ArrayIndexOutOfBoundsException(i); elementData[i] = o; }
  public synchronized void removeElementAt(int i) {
    if (i >= elementCount || i < 0) throw new ArrayIndexOutOfBoundsException(i);
    System.arraycopy(elementData, i + 1, elementData, i, elementCount - i - 1);
    elementData[--elementCount] = null;
  }
  public synchronized void insertElementAt(Object o, int i) {
    if (i > elementCount || i < 0) throw new ArrayIndexOutOfBoundsException(i);
    ensureCapacity(elementCount + 1);
    System.arraycopy(elementData, i, elementData, i + 1, elementCount - i);
    elementData[i] = o; elementCount++;
  }
  public synchronized void addElement(Object o) { ensureCapacity(elementCount + 1); elementData[elementCount++] = o; }
  public synchronized boolean removeElement(Object o) { int i = indexOf(o); if (i < 0) return false; removeElementAt(i); return true; }
  public synchronized void removeAllElements() { for (int i = 0; i < elementCount; i++) elementData[i] = null; elementCount = 0; }
  public synchronized String toString() {
    StringBuffer b = new StringBuffer("[");
    for (int i = 0; i < elementCount; i++) { if (i > 0) b.append(", "); b.append(elementData[i]); }
    return b.append("]").toString();
  }
}
