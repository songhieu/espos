package javax.microedition.rms;

import com.espos.j2me.Native;
import java.util.Hashtable;
import java.util.Vector;

/**
 * Record stores kept in memory while open (one instance a name, shared by every opener: an open count) and written
 * whole to the game's folder (Native.rmsSave) after every change. File format (big-endian):
 *   "JRMS", format 1 (byte), authmode (byte), writable (byte), nextId (int), version (int), lastModified (long),
 *   count (int), then count x {id (int), length (int), bytes}.
 */
public class RecordStore {
    public static final int AUTHMODE_PRIVATE = 0, AUTHMODE_ANY = 1;

    static final int MAX_SIZE = 512 * 1024;
    static final int FORMAT = 1;
    private static final int HEADER = 4 + 3 + 4 + 4 + 8 + 4, PER_RECORD = 8;

    /** Tests only: name -> byte[] used instead of the device's files (natives are not there on a PC). */
    static Hashtable files;

    private static final Object LOCK = new Object();
    private static final Hashtable opened = new Hashtable();  // name -> RecordStore

    private final String name;
    private int openCount;
    int nextId = 1, version;
    long lastModified;
    int authmode;
    boolean writable;
    // records by ascending id
    int[] ids = new int[8];
    byte[][] data = new byte[8][];
    int count;
    private final Vector listeners = new Vector();

    RecordStore(String name) { this.name = name; }

    // ---- opening and closing

    public static RecordStore openRecordStore(String recordStoreName, boolean createIfNecessary)
            throws RecordStoreException, RecordStoreFullException, RecordStoreNotFoundException {
        return open(recordStoreName, createIfNecessary, AUTHMODE_PRIVATE, true);
    }

    public static RecordStore openRecordStore(String recordStoreName, boolean createIfNecessary, int authmode,
                                              boolean writable)
            throws RecordStoreException, RecordStoreFullException, RecordStoreNotFoundException {
        if (authmode != AUTHMODE_PRIVATE && authmode != AUTHMODE_ANY) throw new IllegalArgumentException();
        return open(recordStoreName, createIfNecessary, authmode, writable);
    }

    /** Another suite's store: only this suite's own stores exist here. */
    public static RecordStore openRecordStore(String recordStoreName, String vendorName, String suiteName)
            throws RecordStoreException, RecordStoreNotFoundException {
        if (vendorName == null || suiteName == null) throw new IllegalArgumentException();
        if (vendorName.equals(property("MIDlet-Vendor")) && suiteName.equals(property("MIDlet-Name")))
            return open(recordStoreName, false, AUTHMODE_PRIVATE, true);
        checkName(recordStoreName);
        throw new RecordStoreNotFoundException(recordStoreName);
    }

    private static String property(String key) {
        try {
            return Native.appProperty(key);
        } catch (Throwable e) {
            return null;
        }
    }

    private static void checkName(String n) {
        if (n == null || n.length() < 1 || n.length() > 32) throw new IllegalArgumentException("record store name");
    }

    private static RecordStore open(String n, boolean create, int authmode, boolean writable)
            throws RecordStoreException {
        checkName(n);
        synchronized (LOCK) {
            RecordStore rs = (RecordStore) opened.get(n);
            if (rs == null) {
                byte[] b = load(n);
                if (b != null) {
                    rs = decode(n, b);
                } else {
                    if (!create) throw new RecordStoreNotFoundException(n);
                    rs = new RecordStore(n);
                    rs.authmode = authmode;
                    rs.writable = writable;
                    rs.lastModified = System.currentTimeMillis();
                    rs.persist();
                }
                opened.put(n, rs);
            }
            rs.openCount++;
            return rs;
        }
    }

    public void closeRecordStore() throws RecordStoreNotOpenException, RecordStoreException {
        synchronized (LOCK) {
            if (openCount <= 0) throw new RecordStoreNotOpenException(name);
            if (--openCount == 0) {
                if (opened.get(name) == this) opened.remove(name);
                listeners.removeAllElements();
            }
        }
    }

