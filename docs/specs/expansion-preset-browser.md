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

## Data model and contract (`PresetLibrary`, `PresetBrowserModel`)

Code: `Source/State/Pure/PresetLibrary.*`, `PresetBrowserModel.*`, `PresetFacets.h`. Rescan and file operations run off the audio thread.

**Entry.** Each entry has a library id (`factory:<preset id>`, `user:<file name>`, `pack:<pack id>:<file name>`), origin (Factory, User, Pack), name, author, description, tags, file path (empty for embedded factory presets), pack id for pack presets, derived facets (layer balance, motion bits, texture bits), favourite flag and last-used time.

**Query.** `BrowserQuery` holds text terms (all must match name, tags, author or description, ASCII case-insensitive), required tags (all), origin filter (All, Factory, User, Packs, Favourites), an optional layer-balance filter, a motion mask and a texture mask (any bit in a mask matches), and the sort mode. Different filters AND together. An empty result is a normal outcome; the UI shows the empty-state message.

**Sorting.** Name (case-folded, then id). RecentlyUsed (most recent first, never used last, then name). Category (first tag, case-folded, untagged last, then name). All orders are total and stable.

**Navigation.** `select(id)`, `selectNext` and `selectPrevious` (optional wrap) move within the current results only.

**Modified flag.** `notifyLoaded` records the loaded preset and its sound snapshot. `notifyEdited(current)` recomputes the flag by comparing sound parameters with it, so putting a knob back clears the flag. `needsConfirmationBeforeLoad` is true while modified.

**Favourites and recents.** Stored in `browser_state.json` next to the Presets and Packs folders: lexical keys, `"format": "snaral-browser-state"`, `schema_version` 1, `favourites` (array of library ids) and `recent` (array of `{id, used}`), both capped. Saves are atomic. A corrupt or unknown-format file is quarantined and the browser starts empty, with a note in the scan report. A file from a newer schema is left untouched and favourites stay unavailable for that session. Favourites of entries that are not currently installed are kept.

**Audition.** Contract in `PresetBrowserModel.h`. The editor calls `beginAudition(current)` once (idempotent; it keeps the original state and dirty flag), publishes each previewed preset as one atomic snapshot, and never sends note-offs, resets voices or touches held notes (`auditionMustPreserveHeldNotes`). `confirmAudition` keeps the preview as the loaded preset. Cancel returns an `AuditionRestore` with the original snapshot, dirty flag and loaded id and name, which the editor publishes. The editor records one "Load Preset" undo step only on confirm.

**Errors.** `rescan` returns a `ScanReport`; each skipped file lists its name and the first parse error. Invalid, oversized or unreadable user files never abort a scan. Browsing never writes to preset files.

## Acceptance
1. Search returns matching tags and metadata case-insensitively within 200 ms for 2,000 indexed presets on the reference test machine.
2. Filters compose predictably and display an empty-state message when no results match.
3. Preview cancel restores all parameters and dirty state exactly.
4. Favorites and last-used ordering survive application restart.
5. Invalid files do not crash or block audio, and error details identify the file.
