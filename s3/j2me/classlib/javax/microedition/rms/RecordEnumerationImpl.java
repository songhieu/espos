package javax.microedition.rms;

/**
 * The records of a store that pass the filter, in the comparator's order. With no comparator the order is ascending
 * record id (MIDP leaves it undefined; ascending is what most phones give for a store written in order).
 * The cursor: -1 means "reset" (next gives the first record, previous the last); otherwise it is the position of the
 * record given last.
 */
final class RecordEnumerationImpl implements RecordEnumeration, RecordListener {
    private final RecordStore store;
    private final RecordFilter filter;
    private final RecordComparator comparator;
    private boolean live, destroyed;
    private int[] ids = new int[0];
    private int index = -1;

    RecordEnumerationImpl(RecordStore store, RecordFilter filter, RecordComparator comparator, boolean keepUpdated) {
        this.store = store;
        this.filter = filter;
        this.comparator = comparator;
        rebuild();
        keepUpdated(keepUpdated);
    }

    public synchronized int numRecords() {
        alive();
        return ids.length;
    }

    public byte[] nextRecord() throws InvalidRecordIDException, RecordStoreNotOpenException, RecordStoreException {
        store.checkOpen();
        return store.getRecord(nextRecordId());
    }

    public synchronized int nextRecordId() throws InvalidRecordIDException {
        alive();
        int i = index < 0 ? 0 : index + 1;
        if (i >= ids.length) throw new InvalidRecordIDException("no next record");
        index = i;
        return ids[i];
    }

    public byte[] previousRecord() throws InvalidRecordIDException, RecordStoreNotOpenException,
            RecordStoreException {
        store.checkOpen();
        return store.getRecord(previousRecordId());
    }

    public synchronized int previousRecordId() throws InvalidRecordIDException {
        alive();
        int i = index < 0 ? ids.length - 1 : index - 1;
        if (i < 0) throw new InvalidRecordIDException("no previous record");
        index = i;
        return ids[i];
    }

    public synchronized boolean hasNextElement() {
        alive();
        if (!store.isOpen()) return false;
        return index < 0 ? ids.length > 0 : index + 1 < ids.length;
    }

    public synchronized boolean hasPreviousElement() {
        alive();
        if (!store.isOpen()) return false;
        return index < 0 ? ids.length > 0 : index > 0;
    }

    public synchronized void reset() {
        alive();
        index = -1;
    }

    public synchronized void rebuild() {
        alive();
        int[] all = store.idsNow();
        int n = 0;
        int[] keep = new int[all.length];
        byte[][] recs = new byte[all.length][];
        for (int i = 0; i < all.length; i++) {
            byte[] r = store.peek(all[i]);
            if (r == null) continue;
            if (filter != null && !filter.matches(copy(r))) continue;
            keep[n] = all[i];
            recs[n] = r;
            n++;
        }
        if (comparator != null) {
            // insertion sort, stable: records the comparator calls EQUIVALENT keep id order
            for (int i = 1; i < n; i++) {
                int id = keep[i];
                byte[] r = recs[i];
                int j = i - 1;
                while (j >= 0 && comparator.compare(copy(recs[j]), copy(r)) == RecordComparator.FOLLOWS) {
                    keep[j + 1] = keep[j];
                    recs[j + 1] = recs[j];
                    j--;
                }
                keep[j + 1] = id;
                recs[j + 1] = r;
            }
        }
        int[] r = new int[n];
        System.arraycopy(keep, 0, r, 0, n);
        ids = r;
        index = -1;
    }

    /** What the filter and comparator see: a copy (they may change it); an empty record is null, as getRecord gives. */
    private static byte[] copy(byte[] r) {
        if (r.length == 0) return null;
        byte[] c = new byte[r.length];
        System.arraycopy(r, 0, c, 0, r.length);
        return c;
    }

    public synchronized void keepUpdated(boolean keepUpdated) {
        alive();
        if (keepUpdated == live) return;
        live = keepUpdated;
        if (live) {
            store.addRecordListener(this);
            rebuild();
        } else {
            store.removeRecordListener(this);
        }
    }

    public synchronized boolean isKeptUpdated() {
        alive();
        return live;
    }

    public synchronized void destroy() {
        alive();
        if (live) store.removeRecordListener(this);
        live = false;
        destroyed = true;
        ids = new int[0];
    }

    private void alive() {
        if (destroyed) throw new IllegalStateException("enumeration destroyed");
    }

    // ---- keepUpdated: follow the store, keeping the cursor on the record it was on

    public void recordAdded(RecordStore rs, int recordId) { follow(); }
    public void recordChanged(RecordStore rs, int recordId) { follow(); }

    public synchronized void recordDeleted(RecordStore rs, int recordId) {
        if (destroyed) return;
        int at = -1;
        for (int i = 0; i < ids.length; i++) if (ids[i] == recordId) at = i;
        if (at < 0) return;
        int[] r = new int[ids.length - 1];
        System.arraycopy(ids, 0, r, 0, at);
        System.arraycopy(ids, at + 1, r, at, r.length - at);
        ids = r;
        if (at <= index) index--;  // the cursor's record went: next gives the one after it
    }

    private synchronized void follow() {
        if (destroyed) return;
        int cur = index >= 0 && index < ids.length ? ids[index] : 0;
        rebuild();
        if (cur != 0) for (int i = 0; i < ids.length; i++) if (ids[i] == cur) index = i;
    }
}
