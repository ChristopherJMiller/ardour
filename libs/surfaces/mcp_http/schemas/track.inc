R"mcp(
,{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
    "additionalProperties": false
  }
},{
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
},{
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
},{
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
},{
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
    "additionalProperties": false
  }
}
)mcp"
