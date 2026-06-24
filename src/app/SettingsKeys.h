#pragma once
// src/app/SettingsKeys.h
//
// Central registry of QSettings keys. Persisted preferences are written in one
// place and read in another (often different files); using shared constants here
// instead of repeating string literals prevents a save-site and a load-site from
// silently drifting apart (a typo'd key fails quietly — nothing persists).
//
// Add new persisted-preference keys here, namespaced by area ("group/name").

namespace studio::settings {

// Main window
inline constexpr auto kWindowGeometry = "mainWindow/geometry";

// Recording
inline constexpr auto kRecordingOutputFolder = "recording/outputFolder";
inline constexpr auto kRecordingWriteCsv     = "recording/writeCsv";

// Acquisition
inline constexpr auto kAcqSampleRate = "acq/sampleRate";
inline constexpr auto kAcqGain       = "acq/gain";

} // namespace studio::settings
