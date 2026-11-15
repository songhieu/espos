#include "InkMenuActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "components/UITheme.h"
#include "inkdesk/InkUi.h"

namespace iu = inkdesk::ui;

const char* InkMenuActivity::okLabel() const { return tr(STR_IK_OPEN); }

void InkMenuActivity::onEnter() {
  Activity::onEnter();
  refresh();
  selected_ = 0;
  scroll_ = 0;
  requestUpdate();
}

void InkMenuActivity::select(const int index) {
  if (items_.empty()) return;
  const int n = static_cast<int>(items_.size());
  selected_ = (index % n + n) % n;
  requestUpdate();
}

void InkMenuActivity::loop() {
  using B = MappedInputManager::Button;
  if (mappedInput.wasReleased(B::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(B::Down)) {
    if (!onSideButton(1)) select(selected_ + 1);
    return;
  }
  if (mappedInput.wasPressed(B::Up)) {
    if (!onSideButton(-1)) select(selected_ - 1);
    return;
  }
  if (mappedInput.wasPressed(B::Right) || mappedInput.wasPressed(B::NavNext)) return select(selected_ + 1);
  if (mappedInput.wasPressed(B::Left) || mappedInput.wasPressed(B::NavPrevious)) return select(selected_ - 1);
  if (mappedInput.wasReleased(B::Confirm) && !items_.empty()) activate(selected_);
}

void InkMenuActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;
  iu::header(renderer, title());
  int y = drawTop(iu::HEADER_H + 12);

  // Note box height first, so the rows know where to stop.
  const std::string note = noteText();
  int noteTop = bottom;
  if (!note.empty()) {
    // Draw into a measured box at the bottom.
    const int lh = renderer.getLineHeight(iu::fontSmall()) + 2;
    const int guessLines = static_cast<int>(renderer.getTextWidth(iu::fontSmall(), note.c_str()) / (w - 80)) + 1;
    const int noteH = (guessLines > 3 ? 3 : guessLines) * lh + 26;
    noteTop = bottom - noteH;
    iu::note(renderer, iu::SIDE + 4, noteTop, w - (iu::SIDE + 4) * 2, note.c_str(), 3);
    noteTop -= 10;
  }

  if (items_.empty()) {
    iu::wrap(renderer, iu::fontBody(), iu::SIDE, y + 10, w - iu::SIDE * 2, emptyText(), 4);
  } else {
    // Keep the selected row visible: rows are laid out from scroll_.
    auto heightOf = [&](const Item& it) { return iu::rowHeight(renderer, !it.sub.empty()) + 6; };
    if (selected_ < scroll_) scroll_ = selected_;
    while (true) {
      int used = 0;
      for (int i = scroll_; i <= selected_; i++) used += heightOf(items_[i]);
      if (used <= noteTop - y || scroll_ >= selected_) break;
      scroll_++;
    }
    int ry = y;
    for (int i = scroll_; i < static_cast<int>(items_.size()); i++) {
      const Item& it = items_[i];
      const int h = heightOf(it);
      if (ry + h > noteTop) break;
      iu::row(renderer, 12, ry, w - 24, it.title.c_str(), it.sub.c_str(), it.right.c_str(), i == selected_, it.icon);
      ry += h;
    }
    // Scroll hint dots on the right edge when the list does not fit.
    if (scroll_ > 0) renderer.fillRoundedRect(w - 8, y, 4, 24, 2, Color::Black);
  }
  iu::hints(renderer, mappedInput, tr(STR_BACK), items_.empty() ? "" : okLabel(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  renderer.displayBuffer();
}
