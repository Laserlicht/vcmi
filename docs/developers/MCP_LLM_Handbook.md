# VCMI MCP — LLM Handbook

This is the operating guide for an LLM (or any autonomous MCP client) driving VCMI through the
MCP server. It assumes the server is already running and connected — for setup see
[`MCP_Server.md`](MCP_Server.md). Read that page for build/config; read this one to actually
play well.

The golden rule: **you are a normal player client, not the server.** Everything you do goes
through the same validated path a human's clicks do. You cannot cheat the rules, you only see
what your player sees (fog of war applies), and the server is the final authority on whether any
action is legal.

---

## 1. Session bootstrap

```
set_llm_control { "enabled": true }          # take over dialogs (see §5) — do this first
lobby_get_state                              # already in a game? or at the main menu?
```

If `lobby_get_state` shows `state: 0` (no connection), start a game:

```
lobby_new_game
lobby_list_maps                              # pick a fileUri
lobby_select_map { "fileUri": "Maps/Arrogance" }
lobby_claim_player { "player": "red" }       # optional; which color you play
lobby_set_difficulty { "difficulty": 1 }     # optional
lobby_start_game
```

Then wait for play to begin:

```
wait_for_event { "types": ["playerStartsTurn"] }
```

Static reference data never changes during a game — fetch each **once** and cache it:
`get_config`, `list_creatures`, `list_artifacts`, `list_spells`, `list_skills`,
`list_buildings`, `list_hero_types`. Do not re-request these every turn.

---

## 2. The action envelope — how to read tool results

Every **action** tool (anything that changes game state: `move_hero`, `build_building`,
`battle_attack`, …) returns the same envelope, not a bare success flag:

```json
{
  "status": "ok",
  "events": [ { "seq": 42, "type": "heroMoved", "data": { … } } ],
  "pendingQueries": []
}
```

- `status`:
  - `"ok"` — the server applied the request.
  - `"rejected"` — refused. Read the `error` field (present for client-side validation failures:
    unknown id, not your object, no path, …). A rejection is **not** a crash — adjust and retry.
  - `"pending"` — no acknowledgement within the timeout. The action may still be resolving
    (e.g. a long movement, or it triggered a dialog). Continue via `get_events` /
    `get_pending_queries`; do not blindly resend.
- `events`: everything that happened **as a consequence** of your action, since you issued it —
  including things you didn't directly cause (an AI reacting, a battle starting). This is the
  primary feedback channel. Always read it. Do not re-query full state after every action when
  the events already tell you what changed.
- `pendingQueries`: dialogs now waiting on you. **If this is non-empty, handle it before doing
  anything else** (see §5) — the game is blocked until you do.

Read tools (`get_*`, `list_*`) just return data directly, no envelope.

---

## 3. The turn loop

A robust single-player turn looks like:

```
1. get_game_state                      # whose turn, day, resources overview
2. get_heroes / get_towns              # your assets
3. for each hero you want to move:
     get_hero_path { heroId, x, y, z } # is it reachable? how many turns? blocked by what?
     move_hero { heroId, x, y, z }
       → inspect events: heroMoved (result/attackedFrom), objectRemoved, infoWindow, battleStart…
       → if pendingQueries non-empty → answer them (§5)
4. in towns: build_building, recruit_creatures, hire_hero, …
5. end_turn
6. wait_for_event { "types": ["playerStartsTurn", "battleStart", "playerEndsGame"] }
```

Key reads:
- `get_map_content` — all visible heroes/towns/objects (ids + positions).
- `get_tiles { x1, y1, x2, y2, z }` — terrain of a **region** (never dump the whole map).
- `get_object_details { objectId }` — full info on any object; auto-dispatches to hero/town
  detail, or a generic description with dwelling/market data.
- `get_hero_path` — reachability and per-node turns/cost **before** committing a `move_hero`.

Do not busy-poll. When it's not your turn (AI thinking, or a multiplayer opponent), block on
`wait_for_event` instead of calling `get_game_state` in a loop.

---

## 4. Events reference

Events arrive in the journal (monotonic `seq`) and inside action envelopes. Poll with
`get_events { sinceSeq?, types?, limit? }` or long-poll with
`wait_for_event { types?, sinceSeq?, timeoutMs? }`. Filter by `types` to avoid noise.

Turn / game flow: `playerStartsTurn`, `playerEndsTurn`, `newTurn` (new day/week),
`playerEndsGame` (victory/defeat — check `victory`), `daysWithoutTown`, `playerBlocked`,
`playerCheated`, `systemMessage`, `chatMessage`.

Hero / adventure: `heroMoved` (has `result`: 1=success, 2=teleport, 3=blocking visit, 4=embark,
5=disembark; and `attackedFrom` if it walked into a guard), `heroVisit`, `heroVisitCastle`,
`heroRecruited`, `heroGiven`, `heroExperienceChanged`, `heroPrimarySkillChanged`,
`heroSecondarySkillChanged`, `heroSpellsChanged`, `objectCreated`, `objectRemoved`,
`objectPropertyChanged` (e.g. a mine changing owner), `resourcesChanged`, `bonusGiven`,
`bonusRemoved`, `questAdded`, `adventureSpellCast`, `fogOfWarChanged` (aggregated count +
bounding box — use `get_tiles` for detail), `infoWindow` (a "you found…" message — **read the
text**, it often carries the outcome of what you just did).

Town: `buildingBuilt`, `buildingRazed`, `spellResearch`.

