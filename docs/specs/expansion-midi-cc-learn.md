# Feature Spec: MIDI CC Learn (Core CDL Requirement)

**Category:** Usability  
**Depends on:** host-integration, parameter-state, macros-ui

**Release scope:** Core CDL-compliant release. The filename is retained for link stability; this feature is not deferred to expansion.

## Objective
Allow hardware controls to operate Snaral’s macros without requiring users to identify CC numbers manually.

## Behavior
- Learn target is one of the five macros or a supported performance control. MIDI learn starts only after explicit user action.
- Support up to 16 per-instance mappings. A mapping stores MIDI channel or omni, CC number, target parameter ID, min/max range, and inversion.
- Incoming mapped CC is converted to normalized target range and follows the same DSP mapping and smoothing as UI/host automation.
- If a CC is already mapped, warn and offer replace, keep both with distinct channels, or cancel. Do not silently remap.
- Pitch bend, sustain CC64, channel pressure, and all-notes-off messages are reserved and cannot be learned as generic CC mappings.
- Mapping state persists in project state. User global templates may be exported separately; no external server.

## Acceptance
1. Learn captures the first eligible CC and reports channel/number.
2. Min/max/invert mappings produce exact endpoints and monotonic intermediate values.
3. Conflicts are visible and resolution is explicit.
4. Reserved messages keep their MIDI behavior and cannot be assigned.
5. Mappings survive project save/reopen and respond without audio-thread blocking.
