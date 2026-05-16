---
name: mix_diagnostic
title: Walk through the current mix and surface obvious issues
description: Read-only scan of session state — flags potential issues like clipping levels, missing routing, dramatic pan/mute settings.
arguments: []
---

Audit the current session and report potential mix issues, without making any edits.

1. `session/get_info` — capture session name, sample rate, current tempo.
2. `transport/get_state` — confirm we're stopped so the report is consistent.
3. `tracks/list { "includeHidden": false }` — get all user-visible routes.
4. For each route id returned, in turn:
   - `track/get_info { "id": "<id>" }` — inspect fader dB, pan, mute, solo, rec-enable, plugin chain, sends.
   - Flag any of the following:
     * `fader.db` above +3 dB (likely over-pushed)
     * Multiple `solo: true` routes (probably accidental)
     * `recEnabled: true` on tracks that already have recorded regions (might re-record on next pass)
     * Empty `plugins` array on a track named "Vocal", "Drum", "Bass", etc. (probably wants at least an EQ/compressor)
     * Pan extremes (`pan.position` ~0.0 or ~1.0) on tracks that aren't intentionally hard-panned
5. `markers/list` — flag obvious gaps in the arrangement (e.g. no section markers at all on a multi-bar session).

Summarise findings for the user as a numbered list, grouped by severity (critical / advisory / informational). Do **not** make any edits — this prompt is read-only.