Battle: `battleStart`, `battleNextRound`, `battleUnitActive` (whose turn — the trigger for your
battle loop), `battleUnitMoved`, `battleAttack` (targets with damage/killed), `battleSpellCast`,
`stacksInjured`, `catapultAttack`, `battleTriggerEffect`, `battleLog` (human-readable lines),
`battleResult`, `battleEnded`, `battleCancelled`.

Dialogs: `queryOpened` (a blocking dialog — see §5), `queryAnswered`, `windowOpened` (a
non-blocking window, informational only), `actionRejected`, `statisticsReady` (payload of
`get_statistics`).

---

## 5. Dialogs / queries — the part that blocks the game

Server-driven decisions (level-up skill choice, "do you want to attack?", teleport exit,
garrison/market/tavern windows, …) arrive as **queries**. A query **halts the game** until you
answer it. Ignoring one hangs your turn.

**Always call `set_llm_control { "enabled": true }` once at session start.** With it on, the game
client suppresses the GUI windows for these dialogs so only you handle them — otherwise a human
watching the client and you could both try to answer, which desyncs. (The one exception is the
end-of-battle result window during a battle a human is watching; battles are otherwise fine.)

Flow whenever `pendingQueries` is non-empty (or a `queryOpened` event fires):

```
get_pending_queries                 # each entry: { queryId, kind, … context … }
answer_query { queryId, reply }
```

`reply` semantics by `kind`:

| kind | reply meaning |
|---|---|
| `blockingDialog` | `1..n` = pick that component/option and confirm; `0` or omit = cancel (only if `allowCancel`). A plain yes/no: `1` = yes, `0`/omit = no. |
| `heroLevelUp` | index into the query's `skillOptions` array (the offered secondary skills). |
| `commanderLevelUp` | index into `skillOptions`. |
| `garrisonDialog` | omit — answering just closes the exchange window; move troops with the army tools (`swap_stacks`, `move_army`, …) **while it's open**, then `answer_query` to close. |
| `exchangeDialog` | omit to close; use army/artifact tools meanwhile. |
| `teleportDialog` | index into the query's `exits` array (which exit to take); omit to stay. |
| `mapObjectSelectDialog` | index into the query's `objects` array (e.g. Town Portal target). |
| `openWindow` | omit — it opened a market/tavern/university/recruitment window; do the actual thing with `trade` / `hire_hero` / `recruit_creatures`, then `answer_query` to close. |
| `battleResult` | omit — acknowledges the result and lets the game continue. |

The `windowOpened` event (shipyard, thieves' guild, hill fort, puzzle map) is **not** a query —
it carries no `queryId` and needs no answer; act via the matching tool (`build_boat`, …) if you
want to.

---

## 6. Battle loop

When `battleStart` fires (or `get_game_state`/an envelope shows a battle), switch to the battle
loop, driven by `battleUnitActive`:

```
loop:
  get_battle_state
    → find the active unit; read its activeUnitTargets and per-unit reachableHexes
  decide, then act (all take unitId of the active unit):
    battle_move   { unitId, hex }                       # hex from that unit's reachableHexes
    battle_attack { unitId, targetUnitId, attackFromHex }   # attackFromHex from
                                                             # activeUnitTargets[t].meleeAttackFromHexes
    battle_shoot  { unitId, targetUnitId }              # only if that target's canShoot is true
    battle_wait / battle_defend / battle_cast_spell / battle_catapult / battle_heal
  the action's envelope already waits for the next battleUnitActive / battleResult,
    so read events to learn who acts next
  on battleResult query → answer_query to finish
```

`get_battle_state` does the tactical arithmetic for you:
- each living unit has `reachableHexes` (where it can go this turn) and `canShootNow`;
- `turnOrder` is the initiative queue for the next couple of turns;
- for the **active** unit, `activeUnitTargets` lists, per living enemy: `canShoot`,
  `meleeAttackFromHexes` (hexes you can attack it from — feed one straight into
  `battle_attack`'s `attackFromHex`), the `damage`/`kills` estimate, and expected
  `retaliationDamage`. Use these to choose the best target instead of guessing geometry.

Hexes are single integers (0–186, 17 per row). `meleeAttackFromHexes` for a double-wide attacker
is a close approximation — if an attack is `rejected`, just pick another hex from the list.

Tactics phase: if `get_battle_state` shows `tacticsDistance > 0` and `tacticsSide` is yours,
reposition with `battle_move`, then `battle_end_tactics`.

---

## 7. Common pitfalls

- **Not reading `events`.** The envelope already tells you what happened; re-querying everything
  after each action wastes turns and context.
- **Ignoring `pendingQueries`.** A blocked query silently stalls your whole turn. Check after
  every action.
- **Treating `rejected` as fatal.** It's the normal "that move is illegal" signal — read
  `error`, adjust, continue.
- **Polling instead of waiting.** Use `wait_for_event` for "not my turn" / "waiting for the next
  battle unit", not a `get_game_state` spin loop.
- **Re-fetching static data.** `list_creatures` et al. never change mid-game — cache them.
- **Dumping the whole map.** Use `get_tiles` on the region you care about, and `get_map_content`
  for object positions.
- **Forgetting `set_llm_control` at start**, then wondering why a human clicking dialogs fights
  you for control.
- **Guessing battle geometry.** Let `get_battle_state`'s `reachableHexes` /
  `activeUnitTargets.meleeAttackFromHexes` tell you what's legal.
