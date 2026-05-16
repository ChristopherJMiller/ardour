R"mcp(
,{
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
},{
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
},{
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
},{
  "name": "transport_goto_end",
  "title": "Transport Go To End",
  "description": "Move playhead to session end.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
  "name": "transport_prev_marker",
  "title": "Transport Previous Marker",
  "description": "Move playhead to the previous visible marker/range boundary.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
  "name": "transport_next_marker",
  "title": "Transport Next Marker",
  "description": "Move playhead to the next visible marker/range boundary.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
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
},{
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
},{
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
},{
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
},{
  "name": "transport_play",
  "title": "Transport Play",
  "description": "Start transport playback.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
  "name": "transport_stop",
  "title": "Transport Stop",
  "description": "Stop transport playback.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
}
)mcp"
