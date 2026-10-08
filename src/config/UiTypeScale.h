#pragma once

#include <QString>

/**
 * @brief The app's type scale — every font size in the UI comes from here.
 *
 * Five named steps, one role each, plus the on-image overlay register.
 * Use px(step) inside style-sheet strings and the step itself for
 * setPixelSize()/setFont() in code. The named families are the ones verified
 * present in the runtime image (Noto Sans / Noto Sans Mono, DejaVu
 * fallbacks) — a family that silently falls back is not a type choice.
 */
namespace UiTypeScale {
// Page and panel identity: the Network Status title, modal headlines.
constexpr int kDisplay = 17;
// Dialog, section and card titles.
constexpr int kTitle = 14;
// Base UI text: application font, controls, tab bars, dialog body.
constexpr int kBody = 12;
// Captions, hints, dense table text, state labels.
constexpr int kMeta = 11;
// Badges, markers, axis and legend labels.
constexpr int kMicro = 10;
// On-image metadata (analysis HUD): its own register, below the UI floor.
constexpr int kOverlayMono = 8;

// "12px" as a style-sheet value.
inline QString px(int step) {
    return QString::number(step) + QStringLiteral("px");
}

// Quoted family stacks for style sheets.
inline QString uiFamilyCss() {
    return QStringLiteral("'Noto Sans', 'DejaVu Sans', sans-serif");
}
inline QString monoFamilyCss() {
    return QStringLiteral("'Noto Sans Mono', 'DejaVu Sans Mono', monospace");
}

// Bare family names for QFont.
inline QString uiFamily() {
    return QStringLiteral("Noto Sans");
}
inline QString monoFamily() {
    return QStringLiteral("Noto Sans Mono");
}
}  // namespace UiTypeScale
