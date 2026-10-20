# Feature Spec: Professional Preset Library

**Category:** Product value  
**Depends on:** factory-presets, preset-file-contract, preset-browser

## Objective
Offer curated, production-oriented preset packs that expand the initial factory library while preserving provenance, consistent metadata, and reliable import.

## Behavior
- Packs contain only Snaral preset JSON and a manifest; no external audio, executable scripts, or paths.
- Each patch is tagged by sound role, motion, sub/mid balance, texture, and intended performance mode.
- Pack metadata includes pack ID/version, author, license, compatible minimum plug-in state version, and preset count.
- Pack import validates every file before adding any; duplicate IDs prompt skip/replace/cancel.
- No store, payment, licensing, or activation system is part of this feature spec.

## Acceptance
1. Pack import is atomic and reports each skipped/duplicate/invalid item.
2. All presets meet factory note, level, peak, and centered-sub review requirements.
3. Pack manifest and preset count match actual contents.
4. User can remove an imported pack without deleting separately saved presets.
5. Pack authorship/license data is visible in browser details.
