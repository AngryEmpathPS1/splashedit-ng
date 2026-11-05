// Where a popup opened from a field goes (#324): under the field, or above it
// when there is no room below, and always inside the screen. Kept apart from
// the widgets (no ImGui) so editor_tests can check it. Positions are in px,
// y down; a popup set at an explicit position skips ImGui's own clamping, so
// this does it instead.
#pragma once

#include <algorithm>

namespace ui {

struct PopupBox {
    float x0, y0, x1, y1;
};

struct PopupPos {
    float x, y;
};

// The top left corner for a popup `w` x `h` opened from `field`, `gap` px off
// it, inside `screen` with `margin` px kept clear. `pivotX` lines the popup up
// with the field as ImGui's pivot does: 0 its left edge on the field's left,
// 1 its right edge on the field's right. It goes under the field; if it would
// cross the screen's bottom there it goes above the field instead, when that
// fits; then it is clamped inside the screen, and one taller than the screen
// keeps its top on screen (the top is where it is read from).
inline PopupPos popupBelow(PopupBox field, float w, float h, PopupBox screen, float pivotX, float gap, float margin) {
    const float left = screen.x0 + margin, top = screen.y0 + margin;
    const float right = screen.x1 - margin, bottom = screen.y1 - margin;
    PopupPos p;
    p.x = pivotX >= 0.5f ? field.x1 - w : field.x0;
    p.y = field.y1 + gap;
    if (p.y + h > bottom) p.y = field.y0 - gap - h;  // no room below: above it (the clamp pulls it in if that does not fit either)
    p.x = std::max(std::min(p.x, right - w), left);
    p.y = std::max(std::min(p.y, bottom - h), top);
    return p;
}

}  // namespace ui
