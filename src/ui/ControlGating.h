#pragma once
// src/ui/ControlGating.h
//
// Pure control-gating rules for the main toolbar. Kept header-only and
// widget-free so the policy is unit-testable without a QWidget.

#include "app/AppState.h"

namespace studio::gating {

/// Whether the Start (stream) action may be triggered.
///
/// The simulator is built-in and always startable; hardware sources (MMB0,
/// serial) require a live link — established by a successful connect and
/// revoked when the source reports an error/drop. Nothing is startable while
/// a connect attempt is in flight.
inline bool startEnabled(studio::SourceType type, bool deviceLinkUp, bool connecting)
{
    if (connecting)
        return false;
    if (type == studio::SourceType::Simulated)
        return true;
    return deviceLinkUp;
}

/// Whether the Connect action may be triggered (no re-entrant attempts).
inline bool connectEnabled(bool connecting)
{
    return !connecting;
}

} // namespace studio::gating
