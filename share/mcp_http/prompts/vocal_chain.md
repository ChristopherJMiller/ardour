---
name: vocal_chain
title: Set up a vocal chain
description: Standard mixing chain on a vocal track — HPF/EQ, compressor, de-esser, reverb send.
arguments:
  - name: track_id
    description: The track to set up. Pass "selected" to use the currently-selected route.
    required: false
---

Use these MCP tools to build a standard vocal chain on track `${track_id|selected}`:

1. Call `track/get_info { "id": "${track_id|selected}" }` to confirm the track exists and inspect its current plugin chain.
2. Add a high-pass / EQ:
   - `plugin/list_available { "search": "EQ", "type": "lv2" }` — pick a clean EQ (e.g. `ACE EQ`).
   - `plugin/add { "id": "${track_id|selected}", "uniqueId": "<chosen EQ uniqueId>" }`.
   - `plugin/set_parameter` to dial in: HPF at ~80 Hz, gentle 2-3 dB cut around 250-400 Hz, slight presence boost at 4-6 kHz.
3. Add a compressor:
   - `plugin/list_available { "search": "comp", "type": "lv2" }` — `ACE Compressor` is a safe default.
   - `plugin/add` then set ratio ~3:1, threshold so gain reduction sits around -3 to -5 dB, attack ~10 ms, release ~80 ms.
4. (Optional) Add a de-esser after the compressor.
5. Create or find a vocal-reverb bus and route a send:
   - If a bus named "Vocal verb" doesn't exist, `buses/add { "type": "audio", "name": "Vocal verb" }` and add a reverb plugin to it.
   - `track/add_send { "id": "${track_id|selected}", "targetId": "<bus id>", "db": -12, "postFader": true }`.
6. Confirm with `track/get_info` again — the user should see HPF/EQ → compressor → (de-esser) → send-to-reverb.

If you make many edits, wrap the whole sequence in `session/begin_compound` / `session/commit_compound` so undo collapses to a single step.
