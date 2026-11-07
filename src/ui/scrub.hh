// How far a drag on a number field moves its value. Kept apart from the
// widget (no ImGui) so editor_tests can check it.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace ui {

struct ScrubRate {
    float perPixel;  // value change per pixel dragged
    float step;      // the value is rounded to a multiple of this
};

// A whole-number field (pixels, frames) moves one per pixel, a tenth with
// Shift. Any other grows with the value: 0.01 per pixel below 1, then 1% of
// it, rounded to the power of ten under that (a tenth with Shift).
inline ScrubRate scrubRate(float value, bool integer, bool fine) {
    if (integer) return {fine ? 0.1f : 1.0f, 1.0f};
    const float perPixel = std::max(0.01f, std::fabs(value) * 0.01f) * (fine ? 0.1f : 1.0f);
    return {perPixel, std::pow(10.0f, std::floor(std::log10(perPixel)))};
}

// One frame of a drag: `dx` pixels move the unrounded value `acc` (kept
// across the drag); returns the value to show and store.
inline float scrubTo(float& acc, float dx, bool integer, bool fine) {
    const ScrubRate r = scrubRate(acc, integer, fine);
    acc += dx * r.perPixel;
    return std::round(acc / r.step) * r.step;
}

// A drag on a field that stands for several differing values (#263) moves
// each by what the drag has moved the shown one, from `from` to `to`: `own`
// is that object's value when the drag began. Clamped to [lo, hi] for each
// on its own; a whole-number field rounds it.
inline float scrubOffset(float own, float from, float to, float lo, float hi, bool integer) {
    const float v = std::clamp(own + (to - from), lo, hi);
    return integer ? std::round(v) : v;
}

// Up and Down in a number field being typed into (#158) step the typed value
// by this: one for a whole-number field, else a tenth (a hundredth with Shift,
// finer as a scrub is).
inline float keyStep(bool integer, bool fine) { return integer ? 1.0f : fine ? 0.01f : 0.1f; }

// The typed `text` stepped `dir` (+1 Up, -1 Down) by `step`, written back as
// the field shows a number to type over; false, `text` as it was, when it is
// no number (an empty mixed field, a half-typed "-").
inline bool stepTyped(std::string& text, int dir, float step) {
    const char* s = text.c_str();
    char* end = nullptr;
    const float v = std::strtof(s, &end);
    while (end && *end == ' ') ++end;
    if (!end || end == s || *end != 0 || !std::isfinite(v)) return false;
    float next = v + float(dir) * step;
    // Float noise is taken off (0.1 + 0.2 shows 0.3); an off-grid value keeps
    // its offset (0.25 steps to 0.35).
    const float grid = std::round(next / step) * step;
    if (std::fabs(grid - next) < step * 1e-3f) next = grid;
    if (next == 0) next = 0;  // no "-0"
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", static_cast<double>(next));
    text = b;
    return true;
}

}  // namespace ui
