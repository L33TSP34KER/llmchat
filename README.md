# AI Assistant - Hackable & Customizable

A proactive decision-making assistant built for efficiency, autonomy, and deep customization. It doesn't wait for perfect instructions — it figures things out using its toolset.

## What You Get Out of the Box

### Core Features
- **Proactive Intelligence**: The default system prompt drives autonomous investigation when context is incomplete
- **Memory System**: Persistent storage for critical data across sessions (passwords, paths, decisions)
- **Tool Access**: Built-in tools for weather, calculation, file operations, and terminal execution
- **MCP Server Support**: Connect to external tools via MCP (Model Context Protocol)
- **Customizable Skills**: Blend personas like `coding`, `helpful`, or `creative`

## How Hackable This Is

### 1. Configuration Overhaul
Edit `config.json` to reshape behavior:

```json
{
  "api_endpoint": "YOUR_API_URL",
  "model": "gpt-4o",  // or any model you prefer
  "theme": "terminal",  // casino, minimalist, matrix, etc.
  "include_tools_in_context": true,  // Enable/disable tool introspection
  "skills": [
    {
      "name": "hacker",
      "system_prompt": "You are a security researcher. Audit code for vulnerabilities."
    }
  ]
}
```

### 2. Tool Customization
Every tool is fully configurable:
- **Command substitution**: Replace internal commands with custom scripts
- **Input schemas**: Modify parameter validation and descriptions
- **New tools**: Add your own by extending the `tools` array

### 3. Memory Persistence
The assistant remembers everything you tell it to save:
- **Store**: `save_memory` (keys, values)
- **Retrieve**: `get_memory` (lookups)
- **Audit**: `list_memories` (export)
- **Clear**: `delete_memory` (privacy control)

### 4. MCP Server Integration
Connect to external tools:
```json
{
  "mcp_servers": [
    {
      "name": "github-api",
      "command": "/usr/local/bin/mcp-server",
      "args": ["--token", "ghp_xxx"]
    }
  ]
}
```

### 5. Skill Blending
Mix and match system prompts for different personas:
- Default: Proactive decision-maker
- Add: `"name": "debugger"`, `"name": "writer"`, etc.
- Each skill can override or augment the base behavior

## Quick Start

1. **Copy** `config.example.json` → `config.json`
2. **Edit** the `api_key` and `api_endpoint`
3. **Optional**: Add your own tools under the `tools` array
4. **Run** with `./start.sh` (or your launcher)

## Security Tips
- Change the default `api_key` immediately
- Use `delete_memory` to wipe sensitive data
- Keep `include_tools_in_context` false for privacy-critical sessions

---

*Built for people who want it done, not just discussed.*