    public static void deleteRecordStore(String recordStoreName)
            throws RecordStoreException, RecordStoreNotFoundException {
        checkName(recordStoreName);
        synchronized (LOCK) {
            if (opened.containsKey(recordStoreName)) throw new RecordStoreException("open: " + recordStoreName);
            if (!remove(recordStoreName)) throw new RecordStoreNotFoundException(recordStoreName);
        }
    }

    /** The suite's stores, or null when there are none. */
    public static String[] listRecordStores() {
        String[] l = list();
        return l == null || l.length == 0 ? null : l;
    }

    public void setMode(int authmode, boolean writable) throws RecordStoreException {
        if (authmode != AUTHMODE_PRIVATE && authmode != AUTHMODE_ANY) throw new IllegalArgumentException();
        synchronized (this) {
            checkOpen();
            this.authmode = authmode;
            this.writable = writable;
            persist();
        }
    }

    // ---- facts

    public String getName() throws RecordStoreNotOpenException {
        checkOpen();
        return name;
    }

    public synchronized int getVersion() throws RecordStoreNotOpenException {
        checkOpen();
        return version;
    }

    public synchronized int getNumRecords() throws RecordStoreNotOpenException {
        checkOpen();
        return count;
    }

    public synchronized int getSize() throws RecordStoreNotOpenException {
        checkOpen();
        return size();
    }

    public synchronized int getSizeAvailable() throws RecordStoreNotOpenException {
        checkOpen();
        return Math.max(0, MAX_SIZE - size());
    }

    public synchronized long getLastModified() throws RecordStoreNotOpenException {
        checkOpen();
        return lastModified;
    }

    public synchronized int getNextRecordID() throws RecordStoreNotOpenException, RecordStoreException {
        checkOpen();
        return nextId;
    }

    public void addRecordListener(RecordListener listener) {
        synchronized (listeners) {
            if (listener != null && openCount > 0 && !listeners.contains(listener)) listeners.addElement(listener);
        }
    }

    public void removeRecordListener(RecordListener listener) {
        synchronized (listeners) {
            listeners.removeElement(listener);
        }
    }

    // ---- records

    public int addRecord(byte[] data, int offset, int numBytes)
            throws RecordStoreNotOpenException, RecordStoreException, RecordStoreFullException {
        byte[] copy = copyIn(data, offset, numBytes);
        int id;
        synchronized (this) {
            checkWritable();
            if (size() + PER_RECORD + numBytes > MAX_SIZE) throw new RecordStoreFullException();
            id = nextId++;
            insert(id, copy);
            changedNow();
        }
        tell(1, id);
        return id;
    }

