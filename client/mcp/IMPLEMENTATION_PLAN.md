# VCMI Full MCP Support — Implementation Plan

Status: written 2026-07-13, based on branch `mcp` (HEAD `53e2376a1`). **All phases (0-6)
implemented as of 2026-07-14** - see the phase table in section 14 for what each one covers and
its one open caveat (GUI dialog dismissal, deliberately deferred - see the note under phase 5).
User-facing docs: [`docs/developers/MCP_Server.md`](../../docs/developers/MCP_Server.md).

Goal: an LLM connected via MCP can **fully control the game and read all information a human
player of this client could see** — adventure map, towns, heroes, armies, artifacts, trade,
dialogs, battles — with clean, non-duplicated code and **minimal changes to existing code
outside `client/mcp/`**.

---

## Table of contents

1. [Current state](#1-current-state)
2. [Target architecture](#2-target-architecture)
3. [The "wait for result" mechanism](#3-the-wait-for-result-mechanism)
4. [Query / dialog handling](#4-query--dialog-handling)
5. [Netpack analysis: client → server (actions)](#5-netpack-analysis-client--server-actions)
6. [Netpack analysis: server → client (events)](#6-netpack-analysis-server--client-events)
7. [Battle control](#7-battle-control)
8. [JSON serialization: class table](#8-json-serialization-class-table)
9. [Tool catalog (target API surface)](#9-tool-catalog-target-api-surface)
10. [File layout & code-quality rules](#10-file-layout--code-quality-rules)
11. [Configuration](#11-configuration)
12. [Threading & safety](#12-threading--safety)
13. [Lobby control (optional scope)](#13-lobby-control-optional-scope)
14. [Implementation phases](#14-implementation-phases)
15. [Testing](#15-testing)
16. [Open questions / risks](#16-open-questions--risks)

---

## 1. Current state

The 5 commits on branch `mcp` provide:

| Piece | File | Notes |
|---|---|---|
| cpp-mcp submodule (HTTP/SSE MCP server lib) | `client/lib/cpp-mcp` | `mcp::server`, `register_tool`, `register_resource` |
| Server lifecycle, settings (`mcp.enabled/host/port`) | `client/McpServer.{h,cpp}`, `config/schemas/settings.json` | Started from `GameEngine` (`ENGINE->mcpServer()`) |
| 14 read tools | `client/mcp/InfoTools.cpp` | game state, heroes, towns, player info, tiles, map content, creatures/artifacts/spells/skills lists, config, hero/town details, battle state |
| 6 action tools | `client/mcp/ActionTools.cpp` | execute_command, move_hero, end_turn, recruit_creatures, build_building, dismiss_hero |
| JSON helpers | `client/mcp/Helpers.{h,cpp}` | `heroToJson`, `townToJson`, `battleUnitToJson`, … |

### Known gaps / debt in the current code

- **Actions are fire-and-forget**: they return `"queued for execution"`; the LLM never learns
  whether the action succeeded, what happened (battle started? dialog opened? object visited?).
- **No event feed**: server→client packs (level-ups, combat results, info windows, new day…)
  are invisible to the LLM.
- **No dialog/query support**: any blocking dialog (level-up skill choice, "do you want to
  fight?", garrison window, teleport exit choice) stalls the game with no way for the LLM to
  see or answer it.
- **Only ~15 % of player actions covered** (6 of ~37 usable `CPackForServer` actions).
- **Duplicated boilerplate**: every handler repeats the `textContent` helper, the
  `shared_lock` + `GAME->interface()` null-checks, and hand-written JSON schema literals.
- `move_hero` hardcodes `EPathfindingLayer::LAND` (no sailing), ignores transit, gives no
  path feedback.

---

## 2. Target architecture

```
                 MCP HTTP/SSE (cpp-mcp worker threads)
                        │ tools/call
                        ▼
 ┌───────────────────────────────────────────────────────────┐
 │ client/mcp/                                               │
 │                                                           │
 │  ToolContext        — resolves player / CCallback,        │
 │                       locks CGameState::mutex (shared),   │
 │                       formats results & errors            │
 │  Serializers        — toJson(T) overload set (section 8)  │
 │  EventJournal       — seq-numbered JSON events, ring      │
 │  QueryRegistry      — pending QueryID → query description │
 │  RequestTracker     — requestID → completion (cond var)   │
 │                                                           │
 │  tools/ …           — one file per domain (section 10)    │
 └──────────┬──────────────────────────────┬─────────────────┘
   actions: │ dispatchMainThread           │ observes packs
            ▼                              │ (one-line hook)
   CPlayerInterface / CCallback     CClient::handlePack()
            │ sendRequest(pack)            ▲
            ▼                              │ CPackForClient
        VCMI server  ──────────────────────┘
```

Key decisions:

1. **The MCP module is a "sibling" of the GUI, not a replacement.** It acts through the same
   `CCallback`/`CPlayerInterface` the human uses, so server-side validation, fog of war and
   permissions apply automatically — the LLM is exactly as powerful as a human client.
2. **One integration hook outside `client/mcp/`**: a single call at the end of
   `CClient::handlePack()` (and one in `CClient::handleGamePack` path if needed) forwarding
   every applied `CPackForClient` to the MCP module:
   ```cpp
   ENGINE->mcpServer().onPackApplied(pack);   // no-op when disabled
   ```
   Everything else (journal, query tracking, request completion) derives from this feed via an
   `ICPackVisitor` implemented **inside** `client/mcp/`. No changes to netpacks, server, or lib.
3. **Per-player scoping.** Every tool accepts an optional `player` argument (color string).
   Default: the currently active local player (`GAME->interface()`). In hotseat, all local
   players are controllable; fog of war of the chosen player applies to reads (enforced by
   using that player's `CCallback`). For "each client sees its own view" in multiplayer, run
   one VCMI client (with its own MCP port) per player — no extra code needed.
4. **Static entity data as MCP resources** (`vcmi://creatures`, `vcmi://artifacts`,
   `vcmi://spells`, `vcmi://skills`, `vcmi://config`) via `register_resource`, since they never
   change mid-game. Keep thin `list_*` tools as fallback for clients with poor resource support.

---

## 3. The "wait for result" mechanism

VCMI already contains everything needed — we only surface it:

- `CClient::sendRequest(pack, player, waitTillRealize)` assigns a fresh `requestID`, and the
  server answers every `CPackForServer` with **`PackageApplied{requestID, result}`**
  (`lib/networkPacks/PacksForClient.h:56`). `result == false` means the request was rejected.
- The existing `waitTillRealize` path blocks the *calling* thread (used by AI); we must NOT use
  it from the main thread. Instead the MCP action flow is:

```
MCP worker thread                        main thread                network thread
────────────────────                     ─────────────────────      ─────────────────────
acquire action lock, tracker.beginWait()
markerSeq = journal.currentSeq()
dispatchMainThread(action) ────────────► try { action:
   … tracker.waitResult(timeout) …          cb->something(…) → sendRequest
                                         } catch → tracker.reportLocalError(msg)
                     ◄─────────────────────────────────────────── PackageApplied
                                                                  → tracker.reportApplied(result)
collect journal[markerSeq..now]
return { status, error?, events[...], pendingQueries[...] }
```

- **`RequestTracker`** (as implemented): no per-requestID map — VCMI never hands the requestID
  back through `CCallback`, so the tracker instead serializes MCP actions (one in flight at a
  time via an action lock) and treats the *next* `PackageApplied` after `beginWait()` as the
  answer. `waitResult(timeout)` returns `{applied, errorMessage} | timeout`; `errorMessage` is
  set when the dispatched action threw a validation error before any request was sent.
- **Timeout behavior** (`mcp.requestTimeoutMs`, default 10 000): on timeout the tool returns
  `{"status": "pending", ...}` with whatever events/queries accumulated so far — the LLM
  continues via `get_events` / `get_pending_queries`. This matters because some actions
  legitimately do not complete until *other* things happen (e.g. `MoveHero` onto a monster
  starts a battle; the pack is realized, but the *interaction* continues via a `Query`).
- **Never wait while holding `CGameState::mutex`** (deadlock: pack application takes a unique
  lock).
- Tools that trigger a *response pack* (only `RequestStatistic` → `ResponseStatistic`) need no
  special handling in practice: the server sends `ResponseStatistic` while applying the request,
  i.e. before its `PackageApplied`, so the ordinary wait already captures the `statisticsReady`
  journal entry in the returned delta.
- Additionally a generic **`wait_for_event`** tool (long-poll): blocks the MCP worker until an
  event matching a filter (e.g. `yourTurn`, `battleUnitActive`, `queryOpened`, any) arrives or
  timeout expires. This is how the LLM idles while AI players move, without busy-polling.

---

## 4. Query / dialog handling

Server-driven dialogs arrive as `Query`-derived packs (`queryID` field) and **block the hero /
turn until answered** with `QueryReply{qid, reply}` (or dedicated packs like `ArrangeStacks`
followed by `QueryReply` for garrison dialogs).

**`QueryRegistry`** (fed by the pack visitor):

- On any `Query` pack with valid `queryID`: store `{queryID, type, player, JSON description}`.
  Descriptions use the serializers (section 8): dialog text (`MetaString` → translated string),
  `Component` list (each with type/id/value/description), selection/cancel flags, exit lists, etc.
- On observing an outgoing answer (or `PackageApplied` for our own `QueryReply`): remove entry
  and journal `queryAnswered`.
- Tools:
  - `get_pending_queries` → list of open queries with full context.
  - `answer_query { queryId, reply }` → `cb->sendQueryReply(reply, qid)`; `reply` semantics per
    dialog type documented in the tool description (0 = cancel/no, 1..n = selection index,
    exit index for teleport, object index for map-object select, skill index for level-up).
  - Garrison/exchange dialogs: while open, army/artifact tools work on the two objects; the
    dialog is *closed* via `answer_query` (reply ignored).

**GUI coexistence** (the one place where a small change outside `client/mcp/` may be needed,
Phase 5): when a human watches the same client, the dialog window is on screen; if the LLM
answers via `QueryReply`, the GUI window must be dismissed. Plan: on `queryAnswered` for a
query the GUI displayed, `dispatchMainThread` a `windows().popWindow(...)`-style cleanup via a
small, well-contained handler in `CPlayerInterface` (~20 lines) — or, simpler first iteration,
document that MCP-driven play should not be mixed with clicking dialogs manually.

---

## 5. Netpack analysis: client → server (actions)

Every struct in `lib/networkPacks/PacksForServer.h`, how it is triggered today, and how MCP
exposes it. "Existing accessor" = `CCallback` method (all send via `CClient::sendRequest`).
**Wait** column: ✔ = wait for `PackageApplied` + return journal delta (the default);
✔+Q = additionally likely opens a query the LLM must answer.

| # | Pack | Purpose | Existing accessor | MCP tool (proposed) | Parameters | Wait | Status |
|---|---|---|---|---|---|---|---|
| 1 | `GamePause` | pause single-player game | `cb->gamePause(bool)` | `set_game_pause` | `paused` | ✔ | new |
| 2 | `EndTurn` | end current player's turn | `cb->endTurn()` | `end_turn` | – | ✔ | **exists**, add wait+result |
| 3 | `DismissHero` | remove own hero | `cb->dismissHero(h)` | `dismiss_hero` | `heroId` | ✔ | **exists**, add wait+result |
| 4 | `MoveHero` | move hero along path | `CPlayerInterface::moveHero` (UI path) / `cb->moveHero(h, path/dest, transit, layer)` | `move_hero` | `heroId, x,y,z, transit?, layer?` (auto layer default) | ✔+Q (battles, visits, dialogs) | **exists**, fix layer, add transit + rich result (`TryMoveHero` outcomes, stop reason, remaining MP) |
| 5 | `CastleTeleportHero` | Castle Gate teleport | `cb->teleportHero(h, town)` | `castle_gate_teleport` | `heroId, townId` | ✔ | new |
| 6 | `ArrangeStacks` | swap / merge / split two slots | `cb->swapCreatures / mergeStacks / mergeOrSwapStacks / splitStack` | `swap_stacks`, `merge_stacks`, `split_stack` | `objectId1, slot1, objectId2, slot2 (, count)` | ✔ | new (also used inside garrison/exchange dialogs) |
| 7 | `BulkMoveArmy` | move whole army/slot to other garrison | `cb->bulkMoveArmy` | `move_army` | `srcObjectId, destObjectId, srcSlot?` | ✔ | new |
| 8 | `BulkSplitStack` | split one slot into N free slots | `cb->bulkSplitStack` | `split_stack_evenly` | `objectId, slot, count` | ✔ | new |
| 9 | `BulkMergeStacks` | merge all same-type stacks | `cb->bulkMergeStacks` | `merge_all_stacks` | `objectId, slot` | ✔ | new |
| 10 | `BulkSplitAndRebalanceStack` | spread stack over free slots evenly | `cb->bulkSplitAndRebalanceStack` | `rebalance_stacks` | `objectId, slot` | ✔ | new |
| 11 | `DisbandCreature` | dismiss a stack | `cb->dismissCreature` | `dismiss_creatures` | `objectId, slot` | ✔ | new |
| 12 | `BuildStructure` | build town building | `cb->buildBuilding` | `build_building` | `townId, buildingId` | ✔ | **exists**, add wait+result |
| 13 | `VisitTownBuilding` | trigger visitable building (tavern, market, shrine, …) | `cb->visitTownBuilding` | `visit_town_building` | `townId, buildingId` | ✔+Q (often opens window query) | new |
| 14 | `RazeStructure` | demolish building (special maps/cheats) | – (no CCallback wrapper; server-side & cheats only) | not exposed (use cheat via `send_chat_message` if needed) | – | – | n/a |
| 15 | `SpellResearch` | mage-guild spell research (VCMI extension) | `cb->spellResearch` | `research_spell` | `townId, spellId, accept` | ✔ | new |
| 16 | `RecruitCreatures` | hire creatures from town/dwelling | `cb->recruitCreatures` | `recruit_creatures` | `dwellingId, destinationId, creatureId, amount, level?` | ✔ | **exists**, add wait+result |
| 17 | `UpgradeCreature` | upgrade a stack | `cb->upgradeCreature` | `upgrade_creatures` | `objectId, slot, creatureId?` (default best) | ✔ | new |
| 18 | `GarrisonHeroSwap` | swap garrison/visiting hero | `cb->swapGarrisonHero` | `swap_garrison_hero` | `townId` | ✔ | new |
| 19 | `ExchangeArtifacts` | move one artifact between slots/heroes | `cb->swapArtifacts` | `move_artifact` | `srcObjectId, srcSlot, dstObjectId, dstSlot` | ✔ | new |
| 20 | `BulkExchangeArtifacts` | move/swap all artifacts | `cb->bulkMoveArtifacts` | `transfer_artifacts` | `srcHeroId, dstHeroId, swap?, equipped?, backpack?` | ✔ | new |
| 21 | `ManageBackpackArtifacts` | scroll/sort backpack | `cb->scrollBackpackArtifacts / sortBackpackArtifactsBy*` | `sort_backpack` | `heroId, command` | ✔ | new (low priority — cosmetic) |
| 22 | `ManageEquippedArtifacts` | artifact costume save/load | `cb->manageHeroCostume` | `manage_artifact_costume` | `heroId, costumeIdx, save` | ✔ | new (low priority) |
| 23 | `AssembleArtifacts` | (dis)assemble combined artifact | `cb->assembleArtifacts` | `assemble_artifact` | `heroId, slot, assemble, assembleToId?` | ✔ | new |
| 24 | `EraseArtifactByClient` | discard artifact (edge cases) | `cb->eraseArtifactByClient` | not exposed initially | – | – | n/a (destructive, rarely legal) |
| 25 | `BuyArtifact` | buy spellbook / war machines in town | `cb->buyArtifact` | `buy_artifact` | `heroId, artifactId` | ✔ | new |
| 26 | `TradeOnMarketplace` | all market trades (8 modes incl. sacrifice) | `cb->trade(marketId, mode, sell[], buy[], amounts[], hero)` | `trade` | `marketId, mode, sellIds[], buyIds[], amounts[], heroId?` | ✔ | new (+ `get_market_info` read tool for rates via `IMarket`) |
| 27 | `SetFormation` | tight/loose army formation | `cb->setFormation` | `set_formation` | `heroId, formation` | ✔ | new (low priority) |
| 28 | `SetTactics` | toggle tactics-phase usage | `cb->setTactics` | `set_tactics` | `heroId, enabled` | ✔ | new (low priority) |
| 29 | `SetTownName` | rename town | `cb->setTownName` | `rename_town` | `townId, name` | ✔ | new (low priority) |
| 30 | `HireHero` | recruit hero in tavern | `cb->recruitHero` | `hire_hero` | `townOrTavernId, heroTypeId, nextHeroTypeId?` | ✔ | new (+ tavern info via serializer: `SetAvailableHero` state) |
| 31 | `BuildBoat` | buy boat at shipyard | `cb->buildBoat` | `build_boat` | `shipyardObjectId` | ✔ | new |
| 32 | `QueryReply` | answer any server query/dialog | `cb->sendQueryReply / selectionMade` | `answer_query` | `queryId, reply?` | ✔ | new — **core** (section 4) |
| 33 | `MakeAction` | battle action | `battleCb->battleMakeUnitAction / battleMakeSpellAction / battleMakeTacticAction` | `battle_*` family | see section 7 | ✔ (until next `BattleSetActiveStack` / battle end) | new — **core** |
| 34 | `DigWithHero` | dig for Grail | `cb->dig` | `dig` | `heroId` | ✔ | new |
| 35 | `CastAdvSpell` | cast adventure-map spell | `cb->castSpell` | `cast_adventure_spell` | `heroId, spellId, x?,y?,z?` | ✔+Q (e.g. Town Portal select) | new |
| 36 | `RequestStatistic` | request game statistics dataset | `cb->requestStatistic()` | `get_statistics` | – | wait for `ResponseStatistic` pack | new |
| 37 | `SaveGame` | save the game | `cb->save(fname)` | `save_game` | `filename` | ✔ | new |
| 38 | `PlayerMessage` | chat message **and cheat codes** | `cb->sendMessage(text, obj?)` | `send_chat_message` | `text, objectId?` | ✔ | new (`mcp.allowCheats` gate documented; cheats like `vcmiistari` valuable for testing) |
| 39 | `AdvInterfaceReady` | internal client-ready handshake | automatic | not exposed | – | – | n/a |
| – | `SaveLocalState` (`SaveLocalState.h`) | persist UI-local state | `cb->saveLocalState` | not exposed (pure UI) | – | – | n/a |

Also client-side (no pack, but required for parity with a human player):

| Capability | Backing API | MCP tool |
|---|---|---|
| Path preview: reachability, cost, turns | `cb->getPathsInfo(hero)` / `PlayerLocalState::setPath`, `CGPathNode` | `get_hero_path { heroId, x,y,z }` → nodes, movement cost, turns, blocked reason |
| Available hero upgrades for a stack | `cb->fillUpgradeInfo` (`UpgradeInfo`) | part of `get_army` / `get_object_details` |
| Movement cost between adjacent tiles, guards | `cb->canMoveBetween`, `getGuardingCreaturePosition` | folded into `get_tiles` / `get_hero_path` |
| Console/debug commands | `ClientCommandManager` | `execute_command` (**exists**; keep, mark as debug) |

## 6. Netpack analysis: server → client (events)

All structs from `PacksForClient.h` / `PacksForClientBattle.h` and their MCP disposition.
Since the LLM can always re-read state via tools, most packs need only a **journal event**
(seq, day, type, small JSON payload) or nothing at all. Categories:

- **REQ** — consumed by `RequestTracker` (action completion).
- **QRY** — registered in `QueryRegistry` (must be answered) *and* journaled.
- **EVT** — journaled (payload via serializers).
- **state** — no event; effect visible through read tools (journaling would be noise).
- **UI** — pure client visuals; ignored.

### PacksForClient.h

| Pack | Meaning | Disposition | Journal payload / notes |
|---|---|---|---|
| `PackageApplied` | request acknowledged/result | **REQ** | completes `RequestTracker`; journaled only on `result=false` (`actionRejected`) |
| `PackageReceived` | request received | ignore | – |
| `SystemMessage` | server text message | **EVT** | text |
| `PlayerBlocked` | player blocked/unblocked (upcoming battle) | **EVT** | reason, start/stop |
| `PlayerCheated` | cheat flag set | **EVT** | player |
| `TurnTimeUpdate` | turn timer tick | state | poll via `get_game_state` (avoid journal spam) |
| `PlayerStartsTurn` (Query) | player's turn begins | **EVT** (`yourTurn` when local) | queryID auto-answered by existing client flow; MCP only journals |
| `DaysWithoutTown` | elimination countdown | **EVT** | daysLeft |
| `EntitiesChanged` | entity config change | state | – |
| `SetResources` | resource amounts set | **EVT** | per-resource delta for own player; state for others |
| `SetPrimarySkill` / `SetSecSkill` / `SetHeroExperience` / `GiveStackExperience` | hero/stack progression | **EVT** | heroId, what, value |
| `HeroVisitCastle` | hero enters/leaves town | **EVT** | heroId, townId, in/out |
| `ChangeSpells` / `SetResearchedSpells` | spellbook changes | **EVT** | heroId/townId, spells |
| `SetMana` / `SetMovePoints` | mana/MP set | state | visible via hero reads; journal only absolute==0 edge? → keep state-only |
| `FoWChange` | tiles revealed/hidden | **EVT** (aggregated) | count + bounding box only (tile list via `get_tiles`) |
| `SetAvailableHero` | tavern pool slot changed | state | read via `get_tavern_heroes` |
| `GiveBonus` / `RemoveBonus` | bonus granted/removed | **EVT** (own player/heroes only) | bonus description string |
| `ChangeObjPos` | object moved (boat …) | state | – |
| `PlayerEndsTurn` | a player finished turn | **EVT** | player |
| `PlayerEndsGame` | player won/lost | **EVT** (critical) | player, victory/defeat, reason string |
| `SetCommanderProperty` | commander change | state | – |
| `AddQuest` | quest added to log | **EVT** | quest via serializer |
| `ChangeFormation` / `ChangeTactics` / `ChangeTownName` | echoes of own settings | state | – |
| `RemoveObject` | map object removed | **EVT** | objectId, initiator (picked artifact, defeated hero/monster…) |
| `TryMoveHero` | hero move result (per step) | **EVT** | heroId, from→to, result enum (SUCCESS/BLOCKING_VISIT/EMBARK/TELEPORTATION/FAILED…) — primary feedback for `move_hero` |
| `NewStructures` / `RazeStructures` | building built/razed | **EVT** | townId, buildingIds |
| `SetAvailableCreatures` | dwelling stock changed | state | – |
| `SetHeroesInTown` | garrison/visiting set | state | – |
| `HeroRecruited` / `GiveHero` | new hero appears | **EVT** | heroId, townId |
| `OpenWindow` (Query) | open market/university/tavern/exchange/… window | **QRY** | window type + involved object ids; must be answered to release hero |
| `NewObject` | object created (boat, spawned monster) | **EVT** | object summary |
| `SetAvailableArtifacts` | artifact merchant stock | state | – |
| `ChangeStackCount` / `SetStackType` / `EraseStack` / `SwapStacks` / `InsertNewStack` / `RebalanceStacks` / `BulkRebalanceStacks` | garrison operations | state | armies readable; own-army net change journaled as one aggregated `armyChanged` **EVT** |
| `GrowUpArtifact` / `PutArtifact` / `NewArtifact` / `BulkEraseArtifacts` / `BulkMoveArtifacts` / `DischargeArtifact` / `AssembledArtifact` / `DisassembledArtifact` | artifact operations | state (own-hero net change → aggregated `artifactsChanged` **EVT**) | – |
| `HeroVisit` | hero visits object start/stop | **EVT** | heroId, objectId, start |
| `InfoWindow` | modal/info text ("You find 500 gold") | **EVT** (important!) | translated text + components |
| `NewTurn` | new day/week/month | **EVT** | day, week, month, weekType |
| `SetObjectProperty` | owner/other property changed (mine flagged) | **EVT** when owner changes, else state | objectId, property |
| `ChangeObjectVisitors` | visit bookkeeping | state | – |
| `ChangeArtifactsCostume` | costume saved | state | – |
| `HeroLevelUp` (Query) | choose skill on level-up | **QRY** | heroId, primary skill gained, skill options (serialized) |
| `CommanderLevelUp` (Query) | commander skill choice | **QRY** | options |
| `BlockingDialog` (Query) | yes/no/selection dialog | **QRY** | text, components, selection/cancel flags |
| `GarrisonDialog` (Query) | garrison exchange window | **QRY** | both object ids, removableUnits |
| `ExchangeDialog` (Query) | hero↔hero exchange screen | **QRY** | both hero ids |
| `TeleportDialog` (Query) | choose teleport exit | **QRY** | hero, channel, exits (id+pos), impassable |
| `MapObjectSelectDialog` (Query) | choose object (Town Portal …) | **QRY** | icon, title, description, object list |
| `AdvmapSpellCast` | adventure spell cast happened | **EVT** | casterId, spellId |
| `ShowWorldViewEx` | world-view overlay | UI | – |
| `PlayerMessageClient` | chat line | **EVT** | player, text |
| `CenterView` | camera hint | UI | – |
| `ResponseStatistic` | statistics payload | **REQ**-like | completes `get_statistics` |

### PacksForClientBattle.h

| Pack | Meaning | Disposition | Notes |
|---|---|---|---|
| `BattleStart` | battle begins | **EVT** + battle context | battleId, sides, location; `get_battle_state` becomes live |
| `BattleNextRound` | new round | **EVT** | round no. |
| `BattleSetActiveStack` | unit's turn to act | **EVT** (`battleUnitActive`) | unitId; the trigger for the LLM battle loop |
| `BattleCancelled` | battle aborted | **EVT** | – |
| `BattleResultAccepted` | results applied to heroes | state | – |
| `BattleResult` (Query) | battle finished, result window | **QRY** (auto-answerable) + **EVT** | winner, casualties both sides, exp; answering closes result window |
| `BattleLogMessage` | textual battle log line | **EVT** | translated lines — high value for LLM |
| `BattleStackMoved` | unit walked | **EVT** | unitId, hex path, distance |
| `BattleUnitsChanged` | units added/changed/removed | state | readable via `get_battle_state` |
| `BattleAttack` (+`BattleStackAttacked`) | attack with per-target damage/kills | **EVT** | attacker, targets[{unitId, damage, killed, dead}], shot/counter/lucky flags |
| `StartAction` / `EndAction` | action bracketing | ignore (journal uses concrete packs) | – |
| `BattleSpellCast` | spell resolved in battle | **EVT** | caster side, spellId, affected units, resisted |
| `StacksInjured` | non-attack damage (moat …) | **EVT** | per-unit damage |
| `BattleResultsApplied` | XP/artifact transfer done | **EVT** (`battleEnded` composite) | – |
| `BattleEnded` | cleanup | state | – |
| `BattleObstaclesChanged` | obstacles placed/removed | state (in battle state reads) | – |
| `CatapultAttack` | wall section hit | **EVT** | wall part, damage |
| `BattleSetStackProperty` | casts/counters/enchanter changes | state | – |
| `BattleTriggerEffect` | morale/regeneration/mana drain… | **EVT** | unitId, effect | 
| `BattleUpdateGateState` | drawbridge state | state | – |

**Journal design**: fixed-size ring (default 4096 entries, `mcp.journalSize`), monotonically
increasing `seq`, each entry `{seq, type, data}` (the draft's `day`/`battleRound` fields were
not implemented - the payload carries ids the LLM can resolve instead). `get_events {sinceSeq?,
types?[], limit?}` returns entries + `latestSeq`. Action tools embed the delta slice
automatically, so in the common case the LLM never calls `get_events` explicitly.

**Coverage audit (2026-07-18)**: `JournalVisitor` now implements every pack this table marks
EVT/QRY/REQ, including the batch added after the audit found them missing (`PlayerCheated`,
`SetResources`, `SetSecSkill`, `GiveStackExperience`, `ChangeSpells`, `SetResearchedSpells`,
`FoWChange` as an aggregated count+bounding-box event, `BattleStackMoved`). Two deliberate
simplifications remain: garrison/artifact operation packs are state-only with **no** aggregated
`armyChanged`/`artifactsChanged` event (the envelope's post-action re-read covers the use case;
true multi-pack aggregation isn't worth the complexity), and `TurnTimeUpdate`/`SetMana`/
`SetMovePoints` stay state-only as planned.

---

## 7. Battle control

The human flow: `BattleSetActiveStack` → user picks an action in `BattleInterface` →
`battleCb->battleMakeUnitAction(battleID, BattleAction)`. MCP mirrors it 1:1 with dedicated
tools (all build a `BattleAction` — `lib/battle/BattleAction.h` factory methods — and go
through `CBattleCallback`, main-thread dispatched):

| Tool | BattleAction | Parameters |
|---|---|---|
| `battle_move` | `makeMove` | `unitId, hex` |
| `battle_attack` | `makeMeleeAttack` | `unitId, targetUnitId, attackFromHex?, returnAfterAttack?` |
| `battle_shoot` | `makeShotAttack` | `unitId, targetUnitId` |
| `battle_wait` | `makeWait` | `unitId` |
| `battle_defend` | `makeDefend` | `unitId` |
| `battle_cast_spell` | hero cast (`HERO_SPELL`) / `makeCreatureSpellcast` | `spellId, targetHex/targetUnitId` |
| `battle_catapult` | `CATAPULT` aim | `unitId, hex` |
| `battle_heal` | `makeHeal` (first aid tent) | `unitId, targetUnitId` |
| `battle_retreat` / `battle_surrender` | `makeRetreat`/`makeSurrender` | – (+ `get_battle_state` exposes surrender cost) |
| `battle_end_tactics` | `makeEndOFTacticPhase` | – (tactic-phase moves use `battle_move` via `battleMakeTacticAction`) |

Read support in `get_battle_state` ✅ (2026-07-18, all from `CPlayerBattleCallback`): per living
unit **`reachableHexes`** (`battleGetAvailableHexes`) + `canShootNow` + current
speed/canMove/waited flags; **`turnOrder`** for the next two turns (`battleGetTurnOrder`);
**`obstacles`** with blocked hexes (perspective-filtered, so hidden spell obstacles stay
hidden); `canFlee`/`surrenderCost`/tactics info; and for the active unit an
**`activeUnitTargets`** entry per living enemy: `canShoot`, `meleeAttackFromHexes` (adjacency ∩
reachability - directly usable as `battle_attack`'s `attackFromHex`; approximate for double-wide
attackers, server validates), damage/kills estimate and expected retaliation
(`battleEstimateDamage`). Not exposed: per-spell legality (`battleCanCastThisSpell`) - the LLM
casts and gets a rejection instead.
Waiting ✅ (2026-07-18): after a battle action is acknowledged, `actionTool` additionally waits
(short grace window) for the follow-up `battleUnitActive`/`battleResult`/`battleEnded` journal
event, so the response envelope closes the "action → outcome → whose turn next" loop in one
round trip.
`makeSurrenderRetreatDecision` (auto-retreat prompt) surfaces as a **QRY** if it ever reaches a
human interface.

---

## 8. JSON serialization: class table

> **Note (2026-07-18):** the ✅/🔶/❌ statuses below are frozen at the pre-implementation draft
> and no longer reflect the code — ground truth is `Serializers.{h,cpp}` plus the inline
> serialization in the tool files. Since the draft: `TerrainTile`, `CBuilding`, `HeroType`,
> `Component`, dwelling creatures, market info, quest entries, path nodes, and tavern heroes
> were implemented; the `Ctx` parameter idea was dropped (free functions are enough, visibility
> is enforced by fetching objects through the player-scoped callback before serializing).
> Rows still genuinely open: richer battle-unit stats/effects, `CCommanderInstance`,
> `CObstacleInstance`, `Bonus` descriptions, `TurnTimerInfo`, and the `get_battle_state`
> extensions listed in §7.

One overload set `JsonNode toJson(const T *, const Ctx &)` in `client/mcp/Serializers.{h,cpp}`
(split into `SerializersEntities`, `SerializersMap`, `SerializersBattle` if size demands).
All existing `Helpers.cpp` converters move here and get completed. Rule: **serialize only what
the owning player may see** — always go through the player-scoped callback where visibility
matters (`Ctx` carries the `CCallback`).

Legend: ✅ done (current branch), 🔶 partial (extend), ❌ missing.

### Static entities (→ MCP resources; game-start constant)

| Class / source | Serializer | Status | Fields to add |
|---|---|---|---|
| `Creature` (`CCreatureHandler`) | `toJson(Creature)` | 🔶 inline in `list_creatures` | faction, upgrade targets, cost, abilities/bonus descriptions |
| `Artifact` / `CArtifact` | `artifactToJson` | ✅ | equip slots, combined-parts list, bonus text |
| `spells::Spell` / `CSpell` | `spellToJson` | ✅ | target type, adventure/battle flags text, descriptions per level |
| `Skill` / `CSkill` | `skillToJson` | ✅ | – |
| `HeroType`/`CHeroClass` | ❌ | ❌ | starting army/skills/spell, specialty text — for `list_hero_types` (tavern decisions) |
| Faction/Town type + `BuildingID`→building info | ❌ | ❌ | building name, cost, requirements tree, produced dwelling — **needed for `build_building` to be usable** |
| Game config (merged) | `getFullGameConfig` | ✅ | keep as resource |

### Adventure map / game state

| Class | Serializer | Status | Notes / fields |
|---|---|---|---|
| `CGameState` summary (+ `Calendar`, `StartInfo`) | `gameStateToJson` | 🔶 (`get_game_state`) | add turn order, active player, own player status, timers (`TurnTimerInfo` ❌) |
| `PlayerState` | 🔶 inline | 🔶 | resources ✅; add team, isHuman, daysWithoutTown, quests, owned object counts |
| `TeamState` (fog of war) | tile presence only | 🔶 | expose via `get_tiles` region query instead of full dump |
| `TerrainTile` | ❌ | ❌ | terrain/road/river ids+names, isWater, blocked/visitable, top object id, movement-relevant flags — `get_tiles {x1,y1,x2,y2,z}` region tool (replaces `get_visible_tiles` full-map dump) |
| `CGObjectInstance` (generic) | 🔶 inline in `get_map_content` | 🔶 | extract `objectToJson`: id, typeName/subtype, pos, owner, blockedTiles, visitable pos; per-type extension via small dispatch (below) |
| `CGHeroInstance` | `heroToJson`/`heroDetailsToJson` | 🔶 | add: position ✅(missing!), owner, specialty, commander, war machines, spell points regen, path summary, whether in town, boat |
| `CGTownInstance` | `townToJson`/`townDetailsToJson` | 🔶 | add: owner, position, income, **buildable list with costs & missing requirements** (`getBuildingState`), garrison army, events?, spell research state |
| `CArmedInstance` army (`CCreatureSet`, `CStackInstance`) | inline in `heroToJson` | 🔶 | extract `armyToJson`: per-slot creature id/name/count, experience, stack artifact, upgrade options (`UpgradeInfo` ❌) |
| `CCommanderInstance` | ❌ | ❌ | level, alive, skills, artifacts |
| `CArtifactInstance` + `ArtSlotInfo` | `artifactInstanceToJson` | ✅ | add charges (`DischargeArtifact` support), locked flag |
| `CGDwelling` | ❌ | ❌ | available creatures per level, owner — for external dwellings |
| `CGCreature` (wandering monster) | ❌ | ❌ | creature type, approximate count bracket (as player sees), aggression, upgrade/join hints not visible → omit |
| `CGResource` / `CGMine` / `FlaggableMapObject` | ❌ | ❌ | resource type, amount (if visible), owner, daily income |
| `IMarket` (`CGMarket`, town markets) | ❌ | ❌ | supported modes, efficiency, current exchange rates (`getOffer`) — `get_market_info` |
| `CQuest` / quest log | ❌ | ❌ | mission text, progress, target — `get_quests` |
| `CRewardableObject` | ❌ | ❌ | only player-visible description of choices (feeds `BlockingDialog` answers) |
| `CGPathNode` / path (`CPathsInfo`) | ❌ | ❌ | per-node coord, turns, moveRemains, accessibility, danger — `get_hero_path` |
| Tavern pool (`SetAvailableHero` state) | ❌ | ❌ | hero type, name, class, army preview — `get_tavern_heroes` |
| `Component` | ❌ | ❌ | type name, entity id+name, value, tooltip — **required for dialogs** |
| `MetaString` | ❌ | ❌ | `toString()` translated — used everywhere in dialogs/log |
| `Bonus` (selected) | ❌ | ❌ | human-readable description for hero details & GiveBonus events |
| `EVictoryLossCheckResult` | ❌ | ❌ | victory/defeat + message — `PlayerEndsGame` event |
| `StatisticDataSet` | ❌ | ❌ | for `get_statistics` (`ResponseStatistic`) |

### Battle

| Class | Serializer | Status | Notes |
|---|---|---|---|
| `battle::Unit` / `CStack` | `battleUnitToJson` | 🔶 | add: current stats (att/def/speed after bonuses), retaliations left, shots left, casts left, active effects (bonus list), morale/luck, occupied hexes |
| Battle overview (`CPlayerBattleCallback`) | inline in `get_battle_state` | 🔶 | add: tactics phase & distance, obstacles, per-unit reachability + damage estimates, turn queue (`battleGetTurnOrder`), surrender cost, spellcast legality |
| `CObstacleInstance` | ❌ | ❌ | type, hexes, trigger/absolute |
| `BattleHex` | int only | 🔶 | expose as `{hex, x, y}` for LLM geometry |
| `BattleAction` (echo in events) | `toString` exists | 🔶 | JSON form for journal |
| `BattleResult` | ❌ | ❌ | winner, casualties per side, exp gained |

### Netpack payload serialization (journal)

Small `toJson` visitors per **EVT** pack (section 6) live next to `EventJournal`
(`JournalVisitor.cpp`); they reuse the class serializers above — no duplicated field mapping.

---

## 9. Tool catalog (as implemented)

84 tools, all implemented (audited against the registered `tool_builder` calls 2026-07-18),
grouped by file:

- **Meta/session** (`QueryTools.cpp`): `get_game_state`, `get_events`, `wait_for_event`,
  `get_pending_queries`, `answer_query`, `get_statistics`, `save_game`, `send_chat_message`,
  `set_game_pause`, `execute_command` (debug), `end_turn`
- **Reading map & players** (`InfoTools.cpp` + `AdventureInfoTools.cpp`): `get_player_info`,
  `get_map_content`, `get_tiles` (region, replaces the old full-map `get_visible_tiles`),
  `get_object_details` (any object, type-dispatched), `get_hero_details`, `get_town_details`,
  `get_hero_path`, `get_quests`, `get_market_info`, `get_tavern_heroes`, `get_battle_state`
- **Static data** (`InfoTools.cpp` + `AdventureInfoTools.cpp`; plain tools, not MCP resources -
  see phase 6 leftovers): `get_config`, `list_creatures`, `list_artifacts`, `list_spells`,
  `list_skills`, `list_buildings`, `list_hero_types`
- **Hero actions** (`ActionTools.cpp`): `move_hero`, `cast_adventure_spell`, `dig`,
  `dismiss_hero`, `castle_gate_teleport`, `set_formation`, `set_tactics`
- **Army** (`ArmyTools.cpp`): `swap_stacks`, `merge_stacks`, `split_stack`, `split_stack_evenly`,
  `merge_all_stacks`, `rebalance_stacks`, `move_army`, `dismiss_creatures`, `upgrade_creatures`
- **Artifacts** (`ArtifactTools.cpp`): `move_artifact`, `transfer_artifacts`,
  `assemble_artifact`, `buy_artifact`, `sort_backpack`, `manage_artifact_costume`
- **Town** (`TownTools.cpp`): `build_building`, `visit_town_building`, `recruit_creatures`,
  `hire_hero`, `swap_garrison_hero`, `research_spell`, `rename_town`, `build_boat`, `trade`
- **Battle** (`BattleTools.cpp`): `battle_move`, `battle_attack`, `battle_shoot`, `battle_wait`,
  `battle_defend`, `battle_cast_spell` (hero-cast only), `battle_catapult`, `battle_heal`,
  `battle_retreat`, `battle_surrender`, `battle_end_tactics`
- **Lobby** (`LobbyTools.cpp`, fire-and-forget - see phase 6 note): `lobby_get_state`,
  `lobby_list_maps`, `lobby_select_map`, `lobby_claim_player`, `lobby_set_player_option`,
  `lobby_set_difficulty`, `lobby_start_game`, `load_game`, `restart_game`, `return_to_menu`

Conventions (as implemented - two deliberate deviations from the original draft):
- Instance IDs are `ObjectInstanceID.getNum()`; coordinates `{x,y,z}`; player colors as name
  strings; most other enums as numeric ids (deviation: the draft wanted name strings
  everywhere - numeric ids match what the read tools return, so round-tripping is consistent).
- No per-tool `player` parameter (deviation): every tool acts as the currently active local
  player (`GAME->interface()`). In hotseat the active interface switches with the turn, so this
  is almost always what's wanted; acting for a non-active player would be rejected server-side
  anyway. Revisit only if true concurrent hotseat control becomes a requirement.
- Action result envelope:
  `{status: ok|rejected|pending, error?: "...", events: [...], pendingQueries: [...]}`.
- Errors → MCP tool errors with actionable message (unknown id, not your object, no game…).

## 10. File layout & code-quality rules

```
client/McpServer.{h,cpp}     (lifecycle: owns journal/tracker/registry/visitor, registration, onPackApplied hook)
client/mcp/
  IMPLEMENTATION_PLAN.md      (this file)
  ToolContext.{h,cpp}         (player resolution, locking, action envelope + wait)          [done]
  Serializers.{h,cpp}         (section 8; absorbed Helpers.{h,cpp})                          [done]
  EventJournal.{h,cpp}        (ring buffer + seq + wait support)                             [done]
  JournalVisitor.{h,cpp}      (ICPackVisitor → journal/QRY/REQ dispatch)                      [done]
  QueryRegistry.{h,cpp}                                                                      [done]
  RequestTracker.{h,cpp}                                                                      [done]
  InfoTools.cpp               (read tools; split into tools/ by domain as it grows)          [done, phase 1 subset]
  ActionTools.cpp             (existing 6 actions, retrofitted)                              [done]
  QueryTools.cpp              (get_events/wait_for_event/get_pending_queries/answer_query)    [done]
  tools/AdventureTools.cpp    (phase 3: remaining hero/army/artifact/town/trade actions)
  tools/BattleTools.cpp       (phase 4)
```

`mcp::tool_builder` (already vendored in cpp-mcp, `mcp_tool.h`) is used for all tool schemas
instead of a bespoke `ToolSchema` layer - no need to reinvent it.

Dedup rules (fixing current debt):
- `textContent()` defined **once** (ToolContext), not per file.
- Read tools written as `ctx.readTool([](ToolContext & c){ return toJson(...); })` — the
  wrapper does lock + "no active game" + error mapping.
- Action tools written as `ctx.actionTool(params, [](ToolContext & c){ return c.cb().xyz(...); })`
  — the wrapper does main-thread dispatch, request tracking, wait, envelope.
- Object lookup helpers `ctx.getHero(id)` / `ctx.getTown(id)` / `ctx.getMarket(id)` throw typed
  errors — replaces the repeated `getObj` + `dynamic_cast` + silent-return blocks.
- JSON schemas built with `ToolSchema` helpers instead of nested `mcp::json` literals.
- All ID/enum ↔ string conversion via existing lib `encode/decode` (`GameResID::encode` style),
  never hand-rolled maps.

Changes **outside** `client/mcp/` (kept minimal, the full list):
1. `client/Client.cpp` — one hook line in `handlePack` (`ENGINE->mcpServer().onPackApplied(pack)`).
2. `client/CMakeLists.txt` — new source files under `client/mcp/`.
3. `client/GameEngine.{h,cpp}` — already integrated, untouched further.
4. `client/McpServer.{h,cpp}` — stayed in `client/` (not moved); grew to own the journal/tracker/registry/visitor and expose `onPackApplied`.
5. `config/schemas/settings.json` — new optional keys under `mcp` (section 11).
6. (Phase 5, optional) `client/CPlayerInterface.cpp` — dismissal of GUI dialog when its query
   was answered via MCP.

## 11. Configuration

`settings["mcp"]` (existing: `enabled`, `host`, `port`) — additions, all with schema defaults:

| Key | Default | Purpose |
|---|---|---|
| `requestTimeoutMs` | 10000 | wait budget for action realization |
| `eventWaitTimeoutMs` | 60000 | max long-poll duration of `wait_for_event` |
| `journalSize` | 4096 | event ring size |

Two keys from the original draft were dropped rather than implemented: `allowCheats` (it would
have been a documentation-level guard only - the server accepts cheat messages regardless, so a
client-side toggle adds a false sense of control; `send_chat_message`'s description discloses
the cheat capability instead) and `allowedPlayers` (meaningless while tools always act as the
active local player - see the §9 conventions note on the dropped per-tool `player` parameter).

## 12. Threading & safety

- cpp-mcp handles tools on its own thread pool (`threadpool_size 2`, sessions ≤ 4).
- **Reads**: `std::shared_lock lock(CGameState::mutex)` for the whole serialization (already
  practiced); never call into GUI from MCP threads.
- **Actions**: never touch `CPlayerInterface`/`PlayerLocalState`/GUI except via
  `ENGINE->dispatchMainThread`; get `requestID` back through `std::promise`. Never hold the
  gamestate lock across `dispatchMainThread` or any wait.
- **Journal/registry/tracker**: internally mutex-guarded; `onPackApplied` runs on the network
  thread and must stay cheap (serialize payload, push, notify).
- Server is authoritative: even a buggy/racing MCP request only produces
  `PackageApplied{result=false}`, surfaced as `status: rejected`.
- MCP binds to `localhost` by default; no auth in cpp-mcp — document that exposing the port
  beyond localhost is the user's responsibility.

## 13. Lobby control (optional scope)

"Complete control as a client" can include pre-game flow. `CServerHandler` (`IServerAPI`)
already wraps all `PacksForLobby`; a compact tool set would be:
`lobby_get_state`, `lobby_list_maps` (via map browsing code), `lobby_select_map`
(`setMapInfo`), `lobby_set_player_option` (`setPlayerOption`/`setPlayer`/`setDifficulty`…),
`lobby_start_game` (`sendStartGame`), `load_game`, `restart_game` (`sendRestartGame`),
`return_to_menu` (`endGameplay`). These run in the client UI layer (main-thread dispatch, no
gamestate lock). Deferred to Phase 6 — in-game control first; games can meanwhile be started
manually or via `--donotstartserver`/CLI options.

## 14. Implementation phases

| Phase | Content | Outcome |
|---|---|---|
| **0. Refactor** ✅ | Introduce `ToolContext`, `Serializers` (absorb Helpers); port the 20 existing tools onto `mcp::tool_builder` + `ToolContext::readTool`; delete duplicated boilerplate | identical behavior, clean base |
| **1. Feedback core** ✅ | `handlePack` hook, `EventJournal` + `JournalVisitor`, `RequestTracker`, `QueryRegistry`; tools `get_events`, `wait_for_event`, `get_pending_queries`, `answer_query`; action envelope with wait; retrofit existing 6 actions | LLM sees consequences & can answer dialogs — action tools now return `{status, events, pendingQueries}` instead of "queued for execution" |
| **2. Read completeness** ✅ | `AdventureInfoTools.cpp` + `Serializers` additions for tiles/buildings/hero types; tools `get_tiles`, `get_object_details`, `get_hero_path`, `get_market_info`, `get_tavern_heroes`, `get_quests`, `list_buildings`, `list_hero_types` | LLM can plan like a human player. Static data still exposed as regular tools rather than MCP resources - not yet done |
| **3. Action completeness (adventure)** ✅ | `ArmyTools.cpp` (swap/merge/split/rebalance/move stacks, dismiss, upgrade), `ArtifactTools.cpp` (move/transfer/assemble/buy/sort/costume), `TownTools.cpp` (visit building/hire hero/swap garrison/research spell/rename/build boat/trade), hero misc actions added to `ActionTools.cpp` (cast spell/dig/castle gate/formation/tactics) | full adventure-map control, ~30 new action tools |
| **4. Battle** ✅ | `BattleTools.cpp`: battle_move/attack/shoot/wait/defend/heal/catapult/cast_spell(hero-only)/retreat/surrender/end_tactics, tactics-phase dispatch via `battleMakeTacticAction` | battle control for all common actions; creature-ability casts (`makeCreatureSpellcast`/`makeWalkAndCast`) and richer `get_battle_state` (reachability/damage estimates/turn queue) still open |
| **5. Polish** ✅⚠️ | `get_statistics` (reuses `StatisticDataSet::serializeJson` via a plain `JsonSerializer`, delivered inline in the action envelope's `events`); docs page `docs/developers/MCP_Server.md` with Claude Desktop/Code configs; tool description pass. GUI dialog dismissal was investigated and **deliberately not implemented** - see below | mixed human+LLM usable, with the GUI-sync caveat documented |
| **6. Optional** ✅ | `LobbyTools.cpp`: `lobby_get_state`, `lobby_list_maps`, `lobby_select_map`, `lobby_claim_player`, `lobby_set_player_option`, `lobby_set_difficulty`, `lobby_start_game`, `load_game`, `restart_game`, `return_to_menu`. Lobby packs don't go through the `PackageApplied`/`RequestTracker` pipeline (that's wired to `CPackForClient`, not `CPackForLobby`), so these fire-and-forget and rely on a follow-up `lobby_get_state` to confirm the result - documented as such. Campaign support and MCP resource templates (vs. plain tools) remain undone - low value relative to effort | game-session automation |

**GUI dialog dismissal - why it was skipped:** the plan originally scoped this as "~20 lines in
CPlayerInterface". On closer inspection (`CPlayerInterface::showGarrisonDialog` and friends), several
dialog windows re-send a server request on close as part of their own callback wiring (e.g.
`CGarrisonWindow`'s `quit` callback calls `cb->selectionMade(0, queryID)`). Programmatically closing
such a window after MCP has already answered its query would fire a second, stale request for an
already-closed query - a real correctness bug, not a cosmetic one - and the dialog/window stack has
its own serialization invariants (`dialogs` queue, `showingDialog->isBusy()`) that external closing
could desync. Given the fix is riskier than originally scoped and touches the core human-play GUI
path, it was left as a documented limitation (see `docs/developers/MCP_Server.md`) rather than risk
introducing bugs into `client/CPlayerInterface.cpp` to fix a cosmetic desync that doesn't affect MCP's
own correctness (the server-side answer is unaffected either way).

All phases (0-6) verified: full project builds and links with `ENABLE_MCP_SERVER=ON` (2026-07-14).
Phases 0-1 got a live smoke test (headless VCMI under Xvfb, confirmed the MCP HTTP server starts and
answers real JSON-RPC requests) before an unrelated pre-existing engine crash during AI setup cut the
session short. Phases 2-6 are build/link-verified only - a full live playthrough exercising the new
action/battle/lobby tools has not been done and would be the natural next verification step.

Each phase compiles green with `ENABLE_MCP_SERVER` on and off, and ends with the manual test
script (section 15) passing.

## 15. Testing

- **Build matrix**: `ENABLE_MCP_SERVER=ON/OFF` (guards already in place; keep all MCP code
  behind the flag and out of other targets).
- **Scripted functional test** (`test/mcp/` or `CI/`): small Python script using the official
  `mcp` pip client: start VCMI with a known test map (`cheats on`), connect, run a fixed
  sequence — read state → move hero → expect `TryMoveHero` events → build → recruit → trade →
  answer a blocking dialog → fight a scripted battle → end turn → assert day advanced. Run
  manually at first; CI needs game data (H3 files), so gate behind an env var like the
  existing ERM/functional tests.
- **Unit-testable pieces** (no game needed): `EventJournal` seq/ring/wait semantics,
  `RequestTracker` timeout logic, `ToolSchema` output, enum parsing — plain gtest in `test/`
  only if the project wants them in the main test target; otherwise keep logic trivially thin.
- **Manual checklist** per phase: hotseat two-player control, LLM vs. VCMI AI full game,
  battle round-trip latency, journal overflow behavior, MCP disabled = zero overhead.

## 16. Open questions / risks

1. **GUI/dialog coexistence** (section 4) — first iterations assume the human doesn't click
   dialogs the LLM is answering. Proper window dismissal needs the small Phase-5 hook.
2. **`PlayerStartsTurn` query auto-answer** — verify the existing client answers it without an
   active adventure interface interaction; if the GUI ever blocks turn start behind input, MCP
   must answer that query itself.
3. **Hotseat active-player switching** — acting for a non-active local player is illegal
   server-side; tools must check "is it this player's turn" and return a clear error instead
   of a rejected pack where possible.
4. **Event volume** — big AI turns can flood the journal (FoW aggregation mitigates); tune
   which packs are journal-worthy with real traces.
5. **cpp-mcp maturity** — session handling/streaming quirks; pin submodule. cpp-mcp *does*
   wrap each tool handler's synchronous return in try/catch and turns an escaping exception into
   a JSON-RPC error — but that only covers the calling (MCP worker) thread. **Fixed 2026-07-14,
   found via a real crash report**: action tools dispatch their actual work to the main GUI
   thread via `ENGINE->dispatchMainThread`, invoked from `InputHandler::handleUserEvent`, which
   has *no* exception handling of its own — a validation `throw` inside that dispatched functor
   (e.g. `requireHero` failing, or `buildBuilding` returning false) called `std::terminate()` and
   killed the whole client process, not just the one tool call. Every subsequent tool call then
   appeared to "time out" (the server process was dead). Fixed by making `ToolContext::actionTool`
   catch inside the dispatched lambda and report the failure through `RequestTracker` as
   `status:"rejected"` + a new `error` field, and by adding `dispatchMainThreadSafe` (catch-and-log)
   for the fire-and-forget dispatches (`execute_command`, `LobbyTools`) that have no envelope to
   report through. **Rule going forward: never call `ENGINE->dispatchMainThread` directly from
   MCP code — always go through `actionTool` or `dispatchMainThreadSafe`.** Also split static-data
   read tools (`list_creatures`, `list_artifacts`, `list_spells`, `list_skills`, `get_config`,
   `list_buildings`, `list_hero_types`) off the `CGameState::mutex` lock via a new
   `staticReadTool` — they only touch `LIBRARY`, so needlessly taking the lock could stall them
   for the entire duration of a long AI turn's pack processing (`shared_mutex` blocks new readers
   behind a pending writer), which likely explains the reported intermittent timeouts on
   `list_creatures` even before a crash occurred.
6. **Simultaneous turns (simturns)** — request rejection is more common; envelope already
   copes (`status: rejected`), but battle+simturns interplay needs testing.
7. **Save-compat & mods** — none affected (no lib/serialization changes) — a design invariant
   of this plan; keep it that way in review.
