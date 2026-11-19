#pragma once
// The saver's editor on the device (docs/saver.md §3, mockup mockups/s3-saver-device.html): a long
// press on the saver, or Cài đặt › Màn chờ › Chỉnh màn chờ, opens it on the page shown (this orientation only).
// The real page full screen, every element outlined; drag to move (4 px steps, snaps to the centre and to other
// elements' edges with a guide line), tap to select: a corner handle resizes (widgets: Nhỏ/Vừa/Lớn) and a sheet
// holds its options (Home's widget rows for widgets), Lên trên/Xuống dưới, Xoá. On top: Huỷ, the pages (‹ ›, a tap
// on the name adds, copies or removes one), Xong. At the bottom: Thêm (Home's widget gallery plus the saver's own
// elements), Mẫu (the templates shared with the web, drawn small by the saver), Cài đặt (background, brightness,
// night, page turns). Xong, or 3 minutes untouched, saves s3.saver_pages; Huỷ leaves it as it was.
namespace saveredit {
void open(void (*closed)() = nullptr);  // over the saver, on the page it shows; closed() after it left
void close(bool save);  // leaves (saving when asked and something changed); nothing when not open
bool isOpen();
void tick();            // from the saver's update: 3 minutes untouched saves and leaves
}  // namespace saveredit
