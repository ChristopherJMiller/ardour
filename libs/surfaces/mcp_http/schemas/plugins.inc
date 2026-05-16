R"mcp(
,{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
}
)mcp"
