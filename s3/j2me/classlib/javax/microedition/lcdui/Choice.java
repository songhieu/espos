package javax.microedition.lcdui;

public interface Choice {
    int EXCLUSIVE = 1, MULTIPLE = 2, IMPLICIT = 3, POPUP = 4;
    int TEXT_WRAP_DEFAULT = 0, TEXT_WRAP_ON = 1, TEXT_WRAP_OFF = 2;

    int append(String stringPart, Image imagePart);
    void delete(int elementNum);
    void deleteAll();
    int getFitPolicy();
    Font getFont(int elementNum);
    Image getImage(int elementNum);
    int getSelectedFlags(boolean[] selectedArray_return);
    int getSelectedIndex();
    String getString(int elementNum);
    void insert(int elementNum, String stringPart, Image imagePart);
    boolean isSelected(int elementNum);
    void set(int elementNum, String stringPart, Image imagePart);
    void setFitPolicy(int fitPolicy);
    void setFont(int elementNum, Font font);
    void setSelectedFlags(boolean[] selectedArray);
    void setSelectedIndex(int elementNum, boolean selected);
    int size();
}
