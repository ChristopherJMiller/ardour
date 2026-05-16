---
name: arrangement_section
title: Mark and loop an arrangement section
description: Add a section marker and an auto-loop range for an arrangement section (verse, chorus, bridge, etc.).
arguments:
  - name: section_name
    description: Name for the section marker (e.g. "Verse 1", "Chorus", "Bridge").
    required: true
  - name: start_bar
    description: Bar where the section starts (1-based).
    required: true
  - name: bar_count
    description: Length of the section in bars (default 8).
    required: false
---

Mark section `${section_name}` and prepare to loop it.

1. `tempo/list` if you don't already know the time signature — needed to compute the end BBT correctly.
2. Add the section marker at the start: `markers/add { "name": "${section_name}", "type": "section", "bar": ${start_bar}, "beat": 1 }`.
3. Set the auto-loop range covering `${bar_count|8}` bars: `markers/set_auto_loop { "startBar": ${start_bar}, "startBeat": 1, "endBar": ${start_bar} + ${bar_count|8}, "endBeat": 1 }`.
4. Locate the playhead to the marker: `transport/prev_marker` / `transport/next_marker` until at `${section_name}`, or `transport/locate` to the exact sample reported by step 2.

Useful follow-ups: `transport/loop_toggle` to enter loop playback; `markers/list` to see how the new marker fits relative to existing arrangement sections.
