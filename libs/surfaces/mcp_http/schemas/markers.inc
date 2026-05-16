R"mcp(
,{
  "name": "markers_list",
  "title": "Markers List",
  "description": "List all session markers regardless of marker subtype.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
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
},{
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
    "additionalProperties": false
  }
},{
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
    "additionalProperties": false
  }
},{
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
},{
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
    "additionalProperties": false
  }
},{
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
},{
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
    "additionalProperties": false
  }
},{
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
    "additionalProperties": false
  }
}
)mcp"
