R"mcp(
,{
  "name": "session_get_info",
  "title": "Session Info",
  "description": "Return basic session and transport info.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
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
},{
  "name": "session_commit_compound",
  "title": "Commit Compound Edit",
  "description": "Close the currently open compound edit. Must be paired with a prior session_begin_compound.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
},{
  "name": "session_abort_compound",
  "title": "Abort Compound Edit",
  "description": "Discard the currently open compound edit without committing it. Must be paired with a prior session_begin_compound.",
  "inputSchema": {
    "type": "object",
    "properties": {},
    "additionalProperties": false
  }
}
)mcp"
