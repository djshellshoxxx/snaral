# Feature Spec: Shared CDL Actions and Bounded Randomize

**Requirement source:** Copied approved baseline at ../standards/AUDIO_APP_BASELINE.md  
**Applies to:** Snaral plug-in and standalone

## Objective
Provide consistent Stop/Panic, Reset, Randomize, Bypass, preset selection, and parameter context actions without affecting device, safety, or host state.

## Actions
- Reset restores documented Snaral sound defaults. It does not clear theme, device selection, MIDI mappings, user presets, or saved factory content.
- Randomize creates a valid sound patch from documented sound-shaping parameters only. Exclude output trim, limiter ceiling/enable, global tune, transpose, MIDI/device settings, host state, and theme. The action must remain useful and preserve the centered-sub invariant.
- Provide a visible seed and “Reproduce” action. Persist the resulting full parameter state plus seed and algorithm version in project state when user saves the patch.
- Optional per-group lock/exclude switches protect Voice, Sub, Mid Sources, Motion, Drive, and Texture during randomization. Defaults are unlocked.
- Stop/Panic immediately clears active voices, release tails, and latched pedal state for this instrument. It never stops host transport or other tracks. The single visible Stop/Panic control may be duplicated in the MIDI/voice section when useful.
- Bypass is a clear instrument output bypass with a click-safe transition; host bypass remains supported. The preset selector row includes Export WAV because Snaral is an instrument with offline rendering.
- Relevant parameter context menu: copy value, validated paste, numerical entry, MIDI learn, unlink mapping, and reset to parameter default. Unavailable actions are omitted.

## Safety
Randomize validates every result against parameter registry limits before atomic publication. It does not change a parameter if the current patch contains invalid state. Seeded output is deterministic within a given algorithm version.

## Acceptance
1. Reset restores sound defaults while preserving user preferences and files.
2. 10,000 seeded randomizations produce only in-range states, finite renders, no stuck notes, and centered sub.
3. Same seed, source state, and algorithm version reproduce identical parameter state.
4. Randomize excludes safety/device/host controls and respects enabled locks.
5. Stop/Panic clears voices and pedal state, and never stops host transport or other tracks.
6. Bypass and Stop/Panic are keyboard-accessible and clearly labeled; bypass is click-safe.
7. The common preset row exposes Export WAV without implying that host effects or host transport are included.
6. Context actions validate pasted values and do not silently remap MIDI.
