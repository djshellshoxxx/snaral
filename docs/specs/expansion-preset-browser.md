# Feature Spec: Preset Browser

**Category:** Usability  
**Depends on:** preset-variation, parameter-state

## Objective
Help users find and audition patches quickly as factory and user libraries grow.

## Behavior
- Provide text search across preset name, description, author, and tags.
- Filter by layer balance (sub-forward/mid-forward/balanced), motion (static/contour/synced/free), texture (clean/noise/metal), factory/user/favorite.
- Sort by name, recently used, and category. Favorites persist per user installation.
- Preview selection loads into a temporary audition state. Confirm applies it; cancel restores the prior state including dirty edits. If host MIDI is active, preview must not interrupt held notes.
- Library scanning and metadata parsing occur off the audio thread. Missing or invalid user files are skipped with an actionable status.
- Browser selection never overwrites source preset files.

## Acceptance
1. Search returns matching tags and metadata case-insensitively within 200 ms for 2,000 indexed presets on the reference test machine.
2. Filters compose predictably and display an empty-state message when no results match.
3. Preview cancel restores all parameters and dirty state exactly.
4. Favorites and last-used ordering survive application restart.
5. Invalid files do not crash or block audio, and error details identify the file.
