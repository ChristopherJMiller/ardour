R"mcp(
{
  "tools": [
    {
      "name": "hello_world",
      "title": "Hello World",
      "description": "Return a greeting from Ardour.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "name": {
            "type": "string"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "session_get_info",
      "title": "Session Info",
      "description": "Return basic session and transport info.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "session_save",
      "title": "Save Session",
      "description": "Save current session state, optionally to a snapshot name.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "snapshotName": {
            "type": "string"
          },
          "switchToSnapshot": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "session_undo",
      "title": "Undo",
      "description": "Undo one or more operations from session history.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "count": {
            "type": "integer",
            "minimum": 1
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "session_redo",
      "title": "Redo",
      "description": "Redo one or more operations from session history.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "count": {
            "type": "integer",
            "minimum": 1
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "session_rename",
      "title": "Rename Session",
      "description": "Rename current session.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "newName": {
            "type": "string"
          }
        },
        "required": [
          "newName"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "session_quick_snapshot",
      "title": "Quick Snapshot",
      "description": "Trigger quick snapshot action; optionally switch to the new snapshot.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "switchToSnapshot": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "session_store_mixer_scene",
      "title": "Store Mixer Scene",
      "description": "Store mixer scene by index.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "index": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "index"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "session_recall_mixer_scene",
      "title": "Recall Mixer Scene",
      "description": "Recall mixer scene by index.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "index": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "index"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "session_begin_compound",
      "title": "Begin Compound Edit",
      "description": "Open an undoable compound edit. All subsequent mutating tool calls become part of a single undo step until session_commit_compound is called. Useful when an AI assistant performs a multi-step edit and wants Ctrl+Z to reverse the whole thing.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "name": {
            "type": "string",
            "description": "Human-readable label for the undo entry (default: \"MCP compound edit\")."
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "session_commit_compound",
      "title": "Commit Compound Edit",
      "description": "Close the currently open compound edit. Must be paired with a prior session_begin_compound.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "session_abort_compound",
      "title": "Abort Compound Edit",
      "description": "Discard the currently open compound edit without committing it. Must be paired with a prior session_begin_compound.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "transport_get_state",
      "title": "Transport State",
      "description": "Return transport state (rolling, speed, sample, state).",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "rolling",
          "speed",
          "sample",
          "state"
        ],
        "properties": {
          "rolling": {
            "type": "boolean"
          },
          "speed": {
            "type": "number"
          },
          "sample": {
            "type": "integer"
          },
          "state": {
            "type": "string"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "transport_locate",
      "title": "Transport Locate",
      "description": "Move playhead to an absolute target position. Provide either sample, or bar+beat (fractional beat allowed).",
      "inputSchema": {
        "type": "object",
        "properties": {
          "sample": {
            "type": "integer",
            "minimum": 0
          },
          "bar": {
            "type": "integer",
            "minimum": 1
          },
          "beat": {
            "type": "number",
            "minimum": 1
          }
        },
        "oneOf": [
          {
            "required": [
              "sample"
            ]
          },
          {
            "required": [
              "bar",
              "beat"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "requestedSample",
          "requestedBbt",
          "transport"
        ],
        "properties": {
          "requestedSample": {
            "type": "integer"
          },
          "requestedBbt": {
            "type": "object"
          },
          "transport": {
            "type": "object"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "transport_goto_start",
      "title": "Transport Go To Start",
      "description": "Move playhead to session start. Optional andRoll starts playback after locate.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "andRoll": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "transport_goto_end",
      "title": "Transport Go To End",
      "description": "Move playhead to session end.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "transport_prev_marker",
      "title": "Transport Previous Marker",
      "description": "Move playhead to the previous visible marker/range boundary.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "transport_next_marker",
      "title": "Transport Next Marker",
      "description": "Move playhead to the next visible marker/range boundary.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "transport_loop_toggle",
      "title": "Transport Loop Toggle",
      "description": "Toggle loop playback, or set explicit loop state. Shows loop range if available.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "transport_loop_location",
      "title": "Transport Loop Location",
      "description": "Set loop range. Provide either startSample+endSample, or startBar+startBeat+endBar+endBeat.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "oneOf": [
          {
            "required": [
              "startSample",
              "endSample"
            ]
          },
          {
            "required": [
              "startBar",
              "startBeat",
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "properties": {
          "kind": {
            "type": "string"
          },
          "locationId": {
            "type": "string"
          },
          "name": {
            "type": "string"
          },
          "startSample": {
            "type": "integer"
          },
          "endSample": {
            "type": "integer"
          },
          "startBbt": {
            "type": "object"
          },
          "endBbt": {
            "type": "object"
          },
          "lengthSamples": {
            "type": "integer"
          },
          "hidden": {
            "type": "boolean"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "transport_set_record_enable",
      "title": "Set Global Record Enable",
      "description": "Set session record-enable (global rec arm) state.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "enabled": {
            "type": "boolean"
          }
        },
        "required": [
          "enabled"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "transport_set_speed",
      "title": "Set Transport Speed",
      "description": "Set transport speed (as in OSC set_transport_speed).",
      "inputSchema": {
        "type": "object",
        "properties": {
          "speed": {
            "type": "number"
          }
        },
        "required": [
          "speed"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "transport_play",
      "title": "Transport Play",
      "description": "Start transport playback.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "transport_stop",
      "title": "Transport Stop",
      "description": "Stop transport playback.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "markers_list",
      "title": "Markers List",
      "description": "List all session markers regardless of marker subtype.",
      "inputSchema": {
        "type": "object",
        "properties": {},
        "additionalProperties": false
      }
    },
    {
      "name": "markers_add",
      "title": "Add Marker",
      "description": "Add one marker at current playhead/audible position or a specific bar+beat. If name is omitted or empty, Ardour assigns a default name. Optional type: mark, section (arrangement), scene.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "name": {
            "type": "string"
          },
          "type": {
            "type": "string",
            "enum": [
              "mark",
              "section",
              "scene",
              "arrangement"
            ]
          },
          "bar": {
            "type": "integer",
            "minimum": 1
          },
          "beat": {
            "type": "number",
            "minimum": 1
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "markers_add_range",
      "title": "Add Range Marker",
      "description": "Add one range marker. Provide either startSample+endSample, or startBar+startBeat+endBar+endBeat.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "name": {
            "type": "string"
          },
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "oneOf": [
          {
            "required": [
              "startSample",
              "endSample"
            ]
          },
          {
            "required": [
              "startBar",
              "startBeat",
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "markers_set_auto_loop",
      "title": "Set Auto Loop Range",
      "description": "Set auto-loop range. Provide either startSample+endSample, or startBar+startBeat+endBar+endBeat.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "oneOf": [
          {
            "required": [
              "startSample",
              "endSample"
            ]
          },
          {
            "required": [
              "startBar",
              "startBeat",
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "markers_hide_auto_loop",
      "title": "Hide Auto Loop Range",
      "description": "Hide or show the current auto-loop range without changing its endpoints.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "hidden": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "markers_set_auto_punch",
      "title": "Set Auto Punch Range",
      "description": "Set auto-punch range. Provide either startSample+endSample, or startBar+startBeat+endBar+endBeat.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "oneOf": [
          {
            "required": [
              "startSample",
              "endSample"
            ]
          },
          {
            "required": [
              "startBar",
              "startBeat",
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "markers_hide_auto_punch",
      "title": "Hide Auto Punch Range",
      "description": "Hide or show the current auto-punch range without changing its endpoints.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "hidden": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "markers_delete",
      "title": "Delete Marker",
      "description": "Delete one marker/range by locationId (preferred), or by name with optional sample to disambiguate duplicate names.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "locationId": {
            "type": "string"
          },
          "name": {
            "type": "string"
          },
          "sample": {
            "type": "integer",
            "minimum": 0
          }
        },
        "anyOf": [
          {
            "required": [
              "locationId"
            ]
          },
          {
            "required": [
              "name"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "markers_rename",
      "title": "Rename Marker",
      "description": "Rename one marker/range by locationId (preferred), or by name with optional sample to disambiguate duplicate names.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "locationId": {
            "type": "string"
          },
          "name": {
            "type": "string"
          },
          "sample": {
            "type": "integer",
            "minimum": 0
          },
          "newName": {
            "type": "string"
          }
        },
        "required": [
          "newName"
        ],
        "anyOf": [
          {
            "required": [
              "locationId"
            ]
          },
          {
            "required": [
              "name"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "tracks_list",
      "title": "Tracks List",
      "description": "List session tracks and buses. includeHidden=true includes hidden routes.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "includeHidden": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "tracks"
        ],
        "properties": {
          "tracks": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "tracks_add",
      "title": "Add Track",
      "description": "Add one or more tracks (audio or MIDI). insert can be end/before/after; when before/after and relativeToId is omitted, selected route is used if available.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "type": {
            "type": "string",
            "enum": [
              "audio",
              "midi"
            ]
          },
          "count": {
            "type": "integer",
            "minimum": 1
          },
          "name": {
            "type": "string"
          },
          "inputChannels": {
            "type": "integer",
            "minimum": 1
          },
          "outputChannels": {
            "type": "integer",
            "minimum": 1
          },
          "strictIo": {
            "type": "boolean"
          },
          "insert": {
            "type": "string",
            "enum": [
              "end",
              "before",
              "after"
            ]
          },
          "relativeToId": {
            "type": "string"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "buses_add",
      "title": "Add Bus",
      "description": "Add one or more buses (audio or MIDI). insert can be end/before/after; when before/after and relativeToId is omitted, selected route is used if available.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "type": {
            "type": "string",
            "enum": [
              "audio",
              "midi"
            ]
          },
          "count": {
            "type": "integer",
            "minimum": 1
          },
          "name": {
            "type": "string"
          },
          "inputChannels": {
            "type": "integer",
            "minimum": 1
          },
          "outputChannels": {
            "type": "integer",
            "minimum": 1
          },
          "strictIo": {
            "type": "boolean"
          },
          "insert": {
            "type": "string",
            "enum": [
              "end",
              "before",
              "after"
            ]
          },
          "relativeToId": {
            "type": "string"
          }
        },
        "additionalProperties": false
      }
    },
    {
      "name": "track_get_info",
      "title": "Get Route Info",
      "description": "Get one route info (fader, pan, rec, mute, solo, sends, plugins). Pan uses Ardour convention: 0.0=right, 0.5=center, 1.0=left.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          }
        },
        "required": [
          "id"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "id",
          "name",
          "type"
        ],
        "properties": {
          "id": {
            "type": "string"
          },
          "name": {
            "type": "string"
          },
          "type": {
            "type": "string"
          },
          "sends": {
            "type": "array"
          },
          "plugins": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "track_get_regions",
      "title": "Get Track Regions",
      "description": "List regions in one track playlist. includeHidden=true includes hidden regions.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "includeHidden": {
            "type": "boolean"
          }
        },
        "required": [
          "id"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "id",
          "regions"
        ],
        "properties": {
          "id": {
            "type": "string"
          },
          "regions": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_get_info",
      "title": "Get Region Info",
      "description": "Get one region info. If regionId is omitted, resolves to selected track's top region at playhead. includeAnalysis=true adds audio peak/RMS analysis.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "includeAnalysis": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "type",
          "startSample",
          "endSample",
          "isAudio"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "resolvedVia": {
            "type": "string"
          },
          "type": {
            "type": "string"
          },
          "isAudio": {
            "type": "boolean"
          },
          "audio": {
            "type": [
              "object",
              "null"
            ]
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_set_gain",
      "title": "Set Region Gain",
      "description": "Set one audio region gain using linear gain or dB. Optional invertPolarity toggles polarity.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "linear": {
            "type": "number",
            "minimum": 0
          },
          "db": {
            "type": "number"
          },
          "invertPolarity": {
            "type": "boolean"
          }
        },
        "oneOf": [
          {
            "required": [
              "linear"
            ]
          },
          {
            "required": [
              "db"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "updated",
          "gain"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "updated": {
            "type": "boolean"
          },
          "gain": {
            "type": "object"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_normalize",
      "title": "Normalize Region",
      "description": "Normalize one audio region to targetDb (default 0.0 dBFS).",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "targetDb": {
            "type": "number"
          }
        },
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "targetDb",
          "peakAmplitude",
          "updated",
          "gain"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "targetDb": {
            "type": "number"
          },
          "peakAmplitude": {
            "type": "number"
          },
          "updated": {
            "type": "boolean"
          },
          "gain": {
            "type": "object"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_split",
      "title": "Split Region",
      "description": "Split one region (audio or MIDI) at sample, bar+beat, or current playhead if no split point is provided. If regionId is omitted, uses selected track's top region at playhead.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "sample": {
            "type": "integer",
            "minimum": 0
          },
          "bar": {
            "type": "integer",
            "minimum": 1
          },
          "beat": {
            "type": "number",
            "minimum": 1
          }
        },
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "split",
          "splitSample",
          "createdRegions"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "split": {
            "type": "boolean"
          },
          "splitSample": {
            "type": "integer"
          },
          "createdRegions": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_resize",
      "title": "Resize Region",
      "description": "Resize one region (audio or MIDI) by setting start and/or end timeline boundary using sample or bar+beat. If regionId is omitted, uses selected track's top region at playhead.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "anyOf": [
          {
            "required": [
              "startSample"
            ]
          },
          {
            "required": [
              "startBar",
              "startBeat"
            ]
          },
          {
            "required": [
              "endSample"
            ]
          },
          {
            "required": [
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "startSample",
          "endSample"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "resolvedVia": {
            "type": "string"
          },
          "startSample": {
            "type": "integer"
          },
          "endSample": {
            "type": "integer"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_copy",
      "title": "Copy Region",
      "description": "Copy one region (audio or MIDI) to an absolute position or by delta. deltaBeats can be fractional. Optional trackId copies to another track. If regionId is omitted, uses selected track's top region at playhead.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "trackId": {
            "type": "string"
          },
          "sample": {
            "type": "integer",
            "minimum": 0
          },
          "bar": {
            "type": "integer",
            "minimum": 1
          },
          "beat": {
            "type": "number",
            "minimum": 1
          },
          "deltaSamples": {
            "type": "integer"
          },
          "deltaBeats": {
            "type": "number"
          }
        },
        "oneOf": [
          {
            "required": [
              "sample"
            ]
          },
          {
            "required": [
              "bar",
              "beat"
            ]
          },
          {
            "required": [
              "deltaSamples"
            ]
          },
          {
            "required": [
              "deltaBeats"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "sourceRegionId",
          "startSample",
          "endSample"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "sourceRegionId": {
            "type": "string"
          },
          "resolvedVia": {
            "type": "string"
          },
          "startSample": {
            "type": "integer"
          },
          "endSample": {
            "type": "integer"
          },
          "crossTrack": {
            "type": "boolean"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "region_move",
      "title": "Move Region",
      "description": "Move one region (audio or MIDI) to an absolute position or by delta. deltaBeats can be fractional. Optional trackId moves to another track. If regionId is omitted, uses selected track's top region at playhead.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "trackId": {
            "type": "string"
          },
          "sample": {
            "type": "integer",
            "minimum": 0
          },
          "bar": {
            "type": "integer",
            "minimum": 1
          },
          "beat": {
            "type": "number",
            "minimum": 1
          },
          "deltaSamples": {
            "type": "integer"
          },
          "deltaBeats": {
            "type": "number"
          }
        },
        "oneOf": [
          {
            "required": [
              "sample"
            ]
          },
          {
            "required": [
              "bar",
              "beat"
            ]
          },
          {
            "required": [
              "deltaSamples"
            ]
          },
          {
            "required": [
              "deltaBeats"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "regionId",
          "sourceRegionId",
          "startSample",
          "endSample"
        ],
        "properties": {
          "regionId": {
            "type": "string"
          },
          "sourceRegionId": {
            "type": "string"
          },
          "resolvedVia": {
            "type": "string"
          },
          "startSample": {
            "type": "integer"
          },
          "endSample": {
            "type": "integer"
          },
          "crossTrack": {
            "type": "boolean"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "plugin_get_description",
      "title": "Plugin Description",
      "description": "Get plugin descriptor and parameter metadata for a route plugin.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "id",
          "pluginIndex"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "plugin_set_parameter",
      "title": "Set Plugin Parameter",
      "description": "Set one plugin parameter by parameterIndex or controlId. Use exactly one of value or interface: value sets the parameter's native/internal range; interface sets the normalized/display-mapped control value.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer",
            "minimum": 0
          },
          "parameterIndex": {
            "type": "integer",
            "minimum": 0
          },
          "controlId": {
            "type": "integer",
            "minimum": 0
          },
          "value": {
            "type": "number"
          },
          "interface": {
            "type": "number"
          }
        },
        "required": [
          "id",
          "pluginIndex"
        ],
        "allOf": [
          {
            "oneOf": [
              {
                "required": [
                  "parameterIndex"
                ]
              },
              {
                "required": [
                  "controlId"
                ]
              }
            ]
          },
          {
            "oneOf": [
              {
                "required": [
                  "value"
                ]
              },
              {
                "required": [
                  "interface"
                ]
              }
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "id",
          "pluginIndex",
          "parameterIndex",
          "controlId",
          "value",
          "interface"
        ],
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer"
          },
          "parameterIndex": {
            "type": "integer"
          },
          "controlId": {
            "type": "integer"
          },
          "value": {
            "type": "number"
          },
          "interface": {
            "type": "number"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "plugin_set_enabled",
      "title": "Set Plugin Enabled",
      "description": "Enable or disable one route plugin by index.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer",
            "minimum": 0
          },
          "enabled": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "pluginIndex",
          "enabled"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "plugin_remove",
      "title": "Remove Plugin",
      "description": "Remove one route plugin by index.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "id",
          "pluginIndex"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "removed",
          "id",
          "pluginIndex",
          "plugins"
        ],
        "properties": {
          "removed": {
            "type": "boolean"
          },
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer"
          },
          "plugins": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "plugin_reorder",
      "title": "Reorder Plugin",
      "description": "Move one route plugin from fromIndex to toIndex.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "fromIndex": {
            "type": "integer",
            "minimum": 0
          },
          "toIndex": {
            "type": "integer",
            "minimum": 0
          }
        },
        "required": [
          "id",
          "fromIndex",
          "toIndex"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "moved",
          "id",
          "fromIndex",
          "toIndex",
          "plugins"
        ],
        "properties": {
          "moved": {
            "type": "boolean"
          },
          "id": {
            "type": "string"
          },
          "fromIndex": {
            "type": "integer"
          },
          "toIndex": {
            "type": "integer"
          },
          "plugins": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "plugin_set_position",
      "title": "Set Plugin Position",
      "description": "Move one route plugin between pre-fader and post-fader sections.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer",
            "minimum": 0
          },
          "postFader": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "pluginIndex",
          "postFader"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "moved",
          "id",
          "pluginIndex",
          "preFader",
          "postFader",
          "plugins"
        ],
        "properties": {
          "moved": {
            "type": "boolean"
          },
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer"
          },
          "preFader": {
            "type": "boolean"
          },
          "postFader": {
            "type": "boolean"
          },
          "plugins": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "plugin_list_available",
      "title": "List Available Plugins",
      "description": "List discovered plugins from Ardour's plugin manager. Returns stable pluginId values suitable for plugin_add.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "search": {
            "type": "string"
          },
          "type": {
            "type": "string"
          },
          "includeHidden": {
            "type": "boolean"
          },
          "includeInternal": {
            "type": "boolean"
          }
        },
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "count",
          "plugins"
        ],
        "properties": {
          "count": {
            "type": "integer"
          },
          "plugins": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "plugin_add",
      "title": "Add Plugin",
      "description": "Add one plugin to a route by pluginId (preferred) or uniqueId+type. position inserts at plugin index.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginId": {
            "type": "string"
          },
          "uniqueId": {
            "type": "string"
          },
          "type": {
            "type": "string"
          },
          "position": {
            "type": "integer",
            "minimum": 0
          },
          "enabled": {
            "type": "boolean"
          }
        },
        "required": [
          "id"
        ],
        "allOf": [
          {
            "oneOf": [
              {
                "required": [
                  "pluginId"
                ]
              },
              {
                "required": [
                  "uniqueId"
                ]
              }
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "id",
          "pluginIndex",
          "plugins"
        ],
        "properties": {
          "id": {
            "type": "string"
          },
          "pluginIndex": {
            "type": "integer"
          },
          "plugins": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "track_get_fader",
      "title": "Get Route Fader",
      "description": "Get current route fader as normalized position and dB.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          }
        },
        "required": [
          "id"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_select",
      "title": "Select Track",
      "description": "Select a route in Ardour.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          }
        },
        "required": [
          "id"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_rename",
      "title": "Rename Track",
      "description": "Rename one route by ID.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "newName": {
            "type": "string"
          }
        },
        "required": [
          "id",
          "newName"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_set_mute",
      "title": "Set Track Mute",
      "description": "Set route mute state.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "value": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "value"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_set_solo",
      "title": "Set Track Solo",
      "description": "Set route solo state.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "value": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "value"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_set_rec_enable",
      "title": "Set Record Enable",
      "description": "Set track record-enable state.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "value": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "value"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_set_rec_safe",
      "title": "Set Record Safe",
      "description": "Set track record-safe state.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "value": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "value"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_set_pan",
      "title": "Set Pan",
      "description": "Set route pan position (Ardour convention: 0.0=right, 0.5=center, 1.0=left).",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "position": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          }
        },
        "required": [
          "id",
          "position"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_set_send_level",
      "title": "Set Send Level",
      "description": "Set route send level by send index using normalized position (0.0 to 1.0) or dB. For dB mode, valid range is -193.0 to +6.0, and -193.0 is silence floor.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "sendIndex": {
            "type": "integer",
            "minimum": 0
          },
          "position": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "db": {
            "type": "number",
            "minimum": -193.0,
            "maximum": 6.0
          }
        },
        "required": [
          "id",
          "sendIndex"
        ],
        "oneOf": [
          {
            "required": [
              "position"
            ]
          },
          {
            "required": [
              "db"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "track_add_send",
      "title": "Add Send",
      "description": "Add one internal aux send from a source route to a target route. Optional level and enabled state are applied to the resolved send. For dB mode, valid range is -193.0 to +6.0, and -193.0 is silence floor.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "targetId": {
            "type": "string"
          },
          "position": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "db": {
            "type": "number",
            "minimum": -193.0,
            "maximum": 6.0
          },
          "enabled": {
            "type": "boolean"
          },
          "postFader": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "targetId"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "created",
          "send",
          "sends"
        ],
        "properties": {
          "created": {
            "type": "boolean"
          },
          "send": {
            "type": "object"
          },
          "sends": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "track_set_send_position",
      "title": "Set Send Position",
      "description": "Set send pre/post-fader position for an existing internal aux send.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "sendIndex": {
            "type": "integer",
            "minimum": 0
          },
          "postFader": {
            "type": "boolean"
          }
        },
        "required": [
          "id",
          "sendIndex",
          "postFader"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "id",
          "routeName",
          "sendIndex",
          "preFader",
          "postFader"
        ],
        "properties": {
          "id": {
            "type": "string"
          },
          "routeName": {
            "type": "string"
          },
          "sendIndex": {
            "type": "integer"
          },
          "preFader": {
            "type": "boolean"
          },
          "postFader": {
            "type": "boolean"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "track_remove_send",
      "title": "Remove Send",
      "description": "Remove one route send by send index. Optional targetId can be used as a safety check for internal sends.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "sendIndex": {
            "type": "integer",
            "minimum": 0
          },
          "targetId": {
            "type": "string"
          }
        },
        "required": [
          "id",
          "sendIndex"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "removed",
          "routeId",
          "routeName",
          "sendIndex",
          "sends"
        ],
        "properties": {
          "removed": {
            "type": "boolean"
          },
          "routeId": {
            "type": "string"
          },
          "routeName": {
            "type": "string"
          },
          "sendIndex": {
            "type": "integer"
          },
          "sends": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "track_set_fader",
      "title": "Set Route Fader",
      "description": "Set route fader by normalized position (0.0 to 1.0) or dB. For dB mode, valid range is -193.0 to +6.0, and -193.0 is silence floor.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "id": {
            "type": "string"
          },
          "position": {
            "type": "number",
            "minimum": 0,
            "maximum": 1
          },
          "db": {
            "type": "number",
            "minimum": -193.0,
            "maximum": 6.0
          }
        },
        "required": [
          "id"
        ],
        "oneOf": [
          {
            "required": [
              "position"
            ]
          },
          {
            "required": [
              "db"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "midi_region_add",
      "title": "Add MIDI Region",
      "description": "Create an empty MIDI region on a MIDI track. Provide either startSample+endSample, or startBar+startBeat+endBar+endBeat.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "trackId": {
            "type": "string"
          },
          "name": {
            "type": "string"
          },
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "required": [
          "trackId"
        ],
        "oneOf": [
          {
            "required": [
              "startSample",
              "endSample"
            ]
          },
          {
            "required": [
              "startBar",
              "startBeat",
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "created"
        ],
        "properties": {
          "created": {
            "type": "object"
          },
          "usedDefaultName": {
            "type": "boolean"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "midi_note_add",
      "title": "Add MIDI Note",
      "description": "Add one MIDI note to an existing MIDI region at region beats, sample, or bar+beat position.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "note": {
            "type": "integer",
            "minimum": 0,
            "maximum": 127
          },
          "velocity": {
            "type": "integer",
            "minimum": 0,
            "maximum": 127
          },
          "channel": {
            "type": "integer",
            "minimum": 1,
            "maximum": 16
          },
          "lengthBeats": {
            "type": "number",
            "exclusiveMinimum": 0
          },
          "regionBeat": {
            "type": "number",
            "minimum": 0
          },
          "sample": {
            "type": "integer",
            "minimum": 0
          },
          "bar": {
            "type": "integer",
            "minimum": 1
          },
          "beat": {
            "type": "number",
            "minimum": 1
          }
        },
        "required": [
          "regionId",
          "note",
          "lengthBeats"
        ],
        "oneOf": [
          {
            "required": [
              "regionBeat"
            ]
          },
          {
            "required": [
              "sample"
            ]
          },
          {
            "required": [
              "bar",
              "beat"
            ]
          }
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "midi_note_list",
      "title": "List MIDI Notes",
      "description": "List all MIDI notes in a MIDI region.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          }
        },
        "required": [
          "regionId"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "midi_note_edit",
      "title": "Edit MIDI Notes",
      "description": "Edit or delete MIDI notes by noteId in a MIDI region.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "edits": {
            "type": "array",
            "minItems": 1,
            "items": {
              "type": "object",
              "properties": {
                "noteId": {
                  "type": "integer"
                },
                "delete": {
                  "type": "boolean"
                },
                "note": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 127
                },
                "deltaSemitones": {
                  "type": "integer"
                },
                "velocity": {
                  "type": "integer",
                  "minimum": 0,
                  "maximum": 127
                },
                "deltaVelocity": {
                  "type": "integer"
                },
                "channel": {
                  "type": "integer",
                  "minimum": 1,
                  "maximum": 16
                },
                "startBeats": {
                  "type": "number",
                  "minimum": 0
                },
                "deltaBeats": {
                  "type": "number"
                },
                "lengthBeats": {
                  "type": "number",
                  "exclusiveMinimum": 0
                }
              },
              "required": [
                "noteId"
              ],
              "additionalProperties": false
            }
          }
        },
        "required": [
          "regionId",
          "edits"
        ],
        "additionalProperties": false
      }
    },
    {
      "name": "midi_note_import_json",
      "title": "Import MIDI JSON",
      "description": "Import MIDI JSON into an existing MIDI region (regionId), or create/populate a new MIDI region on a track (trackId plus range endpoints). midi.midi_events is required. Event positions (bar/b) are relative to region start: bar 1 beat 1 = region start (not absolute session time). b may be fractional (e.g. 1.5 = off-beat 8th note in 4/4), and t is tick offset within the beat using ticks_per_quarter. Channel numbering is 1..16 by default (channel_base=\"one\"). For legacy 0..15 payloads set channel_base=\"zero\". In drum mode (is_drum_mode=true), note_off events are optional; hits may be provided as note_on only.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "midi": {
            "type": "object",
            "properties": {
              "channel_base": {
                "type": "string",
                "enum": [
                  "one",
                  "zero"
                ]
              },
              "channel": {
                "type": "integer",
                "minimum": 0,
                "maximum": 16
              },
              "is_drum_mode": {
                "type": "boolean"
              },
              "ticks_per_quarter": {
                "type": "integer",
                "minimum": 1,
                "maximum": 96000
              },
              "time_signature": {
                "type": "string"
              },
              "midi_events": {
                "type": "array",
                "minItems": 1,
                "items": {
                  "type": "object",
                  "properties": {
                    "bar": {
                      "type": "integer",
                      "minimum": 1
                    },
                    "repeat": {
                      "type": "integer",
                      "minimum": 0
                    },
                    "b": {
                      "type": "number",
                      "minimum": 1
                    },
                    "t": {
                      "type": "integer",
                      "minimum": 0
                    },
                    "n": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 127
                    },
                    "v": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 127
                    },
                    "type": {
                      "type": "string",
                      "enum": [
                        "note_on",
                        "note_off"
                      ]
                    },
                    "channel": {
                      "type": "integer",
                      "minimum": 0,
                      "maximum": 16
                    },
                    "comment": {
                      "type": "string"
                    }
                  },
                  "oneOf": [
                    {
                      "required": [
                        "comment"
                      ]
                    },
                    {
                      "required": [
                        "bar",
                        "repeat"
                      ]
                    },
                    {
                      "required": [
                        "bar",
                        "b",
                        "n"
                      ]
                    }
                  ],
                  "additionalProperties": false
                }
              }
            },
            "required": [
              "midi_events"
            ],
            "additionalProperties": false
          },
          "regionId": {
            "type": "string"
          },
          "trackId": {
            "type": "string"
          },
          "name": {
            "type": "string"
          },
          "startSample": {
            "type": "integer",
            "minimum": 0
          },
          "endSample": {
            "type": "integer",
            "minimum": 0
          },
          "startBar": {
            "type": "integer",
            "minimum": 1
          },
          "startBeat": {
            "type": "number",
            "minimum": 1
          },
          "endBar": {
            "type": "integer",
            "minimum": 1
          },
          "endBeat": {
            "type": "number",
            "minimum": 1
          }
        },
        "required": [
          "midi"
        ],
        "oneOf": [
          {
            "required": [
              "regionId"
            ]
          },
          {
            "required": [
              "trackId",
              "startSample",
              "endSample"
            ]
          },
          {
            "required": [
              "trackId",
              "startBar",
              "startBeat",
              "endBar",
              "endBeat"
            ]
          }
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "createdRegion",
          "region",
          "summary"
        ],
        "properties": {
          "createdRegion": {
            "type": "boolean"
          },
          "region": {
            "type": "object"
          },
          "summary": {
            "type": "object"
          },
          "warnings": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    },
    {
      "name": "midi_note_get_json",
      "title": "Export MIDI JSON",
      "description": "Export all notes from a MIDI region as import-compatible MIDI JSON (standard mode with note_on/note_off events). Export uses channel_base=\"one\" and channels 1..16.",
      "inputSchema": {
        "type": "object",
        "properties": {
          "regionId": {
            "type": "string"
          },
          "ticksPerQuarter": {
            "type": "integer",
            "minimum": 1
          },
          "timeSignature": {
            "type": "string"
          }
        },
        "required": [
          "regionId"
        ],
        "additionalProperties": false
      },
      "outputSchema": {
        "type": "object",
        "required": [
          "region",
          "midi"
        ],
        "properties": {
          "region": {
            "type": "object"
          },
          "midi": {
            "type": "object",
            "required": [
              "channel_base",
              "channel",
              "is_drum_mode",
              "time_signature",
              "ticks_per_quarter",
              "midi_events"
            ],
            "properties": {
              "channel_base": {
                "type": "string",
                "enum": [
                  "one"
                ]
              },
              "channel": {
                "type": "integer"
              },
              "is_drum_mode": {
                "type": "boolean"
              },
              "time_signature": {
                "type": "string"
              },
              "ticks_per_quarter": {
                "type": "integer"
              },
              "midi_events": {
                "type": "array"
              }
            },
            "additionalProperties": true
          },
          "warnings": {
            "type": "array"
          }
        },
        "additionalProperties": true
      }
    }
  ]
}
)mcp"
