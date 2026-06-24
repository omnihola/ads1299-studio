#pragma once

namespace studio::theme {

// ── Color tokens ──────────────────────────────────────────────────────────────
inline constexpr const char* kBg          = "#0e1116";
inline constexpr const char* kSurface     = "#161b22";
inline constexpr const char* kSurfaceAlt  = "#1c2128";
inline constexpr const char* kBorder      = "#2d333b";
inline constexpr const char* kTextPrimary = "#e6edf3";
inline constexpr const char* kTextMuted   = "#8b949e";
inline constexpr const char* kAccent      = "#3fb6ff";
inline constexpr const char* kAccentHover = "#58c4ff";
inline constexpr const char* kSignal      = "#7ee787";
inline constexpr const char* kOk          = "#3fb950";
inline constexpr const char* kWarn        = "#d29922";
inline constexpr const char* kError       = "#f85149";
inline constexpr const char* kLedOff      = "#484f58";
inline constexpr const char* kWarnBg      = "#2b2200";   // very dark amber tint
inline constexpr const char* kErrorBg     = "#2d0b0b";   // very dark red tint

// ── Font families ─────────────────────────────────────────────────────────────
inline constexpr const char* kMonoFamily  = "\"SF Mono\", \"JetBrains Mono\", Menlo, monospace";
inline constexpr const char* kUiFamily    = "-apple-system, \"Segoe UI\", Inter, sans-serif";

// ── Spacing & geometry ────────────────────────────────────────────────────────
inline constexpr int kRadius = 6;

inline int spacing(int step) { return step * 8; }

} // namespace studio::theme
