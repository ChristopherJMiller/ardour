R"mcp(
,{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
}
)mcp"
