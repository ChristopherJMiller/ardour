R"mcp(
,{
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
},{
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
},{
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
}
)mcp"
