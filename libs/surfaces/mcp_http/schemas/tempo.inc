R"mcp(
,{
  "name": "tempo_list",
  "title": "List Tempo Map",
  "description": "Return all tempo and meter markers in the session's tempo map, each with its BBT position, sample, and parameters.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
  "name": "tempo_add_tempo",
  "title": "Add Tempo Marker",
  "description": "Insert a tempo marker at a BBT position. Use endBpm to ramp from bpm to endBpm; omit it for a constant tempo. Wrapped in an undoable command.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "bbt": {
        "type": "object",
        "properties": {
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
          "bar",
          "beat"
        ],
        "additionalProperties": false
      },
      "bpm": {
        "type": "number",
        "exclusiveMinimum": 0,
        "maximum": 999
      },
      "endBpm": {
        "type": "number",
        "exclusiveMinimum": 0,
        "maximum": 999,
        "description": "If different from bpm, creates a ramped tempo."
      },
      "noteType": {
        "type": "integer",
        "minimum": 1,
        "maximum": 64,
        "description": "Beat note value (default 4 = quarter)."
      }
    },
    "required": [
      "bbt",
      "bpm"
    ],
    "additionalProperties": false
  }
},{
  "name": "tempo_add_meter",
  "title": "Add Meter (Time Signature) Marker",
  "description": "Insert a meter / time-signature marker at a BBT position. Wrapped in an undoable command.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "bbt": {
        "type": "object",
        "properties": {
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
          "bar",
          "beat"
        ],
        "additionalProperties": false
      },
      "divisionsPerBar": {
        "type": "integer",
        "minimum": 1,
        "maximum": 64
      },
      "noteValue": {
        "type": "integer",
        "minimum": 1,
        "maximum": 64,
        "description": "Denominator of the time signature (4 = quarter, 8 = eighth)."
      }
    },
    "required": [
      "bbt",
      "divisionsPerBar",
      "noteValue"
    ],
    "additionalProperties": false
  }
},{
  "name": "tempo_remove_tempo",
  "title": "Remove Tempo Marker",
  "description": "Remove the tempo marker at an exact BBT position. Use tempo_list first to find existing positions.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "bbt": {
        "type": "object",
        "properties": {
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
          "bar",
          "beat"
        ],
        "additionalProperties": false
      }
    },
    "required": [
      "bbt"
    ],
    "additionalProperties": false
  }
},{
  "name": "tempo_remove_meter",
  "title": "Remove Meter Marker",
  "description": "Remove the meter marker at an exact BBT position. Use tempo_list first to find existing positions.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "bbt": {
        "type": "object",
        "properties": {
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
          "bar",
          "beat"
        ],
        "additionalProperties": false
      }
    },
    "required": [
      "bbt"
    ],
    "additionalProperties": false
  }
}
)mcp"
