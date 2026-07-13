# MCP Server

VCMI's game client can expose a [Model Context Protocol](https://modelcontextprotocol.io) server,
letting an LLM (Claude, or any other MCP-capable client) fully control the game and read
everything a human player of that client could see: the adventure map, heroes, towns, armies,
artifacts, trade, dialogs, and battles.

The server acts through the same `CCallback`/`CPlayerInterface` a human player uses, so server-side
validation, fog of war, and turn ownership all apply exactly as they would to a human - the LLM is
exactly as powerful as a human sitting at that client, no more.

For the full technical design (netpack-by-netpack mapping, serialization tables, architecture
rationale, and open work) see [`client/mcp/IMPLEMENTATION_PLAN.md`](../../client/mcp/IMPLEMENTATION_PLAN.md).
This page is the operator-facing quick start.

## Building with MCP support

MCP support is a compile-time option, `ENABLE_MCP_SERVER`, on by default outside of minimal/CI
builds. Enable or disable it explicitly with:

```sh
cmake -DENABLE_MCP_SERVER=ON ..
```

It pulls in the vendored [cpp-mcp](https://github.com/hkr04/cpp-mcp) library
(`client/lib/cpp-mcp`, a git submodule - run `git submodule update --init` if it's missing).

## Enabling and configuring

Controlled by the `mcp` section of `settings.json` (schema in
[`config/schemas/settings.json`](../../config/schemas/settings.json)):

| Key | Default | Meaning |
|---|---|---|
| `enabled` | `true` | Start the MCP server on client launch |
| `host` | `"localhost"` | Bind address |
| `port` | `9100` | TCP port |
| `requestTimeoutMs` | `10000` | How long action tools wait for the server to acknowledge a request before returning `status: "pending"` |
| `eventWaitTimeoutMs` | `60000` | Default timeout for `wait_for_event` |
| `journalSize` | `4096` | Number of retained events in the activity journal |

By default the server binds to `localhost` only. Exposing it beyond that is your responsibility -
cpp-mcp does not implement authentication.

## Connecting a client

The server speaks standard MCP over streamable HTTP at `http://<host>:<port>/mcp`. Any MCP client
works; two common ones:

**Claude Code** - add to `.mcp.json` (or via `claude mcp add`):

```json
{
  "mcpServers": {
    "vcmi": {
      "type": "http",
      "url": "http://localhost:9100/mcp"
    }
  }
}
```

**Claude Desktop** - Settings → Connectors → Add custom connector, URL `http://localhost:9100/mcp`.

## How it works

- **Reads** (`get_game_state`, `get_heroes`, `get_hero_details`, `get_tiles`, ...) run under a
  shared lock on `CGameState::mutex` and return the requested slice of state as JSON.
- **Actions** (`move_hero`, `build_building`, `battle_attack`, ...) are dispatched to the main
  thread, and the tool call blocks until the server acknowledges the resulting request (or
  `requestTimeoutMs` elapses). The response is an envelope:

  ```json
  {
    "status": "ok",
    "events": [ { "seq": 42, "type": "heroMoved", "data": { "...": "..." } } ],
    "pendingQueries": []
  }
  ```

  `status` is `"ok"`, `"rejected"` (either the server refused the request - not your turn, not
  owned, insufficient resources, ... - or a client-side validation check failed before any
  request was even sent, e.g. an unknown object id), or `"pending"` (no acknowledgement within
  the timeout; poll with `get_events`/`wait_for_event`). A `"rejected"` response includes an
  `error` field with the reason when the tool can determine one (client-side validation always
  does; a bare server refusal may not). `events` is everything that happened as a consequence of
  the action since it was issued - not just an echo of the action itself, but AI turns,
  level-ups, battle rounds, and so on that may have followed.
- **Dialogs.** Anything that would pop up a blocking window for a human (level-up skill choice,
  "do you want to fight?", garrison exchange, teleport exit picker, market/tavern windows, battle
  results) shows up in `get_pending_queries` and must be answered with `answer_query` before the
  game proceeds. `wait_for_event` is the way to idle a turn loop until either a query opens or an
  event you care about arrives, instead of busy-polling.
- **Static game data** (creatures, artifacts, spells, skills, buildings, hero types, the merged
  mod config) is exposed through `list_*`/`get_config` tools - it's immutable for the session, so
  fetch it once and cache it client-side.

A typical LLM turn loop looks like: `get_game_state` → for each hero, `get_hero_path` /
`get_map_content` to decide a move → `move_hero` → inspect `events`/`pendingQueries` in the
response → `answer_query` for anything that opened → repeat → `end_turn` → `wait_for_event` for
`playerStartsTurn` (or a battle) if it's an AI-controlled opponent's turn next.

## Known limitations

- **Mixing human and LLM control of the same window is not supported yet.** If a dialog is
  answered via `answer_query` while a human is also looking at the client, the GUI window for
  that dialog does not auto-close (see the implementation plan's risk notes for why this is
  harder than it sounds - some dialog windows re-send a request on close, so naively closing them
  from outside risks a duplicate/incorrect request). Play either through the LLM or through the
  GUI for a given session, not both at once.
- **Creature-ability spell casts in battle** (as opposed to hero spellcasting) aren't wired up yet
  - only hero-cast combat spells are supported by `battle_cast_spell`.
- **`get_battle_state`** does not yet report reachable hexes, damage estimates, or turn order -
  only current unit/battlefield state.
- Lobby tools (`lobby_*`, `load_game`, `restart_game`, `return_to_menu`) fire-and-forget rather
  than waiting for a server acknowledgement like in-game action tools do, since pre-game lobby
  packs don't go through the same request/acknowledgement pipeline. Confirm the effect with
  `lobby_get_state` (or `get_game_state` once a game starts).
