---
name: bus_setup
title: Create an aux bus and route sends into it
description: Create a named bus, optionally insert a plugin, and route sends from a list of source tracks.
arguments:
  - name: bus_name
    description: Display name for the new bus (e.g. "Drum buss", "Vocal verb").
    required: true
  - name: source_track_ids
    description: Comma-separated route IDs (or single "selected") whose post-fader signal will be sent to the bus.
    required: false
  - name: insert_plugin
    description: Optional plugin search string to instantiate on the new bus (e.g. "reverb", "compressor").
    required: false
---

Build a named bus called `${bus_name}` and route the requested tracks into it.

1. Open a compound undo so this can be reversed in one step: `session/begin_compound { "name": "Build ${bus_name}" }`.
2. Create the bus: `buses/add { "type": "audio", "name": "${bus_name}" }`. Capture the returned bus id.
3. If `insert_plugin` is non-empty:
   - `plugin/list_available { "search": "${insert_plugin}" }`, pick the most appropriate uniqueId, then
   - `plugin/add { "id": "<new bus id>", "uniqueId": "<chosen uniqueId>" }`.
4. For each source track in `${source_track_ids}`:
   - `track/add_send { "id": "<source>", "targetId": "<bus id>", "db": -12, "postFader": true }`.
5. `session/commit_compound`.

End with `track/get_info` on the new bus so the user sees the resulting chain and incoming sends.
