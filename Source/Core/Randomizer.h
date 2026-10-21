#pragma once

// Bounded Randomize (algorithm version 1), Reset-to-initialized helper and parameter context-action logic
// (copy value, validated paste, reset single parameter). JUCE-free, deterministic, never on the audio thread.
// Spec: docs/specs/cdl-common-actions.md.

#include <cstdint>
#include <string>
#include <string_view>

#include "Parameters/ParameterTable.h"

namespace snaral::rnd
{
inline constexpr int algorithmVersion = 1;

/// Lock groups (bit mask). Macros have no lock. Default = nothing locked.
enum LockGroup : unsigned
{
    lockNone = 0,
    lockVoice = 1u << 0,
    lockSub = 1u << 1,
    lockSources = 1u << 2,
    lockMotion = 1u << 3,
    lockDrive = 1u << 4,
    lockTexture = 1u << 5,
    lockEffects = 1u << 6,
    lockAll = 0x7Fu
};
using LockMask = unsigned;

/// Lock group of a randomizable parameter; lockNone for macros. Meaningful only if isRandomizable().
LockGroup lockGroupOf (int paramIndex) noexcept;
/// True when algorithm v1 may change this parameter (macros included).
bool isRandomizable (int paramIndex) noexcept;

/// The lowest/highest domain value Randomize can produce for a parameter (registry range intersected with the
/// safe window). For tests and UI hints. Only valid when isRandomizable().
void safeWindow (int paramIndex, float& lo, float& hi) noexcept;

struct RandomizeResult
{
    bool ok = false;               // false: source snapshot invalid, `snapshot` equals the input
    std::string error;
    ParamSnapshot snapshot;        // candidate (fully valid when ok)
    int changedCount = 0;          // parameters whose value differs from the input
};

/// splitmix64 seeded from the 32-bit seed. One uniform [0,1) draw per eligible parameter in registry order,
/// consumed even for locked parameters. `amount` in [0,1] (default 1): 1 = the full algorithm; 0 = unchanged;
/// in between continuous values move that fraction of the way (in normalised space) towards the random target
/// and discrete values (choice, bool) switch to it when an independent gate draw is below `amount`.
/// Everything not randomizable is copied bit-exactly from `current`.
RandomizeResult randomize (const ParamSnapshot& current, std::uint32_t seed, LockMask locks = lockNone, float amount = 1.0f);

/// Seed from the system clock (message thread; not deterministic by design).
std::uint32_t seedFromClock();
std::string seedToHex (std::uint32_t seed);                  // 8 lower-case hex digits
bool seedFromHex (std::string_view text, std::uint32_t& seed); // 1..8 hex digits, optional 0x prefix

/// The "Initialized" patch: every sound parameter (not pfNoPreset) at its registry default; utility parameters
/// (bypass, solo, mutes) keep the values of `current`.
ParamSnapshot initializedPatch (const ParamSnapshot& current);

// ---------------------------------------------------------------- parameter context actions

/// Plain domain number as text, locale independent, e.g. "1200", "-3.5". Choice: the index.
std::string copyValueText (int paramIndex, float value);

struct PasteResult
{
    bool ok = false;
    float value = 0.0f;      // domain units, exactly as typed (never clamped)
    std::string error;       // user-facing status, e.g. "Out of range (20 to 20000 Hz)"
};
/// Accepts a finite number with an optional unit suffix equal to the parameter's unit (case-insensitive;
/// `k` or `kHz` for Hz parameters = x1000). Choice parameters also accept the choice name; Bool also on/off/true/false.
/// Out-of-range, malformed or non-integral (for integer/choice/bool) input is rejected; nothing is clamped.
PasteResult parsePasteText (int paramIndex, std::string_view text);

/// Registry default of one parameter (context menu "Reset").
float resetParameterValue (int paramIndex) noexcept;
} // namespace snaral::rnd
