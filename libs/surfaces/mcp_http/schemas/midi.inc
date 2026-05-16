R"mcp(
,{
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
},{
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
    "additionalProperties": false
  }
},{
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
},{
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
},{
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
},{
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
)mcp"