    public void deleteRecord(int recordId)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        synchronized (this) {
            checkWritable();
            int i = indexOf(recordId);
            count--;
            System.arraycopy(ids, i + 1, ids, i, count - i);
            System.arraycopy(data, i + 1, data, i, count - i);
            data[count] = null;
            changedNow();
        }
        tell(3, recordId);
    }

    public synchronized int getRecordSize(int recordId)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        return data[indexOf(recordId)].length;
    }

    public synchronized int getRecord(int recordId, byte[] buffer, int offset)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        byte[] r = data[indexOf(recordId)];
        if (buffer == null) throw new NullPointerException();
        if (offset < 0 || offset + r.length > buffer.length) throw new ArrayIndexOutOfBoundsException();
        System.arraycopy(r, 0, buffer, offset, r.length);
        return r.length;
    }

    /** A copy of the record; null for a record with no data (MIDP). */
    public synchronized byte[] getRecord(int recordId)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException {
        checkOpen();
        byte[] r = data[indexOf(recordId)];
        if (r.length == 0) return null;
        byte[] c = new byte[r.length];
        System.arraycopy(r, 0, c, 0, r.length);
        return c;
    }

    public void setRecord(int recordId, byte[] newData, int offset, int numBytes)
            throws RecordStoreNotOpenException, InvalidRecordIDException, RecordStoreException,
            RecordStoreFullException {
        byte[] copy = copyIn(newData, offset, numBytes);
        synchronized (this) {
            checkWritable();
            int i = indexOf(recordId);
            if (size() - data[i].length + numBytes > MAX_SIZE) throw new RecordStoreFullException();
            data[i] = copy;
            changedNow();
        }
        tell(2, recordId);
    }

    public RecordEnumeration enumerateRecords(RecordFilter filter, RecordComparator comparator, boolean keepUpdated)
            throws RecordStoreNotOpenException {
        checkOpen();
        return new RecordEnumerationImpl(this, filter, comparator, keepUpdated);
    }

    // ---- inside

    boolean isOpen() { return openCount > 0; }

    void checkOpen() throws RecordStoreNotOpenException {
        if (openCount <= 0) throw new RecordStoreNotOpenException(name);
    }

    private void checkWritable() throws RecordStoreNotOpenException {
        checkOpen();
        // writable/authmode only limit other suites; this suite always writes its own stores
    }

    private static byte[] copyIn(byte[] b, int off, int n) {
        if (n < 0) throw new ArrayIndexOutOfBoundsException();
        if (b == null) {
            if (n > 0) throw new NullPointerException();
            return new byte[0];
        }
        if (off < 0 || off + n > b.length) throw new ArrayIndexOutOfBoundsException();
        byte[] c = new byte[n];
        System.arraycopy(b, off, c, 0, n);
        return c;
    }

    /** The position of a record id, or InvalidRecordIDException. */
    int indexOf(int id) throws InvalidRecordIDException {
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            int m = (lo + hi) >>> 1;
            if (ids[m] < id) lo = m + 1;
            else if (ids[m] > id) hi = m - 1;
            else return m;
        }
        throw new InvalidRecordIDException("record " + id);
    }

    private void insert(int id, byte[] b) {
        if (count == ids.length) {
            int[] i2 = new int[count * 2];
            byte[][] d2 = new byte[count * 2][];
            System.arraycopy(ids, 0, i2, 0, count);
            System.arraycopy(data, 0, d2, 0, count);
            ids = i2;
            data = d2;
        }
        int i = count;
        while (i > 0 && ids[i - 1] > id) i--;  // ids grow, so this is almost always the end
        System.arraycopy(ids, i, ids, i + 1, count - i);
        System.arraycopy(data, i, data, i + 1, count - i);
        ids[i] = id;
        data[i] = b;
        count++;
    }

    private int size() {
        int s = HEADER + count * PER_RECORD;
        for (int i = 0; i < count; i++) s += data[i].length;
        return s;
    }

    private void changedNow() throws RecordStoreException {
        version++;
        lastModified = System.currentTimeMillis();
        persist();
    }

    /** The record ids now, ascending (for enumerations). */
    synchronized int[] idsNow() {
        int[] r = new int[count];
        System.arraycopy(ids, 0, r, 0, count);
        return r;
    }

    /** The record's bytes as stored (not a copy; callers must not change them), or null when it is gone. */
    synchronized byte[] peek(int id) {
        try {
            return data[indexOf(id)];
        } catch (InvalidRecordIDException e) {
            return null;
        }
    }

    /** kind 1 added, 2 changed, 3 deleted: told outside the store's lock. */
    private void tell(int kind, int id) {
        Object[] ls;
        synchronized (listeners) {
            if (listeners.isEmpty()) return;
            ls = new Object[listeners.size()];
            listeners.copyInto(ls);
        }
        for (int i = 0; i < ls.length; i++) {
            RecordListener l = (RecordListener) ls[i];
            try {
                if (kind == 1) l.recordAdded(this, id);
                else if (kind == 2) l.recordChanged(this, id);
                else l.recordDeleted(this, id);
            } catch (Throwable e) {
                e.printStackTrace();
            }
        }
    }

    // ---- the file

    private void persist() throws RecordStoreException {
        if (!save(name, encode())) throw new RecordStoreException("cannot write " + name);
    }

    synchronized byte[] encode() {
        byte[] b = new byte[size()];
        int p = 0;
        b[p++] = 'J';
        b[p++] = 'R';
        b[p++] = 'M';
        b[p++] = 'S';
        b[p++] = FORMAT;
        b[p++] = (byte) authmode;
        b[p++] = (byte) (writable ? 1 : 0);
        p = putInt(b, p, nextId);
        p = putInt(b, p, version);
        p = putInt(b, p, (int) (lastModified >>> 32));
        p = putInt(b, p, (int) lastModified);
        p = putInt(b, p, count);
        for (int i = 0; i < count; i++) {
            byte[] r = data[i];
            p = putInt(b, p, ids[i]);
            p = putInt(b, p, r.length);
            System.arraycopy(r, 0, b, p, r.length);
            p += r.length;
        }
        return b;
    }

    /** A store (not open) from its file; RecordStoreException when the file is not one. */
    static RecordStore decode(String name, byte[] b) throws RecordStoreException {
        if (b.length < HEADER || b[0] != 'J' || b[1] != 'R' || b[2] != 'M' || b[3] != 'S')
            throw new RecordStoreException("not a record store: " + name);
        if (b[4] != FORMAT) throw new RecordStoreException("record store format " + b[4] + ": " + name);
        RecordStore rs = new RecordStore(name);
        rs.authmode = b[5];
        rs.writable = b[6] != 0;
        int p = 7;
        rs.nextId = getInt(b, p);
        rs.version = getInt(b, p + 4);
        rs.lastModified = ((long) getInt(b, p + 8) << 32) | (getInt(b, p + 12) & 0xFFFFFFFFL);
        int n = getInt(b, p + 16);
        p += 20;
        if (n < 0) throw new RecordStoreException("damaged: " + name);
        for (int i = 0; i < n; i++) {
            if (p + 8 > b.length) throw new RecordStoreException("damaged: " + name);
            int id = getInt(b, p), len = getInt(b, p + 4);
            p += 8;
            if (len < 0 || p + len > b.length || id <= 0) throw new RecordStoreException("damaged: " + name);
            byte[] r = new byte[len];
            System.arraycopy(b, p, r, 0, len);
            p += len;
            rs.insert(id, r);
            if (id >= rs.nextId) rs.nextId = id + 1;
        }
        return rs;
    }

    private static int putInt(byte[] b, int p, int v) {
        b[p] = (byte) (v >>> 24);
        b[p + 1] = (byte) (v >>> 16);
        b[p + 2] = (byte) (v >>> 8);
        b[p + 3] = (byte) v;
        return p + 4;
    }

    private static int getInt(byte[] b, int p) {
        return ((b[p] & 0xFF) << 24) | ((b[p + 1] & 0xFF) << 16) | ((b[p + 2] & 0xFF) << 8) | (b[p + 3] & 0xFF);
    }

    // ---- storage: the device's files, or the test table

    private static byte[] load(String n) {
        if (files != null) return (byte[]) files.get(n);
        return Native.rmsLoad(n);
    }

    private static boolean save(String n, byte[] b) {
        if (files != null) {
            files.put(n, b);
            return true;
        }
        return Native.rmsSave(n, b);
    }

    private static boolean remove(String n) {
        if (files != null) return files.remove(n) != null;
        return Native.rmsDelete(n);
    }

    private static String[] list() {
        if (files != null) {
            String[] r = new String[files.size()];
            int i = 0;
            for (java.util.Enumeration e = files.keys(); e.hasMoreElements(); ) r[i++] = (String) e.nextElement();
            return r;
        }
        return Native.rmsList();
    }
}
