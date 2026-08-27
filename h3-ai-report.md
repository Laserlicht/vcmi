# Heroes of Might & Magic III — Adventure & Battle AI
## Reverse-engineering report for NH3API

**Target binary:** `/home/abc/Downloads/Heroes3.exe`
**SHA-1** `bdf6ac81e843490476c5d3d75d5e4d85fcf75916` · **MD5** `641c9cb8800b3768ce5d89eb1e4c48cd`
PE32, ImageBase `0x400000`, `.text` `0x401000–0x639612`, link time 2000-09-08.
Version resource: **`FileVersion 4.0` / "Heroes of Might and Magic III: The Shadow of Death"** — i.e. the
GOG **HoMM3 Complete 4.0** executable.

---

# 0. Read this first — build mismatch with NH3API

NH3API (`void2012/NH3API`) targets **SoD 3.2**. The binary supplied here is **Complete 4.0**. The AI
source is the same, but the *addresses* only partly line up. I verified this mechanically: of the 922
code addresses I could scrape out of the NH3API headers, only **120 land on a real function entry**
in this binary.

| NH3API region | Match rate in this exe |
|---|---|
| `0x42xxxx` (combat sim + adventure-AI kingdom code) | 43 / 44 — **matches** |
| `0x40xxxx` | 25 / 41 |
| `0x43xxxx` | 7 / 60 |
| `0x44xxxx`–`0x62xxxx` | ≈ 0–15 % |

Confirmed deltas I established by following vftable writes and call sites:

| Symbol | NH3API (SoD 3.2) | This build (Complete 4.0) | Δ |
|---|---|---|---|
| `type_AI_combat_parameters::ctor` | `0x435B10` | **`0x435EC0`** | +0x3B0 |
| `get_simple_attack_effect` (7-arg) | `0x4357E0` | **`0x435B90`** | +0x3B0 |
| `get_simple_attack_effect` (5-arg) | `0x4358C0` | **`0x435C70`** | +0x3B0 |
| `type_AI_attack_hex_chooser::ctor` | `0x435D10` | **`0x4360C0`** | +0x3B0 |
| `type_AI_attack_hex_chooser::find_attack_hex` | `0x436490` | **`0x436840`** | +0x3B0 |
| `type_AI_spellcaster::ctor` | `0x436610` | **`0x4369C0`** | +0x3B0 |
| `AI_value_of_morale` | `0x435480` | **`0x435830`** | +0x3B0 |
| `AI_value_of_luck` | `0x4355B0` | **`0x435960`** | +0x3B0 |
| `value_of_luck_and_morale` | `0x4354A0` | **`0x435850`** | +0x3B0 |
| `AI_resource_cost(playerData*)` | `0x527150` | **`0x526C70`** | −0x4E0 |
| `armyGroup::get_AI_value` | `0x44A950` | **`0x44AC80`** | +0x330 |
| `type_spellvalue::ctor` | `0x527220` | **`0x526D40`** | −0x4E0 |
| `type_spellvalue::get_best_spell_value` | `0x527A90` | **`0x5275B0`** | −0x4E0 |

Everything below is stated with **Complete 4.0 addresses**. The *algorithms, structure layouts and
constants* are build-independent and transfer to 3.2 unchanged.

### Bug found in NH3API

`adventure_AI.hpp` declares

```cpp
inline std::array<type_AI_player, 8>& AI_player = get_global_var_ref(0x6929A0, ...);
```

`0x6929A0` has **zero references** anywhere in the image. Every AI site computes the array as
`0x692950 + player * 0x98`:

```
00525EF8  lea  ebx, [eax*8 + 0x692950]      ; eax = 19*player  →  152*player
0042D4C8  lea  ecx, [eax*8 + 0x692950]
0052549E  lea  ecx, [eax*8 + 0x692950]
00526A24  lea  ecx, [edx*8 + 0x692950]
00526D2E  lea  ecx, [eax*8 + 0x692950]
004340E2  mov  ecx, 0x692950
```

**`type_AI_player AI_player[8]` lives at `0x692950`**, stride `0x98`. The `0x6929A0` in the header is
off by `0x50`. (Same base in 3.2 unless the .data layout shifted — worth re-checking there, but the
`0x6929A0` value cannot be right for *any* build that indexes with stride 0x98 from a `lea …*8+base`,
since the last field ends at +0x98.)

Also: `type_AI_player::team` at +0 is **not** the team — it is the **player colour / index**. It is used
as the index into `gpGame->playerData[]` and into the team byte-array `gpGame+0x1F879`.

---

# 1. Method

No IDA/Ghidra available, so I built a small capstone-based toolkit in `/home/abc/h3ai/`:

* `h3.py` — PE loader, full-image linear sweep, call/jump/data xref database (836 887 instructions,
  12 928 function entries recovered from call targets + `.rdata/.data` code pointers + `int3` padding).
* `d` — annotated function dump, `q` — function metadata, `cg` — call-graph walker.
* Symbol seeding from the NH3API headers (`syms.txt`), then hand-verification of every symbol used.
* Jump tables decoded directly out of `.text` (object-type dispatch, creature-ability dispatch).

---

# 2. Global state the AI touches

| Address | Meaning |
|---|---|
| `0x6994E8` | `NewGame* gpGame` |
| `0x6994E8 + 0x20AD0` | `playerData playerData[8]`, stride **`0x168`** |
| `0x6994E8 + 0x21620` | `hero heroes[]`, stride **`1170 = 0x492`** |
| `0x6994E8 + 0x21614` | `town* towns` base pointer |
| `0x6994E8 + 0x1FB70` | `NewfullMap map` |
| `0x6994E8 + 0x1F879` | `int8 team[8]` |
| `0x6994E8 + 0x1F636` | `int8 playerIsX[8]` (skip flag: not-in-game / human) |
| `0x6994E8 + 0x1F6D8` | **game difficulty** (0 Easy … 4 Impossible) |
| `0x6994E8 + 0x1F698` | AI "combat skill" level (gate for extra battle-AI passes) |
| `0x6994E8 + 0x1FC48` | has-underground flag |
| `0x6994E8 + 0x1F63E` | int16 **day of the week** (1…7) — see §4.8a, Seer Hut |
| `0x6994E8 + 0x1F640` | int16 **week of the month** (1…4) |
| `0x6994E8 + 0x1F642` | int16 **month** |
| `0x6994E8 + 0x1F89C / 0x1F8D0` | victory-condition type / target hero id |
| `0x6994E8 + 0x1F8E8 / 0x1F904` | loss-condition type / target hero id |
| `0x692950` | **`type_AI_player AI_player[8]`**, stride `0x98` |
| `0x69CCA8` | current player index (`int`) |
| `0x69CCB0` | `playerData*` of the player currently taking a turn |
| `0x69CCC4` | current player's **visibility bit mask** |
| `0x69CCD4` | "AI is moving a hero" flag |
| `0x6972B8` | abort / game-over flag — every AI loop bails when non-zero |
| `0x699284` | global `searchArray*` (path/BFS results, also reused for combat reachability) |
| `0x699268` | adventure-map window / manager |
| `0x6989F8` | visibility map, `uint16` per cell |
| `0x6783C8 / 0x6783CC` | `MAP_WIDTH` / `MAP_HEIGHT` |
| `0x6703B8` | `TCreatureTypeTraits traits[]`, stride **`116 = 0x74`**; `0x6747B0` holds the base pointer |
| `0x6747B4` | `gDwellingType[townType*14 + level]` |
| `0x691684` | AI-turn progress counter (drives the "computer is thinking" bar, clamped to 9) |
| `0x6604D0` | `double AI_enemy_strength_multiplier[5]` (see §4.11) |
| `0x6604F8 / 0x6604FC` | `float attack_computer_bonus / attack_human_bonus` (both 0.5) |
| `0x63B780 / 0x63B788` | `double morale_good = 0.0173`, `morale_bad = −0.0833` |
| `0x63B790 / 0x63B794` | `float luck_good = 0.0173`, `luck_bad = −0.0122` |
| `0x67814C` | `int 2500` — XP-valuation base constant |
| `0x63BD00` | per-row first-hex table for the 187-hex battlefield (`hex/17`) |

### Packed map coordinate

The AI passes destinations around as a **32-bit packed coordinate**, not an `x,y,z` triple:

```
bits  0..9   x   (signed 10-bit;  0x3FF == -1 == "invalid")
bits 10..15  unused
bits 16..25  y   (signed 10-bit)
bits 26..29  z   (level, 4-bit)
```

Canonical decode as emitted by the compiler:

```
x = (int16)(w0 << 6) >> 6
y = (int16)(w1 << 6) >> 6
z = (int16)(w1 << 2) >> 12
```

### `HeroDestination` (16 bytes) — the AI's "where to go" record

The AI's object list and the chosen destination use the same 16-byte record:

| off | type | meaning |
|---|---|---|
| +0x00 | packed coord | target cell |
| +0x04 | int32 | value |
| +0x08 | int32 | move cost / limit |
| +0x0C | bool | reachable this turn |
| +0x0D | bool | flag set by destination chooser |

(NH3API's `HeroDestination` is a *different*, 12-byte struct — this one is the record actually stored
in the object-scan vector.)

### `hero` fields used by the AI

| off | type | meaning |
|---|---|---|
| +0x00 / +0x02 / +0x04 | int16 | x, y, z |
| +0x1A | int32 | hero id |
| +0x22 | int8 | owner colour (−1 = none) |
| +0x35 / +0x39 | int32 | **AI destination x / y** (−1 = none) |
| +0x3D | int32 | AI destination z |
| +0x41 | int16 | (added to search radius) |
| +0x43 | int8 | cleared when destination invalidated |
| +0x44 / +0x45 / +0x46 | int8 | previous destination x / y / z (`0xFF` = none) |
| +0x49 | int32 | max movement points |
| +0x4D | int32 | movement points remaining |
| +0x55 | int16 | level |
| +0x57 / +0x5B / +0x5F / +0x63 / +0x6F | int32 | visited-object bitmasks (Learning Stone, Marletto, Garden, Mercenary Camp, Library) |
| +0x91 | `armyGroup` | 7 × `int32 type` then 7 × `int32 count` (56 bytes) |
| +0xC9 | int8[28] | **secondary-skill levels** (0 = absent … 3 = expert) — see §4.12; an earlier gloss of this field as per-creature-type flags was wrong |
| +0xCD / +0xCF / +0xD0 | int8 | secondary-skill levels (`0xC9 + n`); **`+0xD0` = Wisdom** |
| +0x101 | int32 | army slot count |
| +0x105 | int32 | **AI "already visited this kind of object" flag word** |
| +0x109 | **float** | **value of one experience point** (see §4.9) |
| +0x11C | bool | "hero is done for this turn / asleep" |
| +0x476 / +0x477 / +0x478 / +0x479 | int8 | **primary skills: attack / defence / spell power / knowledge** |
| +0x47A | float | this hero's combat-strength modifier |
| +0x47E / +0x482 / +0x486 | int32 | AI value of **+1 spell power** / **+1 spell duration** / **+1 knowledge** — produced by `AI_update_valuations`, §4.9b |
| +0x48A / +0x48E | int32 | AI value of **mana refilled to 2× maximum** (Magic Spring) / **to maximum** (Magic Well) |
| +0x3EA | int8[70] | spells the hero **knows** |
| +0x430 | int8[70] | spells **available** to the hero (known + granted by artifacts) |
| +0x12D | 8 bytes × 19 | equipped artifact slots (`{int type; int param;}`) |

### `playerData` (`0x168` bytes)

Fields marked **(map)** are read one byte at a time from the map/save stream by
`playerData::deserialise` @ `0x4BA260`, in the order `+0x30, +0x34, +0x38, +0x3D, +0x3E, +0x3F, +0x00`.

| off | meaning |
|---|---|
| +0x00 | int32 **(map)** |
| +0x01 | number of heroes (int8) |
| +0x30 | int8 **(map)** — no consumer anywhere in this build |
| +0x34 | int32 **(map)** — **AI personality** (0 Warrior, 1 Builder, 2 Explorer, 3 Human); see §6A.1 |
| +0x38 | int8 **(map)** — bonus puzzle pieces, read only by `0x4BAF00` (§4.14) |
| +0x39 / +0x3B | packed coord — the **Grail dig-site guess** (§4.14) |
| +0x3D | int8 **(map)** |
| +0x3F | int8 **(map)** — no consumer anywhere in this build |
| +0x08 | `int32 heroIds[]` |
| +0x3E | number of towns (int8) |
| +0x40 | `int8 townIds[]` |
| +0x9C | `int32 resources[7]` (gold at +0xB4) |
| +0x108 | `int32 income[7]` |
| +0x128 | **`double resource_value[7]`** — the AI's marginal value of each resource; `+0x158` is GOLD |
| +0x164 | float — average artifact value used for Pandora/creature-bank estimates |

### `type_AI_player` (`0x98`, at `0x692950`)

| off | meaning |
|---|---|
| +0x00 | int16 **player index** (NH3API calls it `team`) |
| +0x04 | magus-hut value |
| +0x08 | `int32 reserved_funds[7]` |
| +0x24 | `int32 resource_supply[7]` |
| +0x40 | `int32 resource_demand[7]` |
| +0x60 | `double resource_value[7]` |

### `combatManager` fields used by the battle AI

| off | meaning |
|---|---|
| +0x3C | **chosen action code** (3 = defend/wait, 4 = …, 6 = attack, 8 = …) |
| +0x40 / +0x44 | action parameters (target stack index / target hex) |
| +0x1D4 | `hexcell cells[187]`, stride **`0x70 = 112`** |
| +0x53CC / +0x53D0 | `hero* hero[2]` |
| +0x53C0 / +0x53C4 | combat mode flags |
| +0x54A4 | `bool sideIsComputer[2]` |
| +0x54BC | `int32 stackCount[2]` |
| +0x54CC | `army armies[2][21]`, stride **`0x548 = 1352`**, side stride `0x6EE8` |
| +0x132B8 / +0x132BC | current side / current stack index |
| +0x132C0 | (passed through to the AI as `arg4`) |
| +0x132C8 | cleared at start of AI turn |
| +0x132F4 | siege state (0 = field, ≥2 = town with walls, 3 = …) |
| +0x13D68 | quick-combat / auto-combat flag |
| +0x13DE4 | flag suppressing the "wait" action |
| +0x13F60 | wall section states |

`army` fields the AI reads: `+0x10/+0x14` target (cleared to −1), `+0x34` creature type, `+0x38` hex,
`+0x44` facing (0 = left), `+0x4C` count, `+0x58`, `+0x84` flag bits (**bit 0 = two-hex / double-wide** — see §5A.9, bit 5, bit 19, bit 21 = dead/removed,
bit 22), `+0x290 / +0x2B0 / +0x2C0` spell-effect state.

---

# 3. Top-level map of the AI code

| Range | Contents |
|---|---|
| `0x41E190–0x423C80` | **Battle AI** — `combatManager` AI methods, target choice, creature abilities |
| `0x423C80–0x427800` | **Combat simulation** — `type_AI_combat_data`, quick combat, `AI_value_of_combat` |
| `0x4280E0–0x432300` | **Adventure AI** — kingdom/town management, map object scan, value & danger maps |
| `0x4336C0–0x435800` | Artifact / luck / morale valuation |
| `0x435800–0x43C900` | Battle-AI evaluation helpers, hex chooser, `type_AI_spellcaster` |
| `0x525E80–0x52C900` | Adventure-AI turn driver, per-object value evaluators, hero valuation |

---

# 4. Adventure AI

## 4.1 Turn driver — `AI_take_turn` @ `0x525E80`

`__thiscall(this /*advManager*/, int player)` — the only caller is `0x4087B0`.

```
1.  bail out entirely if gpGame->abort (0x6972B8) or the "only this player" gate (0x6994F0) rejects
2.  value_map = calloc((levels) * MAP_W * MAP_H * sizeof(int32))     ; one int per map cell
3.  AI_player[player].begin_turn()                    -> 0x4297C0
4.  advManager::AI_prepare(player)                    -> 0x527960
5.  bump the "computer is thinking" progress counter (0x691684), clamped to 9,
    stepped according to the player's hero count
6.  PASS 1 — special heroes:
        while ((h = AI_pick_special_hero(player, &flag)) != 0)
            AI_hero_turn(h, value_map, flag, &globalFlag)      ; 0x526A90 / 0x5261F0
7.  KINGDOM PHASE:  AI_player[player].manage_kingdom()          ; 0x428DD0
8.  PASS 2 — main hero loop:
        repeat
            pick the town-visiting / best hero by the +0x476..+0x479 priority score
            (see below); if none, pick any garrisoned hero in a town with a
            free slot and mobilise it (0x5BE390)
            AI_hero_turn(hero, value_map, …)                    ; 0x5261F0
        until no hero remains or gpGame->abort
9.  free(value_map); refresh screen
```

**Hero ordering heuristic** — `hero::get_primary_skill_sum` @ `0x4E5960`:

```c
int score = 0;
for (int i = 0; i < 4; ++i) {           // attack, defence, power, knowledge
    int8 v = hero->b[0x476 + i];
    if (v > 99)     score += 99;
    else if (v > 0) score += v;
    else            score += (i >= 2) ? 1 : 0;   // power/knowledge count as ≥1
}
```

The AI moves the hero with the **highest primary-skill total** first; among equals it prefers a hero
whose `+0x44` (previous destination) is set. The same function is the scale factor in every
luck/morale object valuation (`(sum + 40) / 40`).

## 4.2 Per-hero wrapper — `AI_hero_turn` @ `0x5261F0`

`__fastcall(hero* h, int* value_map, bool aggressive, bool* magusHutFlag)`

```
1.  if the hero stands on a town tile → town::AI_visit (0x5253D0)
2.  if hero has no destination and no movement left → hero->mp = 0; return
3.  zero the value_map
4.  if difficulty > 0, or the current player is a human's ally:
        AI_add_enemy_threats(h, value_map)                     ; 0x42DE50   (danger map)
5.  if the player has ≥ 10 wood and ≥ 1000 gold:
        for each town: mark "can build a Capitol/upgrade" bit (0x800 in map-cell flags)
        for each town in the player's build queue: set flag 0x08 on its cell
6.  gpSearchArray->value_map = value_map     (0x699284 + 0x6C)
7.  hero::AI_take_turn(aggressive, magusHutFlag)                ; 0x5267B0
8.  clear the town flags again
9.  if the hero ended standing on a town / garrison → dock it (0x4B9FC0) or continue
```

## 4.3 The hero's own turn — `hero::AI_take_turn` @ `0x5267B0`

`__thiscall(hero*, bool unlimitedRange /*dl*/, bool* magusHutFlag)`

```c
int radius = 32000;                                    // 0x7D00, "search everywhere"
if (hero->AI_dest_x >= 0)                              // already has a goal
    radius = max(hero->mp + hero->w[0x41] + 200, 1000);
if (unlimitedRange) radius = 32000;
if (hero->prev_dest_x != 0xFF) {                       // shrink around last goal
    int d = |y - prevY| + |x - prevX| + prevZ;
    radius = min(d * 200, radius);
}

HeroDestination dest; dest.x = -1;                     // 0x3FF
for (int attempt = 0; attempt < 5; ++attempt) {
    value = hero->AI_choose_destination(radius, &dest, &nearbyFlag, 1, magusHutFlag);   // 0x42E0B0
    if (dest.x >= 0 && (value >= 0 || nearbyFlag)) break;
    if (!gpSearchArray->b[0x20]) break;
    if (radius >= 32000) break;
    radius = min(radius * 2, 32000);                   // widen and retry
}

if (dest.x < 0) {                                      // nothing worth doing
    hero->done = true; hero->AI_dest_x = hero->AI_dest_y = -1;
    return;
}
if (destination is unexplored && value < 75 && gpGame->w[0x1F63E] /*day of week*/ == 7) {
    hero->done = true; return;                         // not worth exploring
}
hero->AI_move_to_destination(&dest, &nearbyFlag, magusHutFlag);   // 0x42FEE0

if (the hero landed on an explored magus hut) {
    AI_player[curPlayer].reset_magus_hut_value();      // 0x429AB0
    *magusHutFlag = false;
    for every hero of this player: hero->done = false; // wake everyone up again
}
```

The **5-attempt doubling search radius** is the core "look further if nothing nearby is worth it" loop.

## 4.4 Object scan — `hero::AI_scan_objects` @ `0x42EDD0`

`__fastcall(hero*, searchArray*, HeroDestination* out /*+list*/, int range, int, int, bool ignoreCost)`
→ returns an `exe_vector<HeroDestination>` (16-byte elements) plus a scalar value.

```
1.  visited = malloc(int16[levels*W*H]) filled with 0xFFFF
2.  searchArray::compute_paths(hero, visited, range, 4|5)                 ; 0x42F570
      → Dijkstra over the whole map from the hero, bounded by `range` movement points
3.  if the hero stands on a tile with an object of ownership > 1 → early out
4.  for each reachable cell produced by the search (searchArray +0x5C..+0x60):
      if !ignoreCost and cell.cost >= cell.limit → skip
      objCell = map->GetObjectCell(coord)                                 ; 0x412BD0
      if the cell has no object (bit 0x10 of objCell+0x0D clear):
          if the cell is already explored by this player AND the player owns ≥1 town
              → skip;
          else  value = 100 + (ignoreCost ? 0 : 100'000)                  ; exploration reward
      else:
          value = hero->AI_object_value(&limit, coord)                    ; 0x528040
          if value == 0 → skip;  if value < 0 and no "critical" flag → skip
      append {coord, value, cost, …} to the result vector,
      keeping the vector sorted / deduplicated
```

Notable: the **exploration drive** is not a separate system. Unexplored map cells simply get a flat
value of `100` (or `100 100` when the caller asks for "critical" mode), and only when the player
actually owns a town — a heroless/townless player will not chase fog.

## 4.5 The influence map — `hero::AI_choose_destination` @ `0x42E0B0`

This is the heart of the adventure AI (2 928 bytes). Called from `0x5267B0` and `0x4302D0`.

`__thiscall(hero*, int radius /*edx*/, HeroDestination* out, bool* nearby, int, int, bool)`

### Phase A — build the value grid

```c
list = AI_scan_objects(hero, gpSearchArray, …, radius, …);      // 0x42EDD0
grid = calloc(int32, levels * W * H);

for (each entry in list) {
    objCell = map->GetObjectCell(entry.coord);
    if (!cell_is_explored(entry.coord)) {           // 0x4F79B0 & 0x69CCC4
        grid[cellIndex] += entry.value;             // no spreading for fog
        continue;
    }
    // 11×11 window around the object, clamped to the map
    searchArray::compute_paths(&localSearch, hero, entry.coord,
                               range = 60000, turnLimit = 500, …);   // 0x56B440

    // approach cost = max path cost over the 3×3 ring around the object
    base = max over dx,dy in [-1..+1] of localSearch.cell(coord+d).cost;

    for (each cell reachable in localSearch) {
        int v;
        if (cell.cost <= base) v = entry.value;
        else                   v = entry.value * 300 / (cell.cost - base + 300);
        grid[cellIndex(cell)] += v;
    }
}
```

**The decay law is `value × 300 / (300 + extra_move_cost)`.** 300 movement points ≈ 1.5 tiles of grass
(a plain grass step costs 100). So an object's pull halves roughly every 3 tiles of extra walking, and
the grid is the *sum* over all objects — that is how the AI naturally walks toward clusters of goodies
rather than the single best item.

### Phase B — pick the destination

```c
mpQuantum = hero->maxMovement * 21 / 100;    // ~1/5 of a turn
noTowns   = (playerData.townCount == 0);

if (out->x < 0) {                            // no target yet
    // scan the 8 neighbours of the hero (delta table at 0x678150, 8 x 4 bytes — see §6A)
    // pick the adjacent, passable, non-fogged, danger-free cell with the
    // best grid[] value that the hero can actually enter
    ...
} else {
    // the caller already fixed a target: decide whether it is reachable this turn
    out->reachable = (path.cost <= mpQuantum) or the target is the current cell;
    if (path.turnsNeeded > path.cost) out->reachable = false;
    if (value < 0 && !noTowns)         out->reachable = false;
}

// final pass over the object list, choosing the entry with the best
// value / cost trade-off, honouring the danger map
```

Special-cases before the generic scan: if the target cell holds **our own town** (`objType 0x62`) or a
**garrison** (`0x50`), or an already-owned object, the hero is sent straight there
(`0x4D75D0(0x22, heroId)` = "issue move order").

## 4.5a The scan/choose loop, filled in

### What `*limit` actually is

`AI_object_value`'s second argument is `&searchCell->cost`, taken straight out of the pathfinder
result: **the movement-point cost of reaching this object**. That single fact makes every
"movement term" in §4.8a coherent:

* an object that *grants* movement subtracts its grant from the cost; if the grant covers the whole
  trip the cost drops to 0 and the handler returns the sentinel **10 000** ("this object pays for its
  own approach");
* an object that merely *costs* movement compares the cost against `hero->movementRemaining` and
  returns 0 when it is out of reach;
* the Magic Well's `cost > 300` test is "this is more than three tiles of detour away".

The two cost fields of the 30-byte search-cell record are **`+0x18`** and **`+0x1A`**, both `int16`,
both seeded to 0 at `0x56B83C` / `0x56B8F3`. They are distinguished by their consumers, and the
teleport branch settles which is which:

| field | written | read by |
|---|---|---|
| `+0x1A` | `+= 50` on a monolith/gate step (`0x56B9D9`) | passed to `AI_object_value` as `*limit` (`0x42F11B`) — **raw movement cost** |
| `+0x18` | — | compared against `visited[]` in the friendly-hero suppression (`0x42F094`, `0x42F0FE`), and against `hero->mp` when `AI_choose_destination` writes `hero->w[0x41]` (`0x42EB6C`) — **turn-adjusted cost** |

This supersedes the looser gloss in §4B.7 ("cost at +0x18, turn count at +0x1A").

### Where the "critical" flag comes from

`AI_build_reachability` (`0x42F570`) returns, and `AI_scan_objects` stores in the local at
`[ebp−0x14]`, exactly one thing:

```c
// 0x42F5EE … 0x42F63C
return sa->valueMap /*+0x6C*/ ? sa->valueMap[cellIndex(hero->pos)] : 0;
```

— **the influence-grid value at the hero's own cell.** `AI_scan_objects` then uses it as the
tie-breaker for worthless objects:

```c
value = hero::AI_object_value(hero, &searchCell->cost, coord);     // 0x528040
if (value >  0) keep;
if (value <  0) skip;                       // negative-valued objects are ALWAYS skipped
if (value == 0) keep only if gridValueAtHeroCell < 0;
```

So the "critical" mode is not a flag a caller sets: it is **"the hero is standing on a cell with
negative influence"** — i.e. inside the danger map's threat zone (§4.6). A hero under threat will
accept a zero-value destination purely to be somewhere else; a hero on safe ground will not.

`AI_scan_objects` returns the same number as its scalar result, which is what `AI_hero_turn` tests.

### The unexplored-cell branch, corrected

```c
uint8 vis = map_visibility_mask(x, y, z);                 // 0x4F79B0
if (!(g_ourPlayerBit /*0x69CCC4*/ & vis) && gpCurPlayer->townCount /*+0x3E*/ > 0)
    value = 100 + (wideMode ? 99900 : 0);                 // 0x1863C + 0x64
else
    value = hero::AI_object_value(...);                   // as above
```

The condition is *unexplored **and** we own at least one town* → flat exploration reward. A townless
player falls through to ordinary object valuation, so it never chases fog. (An earlier draft of §4.4
had this inverted.)

### The friendly-hero suppression, exactly

`AI_build_reachability` fills the `int16 visited[]` array (initialised to `0xFFFF`) as a **cost
floor**, not as a hard mask:

```c
for (each other hero H of ours reachable from us) {
    mapCoords goal; int handicap;
    if (!coords_valid(H->AI_dest)) { goal = H->pos; handicap = 0; }
    else { goal = H->AI_dest;
           int est = H->w[0x41];
           handicap = (est > H->mp) ? 0 : est - H->mp; }        // ≤ 0

    searchArray::compute(&tmp, H, H->pos, goal, H->maxMp, …);
    for (each cell C reachable by H) {
        if (!hero::can_enter(H, C.terrain)) continue;           // 0x4E56E0
        uint16 v = C->w[0x18] + handicap;
        if (v < visited[cellIndex(C)]) visited[cellIndex(C)] = v;
    }
}
```

and `AI_scan_objects` applies it as

```c
if (g_objectSuppressible[objType] != 0 && searchCell->w[0x18] > visited[cellIndex])
    skip;                                                       // 0x42F0C1 … 0x42F10D
```

**A cheaper friendly hero wins the object; an equal or more expensive one does not.** The handicap is
negative or zero, so a hero that still has far to go to its *own* destination gets a discount — it is
treated as being closer than it is, and therefore claims more cells.

`g_objectSuppressible` is the byte array at `0x693718` (232 entries, one per `MapObjectType`), filled
by the initialiser at `0x428070`:

```c
memset(g_objectSuppressible, 0, 232);
char *d = g_objectSuppressible;
for (int *p = g_suppressibleList /*0x660540*/; *p; ++p) *d++ = 1;   // ← writes d[0…36], NOT d[*p]
```

The list at `0x660540` holds 37 object type ids — `{5, 6, 9, 10, 12, 13, 16, 22, 24, 29, 37, 39, 42,
48, 53, 54, 55, 57, 58, 59, 60, 62, 63, 79, 80, 81, 82, 84, 85, 86, 93, 99, 101, 105, 108, 109,
112}` — but the loop increments the *destination* pointer instead of indexing by the value it just
read. The shipped effect is that **object types 0…36 are suppressible and 37…231 are not**, which
overlaps the intended set only by accident. Reproduce the shipped behaviour, not the intent; the
list is given above so the intent is recoverable if you would rather fix it.

### The destination score — `AI_choose_destination`'s final trade-off

For each candidate the chooser calls `hero::AI_reevaluate_step` (`0x42F980`) to get an up-to-date
value, then:

```c
// 0x42EA9F … 0x42EB2D
int value = AI_reevaluate_step(...);
int score;
if (value > 0) {
    int cost = pathCost;                            // the entry's stored cost
    score = (cost > 100) ? value * 100 / cost : value;
    if (score < 1) score = 1;
} else {
    score = value;                                  // 0 or negative
    if (score < 0) reachableThisTurn = false;
}
```

**That is the whole "value / cost trade-off": `value × 100 / cost`, floored at 1, with costs at or
below one grass step (100) charged nothing.** It is a pure value-per-movement-point rate, so a
mediocre object next door beats a rich one three turns away whenever the ratio says so.

Acceptance against the running best is a lexicographic rule on *(reachable-this-turn, turns, score)*:

```c
if (!inDanger && score <= best) reject;
if (forced)                     accept;
if (!bestReachable && !candidateReachable) accept iff score >  best;
if ( bestReachable &&  candidateReachable) accept iff (candTurns <  bestTurns)
                                                  || (candTurns == bestTurns && score > best);
// a reachable candidate always beats an unreachable best
```

On acceptance the chooser writes the destination back into the hero
(`+0x35/+0x39/+0x3D`), stores the residual cost in `hero->w[0x41]`
(`cost − hero->mp`, floored at 0) and the "reachable" byte in `hero->b[0x43]`.

### The radius-widening loop and `searchArray + 0x20`

```c
// hero::AI_take_turn, 0x526860 … 0x5268C3
int radius = startRadius;
for (int attempt = 0; attempt < 5; ++attempt) {
    value = hero::AI_choose_destination(hero, radius, &dest, &nearby, …);   // 0x42E0B0
    if (dest.x >= 0 && (value >= 0 || nearby)) break;      // we have a destination
    if (!gpSearchArray->b[0x20]) break;                    // ← the abort flag
    if (radius >= maxRadius) break;
    radius = min(maxRadius, radius * 2);
}
```

`gpSearchArray` is `*(searchArray**)0x699284`. **`searchArray + 0x20` is cleared unconditionally at
the top of `searchArray::compute` (`0x56B46F`) and is written nowhere else in the whole image.**
`AI_choose_destination` always calls `compute` at least once, so by the time the loop tests the flag
it is always 0 — the five-attempt doubling loop therefore **executes exactly one iteration**. Model
it as a single pass; a faithful reimplementation must not widen the radius.

### Map-cell build flags `0x800` / `0x08`

Step 5 of `AI_hero_turn` (§4.2) sets bits `0x800` and `0x08` on the destination map cell before
calling the search. They are inputs to `searchArray::compute`'s own terrain tests and are consumed
inside the engine; nothing in the AI ever reads them back. They exist to bias the *engine's* search
array, which is why an AI reimplementation that owns its own pathfinder has nothing to reproduce
here.


## 4.6 The danger map — `hero::AI_add_enemy_threats` @ `0x42DE50`

`__thiscall(hero*, int* value_map)`

```c
for (int p = 0; p < 8; ++p) {
    if (team[p] == team[ourColour]) continue;
    if (gpGame->b[0x1F636 + p])     continue;
    for (each hero e of player p) {
        int v = AI_value_of_combat(ourHero, e, &e->army, NULL, NULL);   // 0x427330
        if (v >= 0) continue;                           // we'd win — not a threat

        int reach = e->movementRemaining + 300;          // 0x4E4D90 + 300
        searchArray::compute_paths(gpSearchArray, e, e->coord, 3, reach, …);

        for (each cell e can reach) {
            if (v >= -500'000'000) value_map[cell] += v;      // graded danger
            else                   value_map[cell] = -1'000'000'000;  // absolute no-go
        }
    }
}
```

So: **every cell an enemy hero can reach next turn gets the (negative) combat value added**, and cells
threatened by a hero that would *annihilate* us are hard-blocked with `-1e9`. The two magic constants
are `0xE2329B00 = −500 000 000` and `0xC4653600 = −1 000 000 000`.

## 4.7 Move execution — `hero::AI_move_to_destination` @ `0x42FEE0`

Builds the actual path (`0x430610`), issues the movement, handles boarding/leaving boats
(`0x40EC90`), and demobilises the hero (`0x4175E0`) when it ends inside a town.
`0x42F980` re-evaluates the accumulated value along the path as the hero walks
(it calls `AI_object_value` again per step and adds to a running total; a step whose running total
drops below −500 000 000 aborts the move).

## 4.8 Object valuation — `hero::AI_object_value` @ `0x528040`

`__fastcall(hero*, int* limit /*edx*/, uint32 packedCoord)` → `int32`. 5 712 bytes, dispatched by

```
objType = objCell->d[0x1E];                      // MapObjectType
if (objType-4 > 0x6D) return 0;
switch (byte[0x529600 + objType-4]) → jumptable at 0x5294F0
```

I decoded the whole table. `0x5294E5` is the "return 0 / not interesting" default.

| Object (id) | Handler | Behaviour |
|---|---|---|
| Arena (4) | `0x52808E` | `2 × expForNextLevel(level) × hero->xpValue`, 0 if already used |
| Artifact (5) | `0x5280CA` | 0 if the backpack is full (`AI_artifact_count(hero,true) >= 64`); else `max(AI_get_value_of_artifact, 10)` then a **sub-switch** on the pickup condition (below) |
| Pandora's Box (6) | `0x528204` | full contents evaluation — see below |
| Black Market (7) | `0x5283FA` | 0 if the backpack is full; else Σ over the 7 offered artifacts of `0x529750`: `max(0, AI_get_value_of_artifact(art) − price × resource_value[priceResource])`, and 0 for any artifact the player cannot afford |
| Keymaster (10) | `0x528459` | `5000` if the player does **not** yet own this border key, `0` if it does |
| Buoy (11) | `0x528488` | `morale_value(+1) × armyValue × (priority+40)/40`; 0 if already boosted or unreachable |
| Campfire (12) | `0x52851E` | `100·amount × goldValue + amount × resourceValue[type]` |
| Swan Pond (14) | `0x52856C` | luck −2 …+2 clamp; `luck_value × armyValue × (priority+40)/40`, and it *rewrites `*limit`* so the AI knows it costs the rest of the turn |
| Creature Bank (16) | `0x528626` | `AI_value_of_combat(guards)`; then + `resource_value(loot)` + `traits[reward].AI_value × count` + `artifactCount × playerData.artifactValue` |
| Creature dwelling (17, 20) | `0x5286E8` | `0x529A30`: 0 if the dwelling is already ours or an ally's; `AI_value_of_combat(guards)` if guarded; then the value of the creatures buyable now (`0x42D780` against the player's purse); **+5 000 000 / obeliskCount** when a "flag all dwellings" victory condition is active |
| Corpse (22) | `0x528723` | backpack not full → `playerData.artifactValue / 5`; full → `200 × goldValue`. Skipped if this corpse was already searched (bitmask in `gpCurPlayer+0xC0`) |
| Marletto Tower (23) | `0x52878B` | `expForNextLevel(level) × hero->xpValue`, 0 if the bit for this tower is already set in `hero+0x5B` |
| Derelict Ship (24) / Utopia (25) | `0x5287C8` | identical to Creature Bank |
| Faerie Ring (28) | `0x52888A` | luck +1 → `luck_value × armyValue × (priority+40)/40` |
| Flotsam (29) | `0x528924` | `175 × goldValue + 5 × woodValue` |
| Fountain of Fortune (30) | `0x52894F` | luck bonus encoded in the object; `luck_value(current, bonus)` |
| Fountain of Youth (31) | `0x528A03` | if ≥ 200 mp left: consume 200 and value morale; else return `10000` |
| Garden of Revelation (32) | `0x528A48` | `hero->d[0x486]` (AI value of +1 knowledge) |
| Garrison (33) | `0x528A6B` | own garrison → `0x42C4A0` (troop-exchange evaluation); enemy → `AI_value_of_combat` |
| Hero (34) | `0x528BD9` | own → merge/refit; enemy → `AI_value_of_combat` |
| Hill Fort (35) | `0x528DF0` | value of upgrading the army minus the cost |
| Hut of Magi (37) | `0x528F2B` | `AI_player[owner].magus_hut_value` (`0x692954`!) |
| Idol of Fortune (38) | `0x528B13` | luck or morale depending on the week |
| Lean To (39) | `0x528F45` | `3 × playerData.d[0x160]`, 0 if already visited (bitmask `gpCurPlayer+0xC4`) |
| Library of Enlightenment (41) | `0x528F72` | requires `hero->level + 2 × hero->b[0xCD] ≥ 10`; value `= armyValue/10 + 2 × (hero->d[0x47E] + hero->d[0x486])` |
| Lighthouse (42) | `0x528FD6` | `1000` if the owner is not on our team, `0` if it already is |
| School of Magic (47) | `0x528FF8` | `0x529F90`: `max(hero->d[0x47E], hero->d[0x486]) − 1000 × goldValue`, gated on `hero+0x77` |
| Magic Spring (48) | `0x52900A` | `0x52B810` — mana refill value |
| Magic Well (49) | `0x529020` | `0x52A510` — mana refill value |
| Mercenary Camp (51) | `0x529033` | `expForNextLevel × xpValue`, gated on `hero+0x63` |
| Mermaid (52) | `0x529070` | `0x527D80` — luck |
| Mine (53) | `0x52909E` | `0x52A010`: `AI_value_of_combat(guards)` + `2 × dailyYield × resource_value[res] × (get_attack_bonus + 1.0)`; daily yields from `0x678288` = {wood 2, mercury 1, ore 2, sulfur 1, crystal 1, gems 1, gold 1000}; **+5 000 000** when a "flag N mines" victory condition matches |
| Monster (54) | `0x5290B0` | `0x52A140` — `AI_value_of_combat` + treasure |
| Mystical Garden (55) | `0x5290C6` | `(500 × goldValue + 5 × gemsValue) / 2` — the average of its two possible rewards; 0 unless bit 10 of the cell data ("regrown") is set |
| Oasis (56) | `0x529105` | `0x52A1E0(hero, 0x80, 400)` — morale + mp |
| Obelisk (57) | `0x529120` | `0x52A2B0` — puzzle-map progress |
| Redwood Obs. (58) / Pillar of Fire (60) | `0x529134` | `0x432220(colour, 20, coord)` — scouting-radius value |
| Star Axis (61) | `0x52914F` | `0x52A380`: `hero->d[0x47E]` (AI value of +1 spell power), 0 if the bit in `hero+0x67` is set |
| Prison (62) | `0x529161` | `0x52A3A0`: `2500 × goldValue + armyGroup::get_AI_value(the freed hero's army)` |
| Pyramid (63) | `0x529174` | `0x52A410` — combat + spell reward |
| Rally Flag (64) | `0x529186` | `0x52A5F0` — luck + morale + mp |
| Refugee Camp (78) | `0x5291A5` | `0x52A700` |
| Resource (79) | `0x5291B7` | `0x52A7C0` — `amount × resource_value[type]` |
| Scholar (81) | `0x5291CD` | `expForNextLevel(level) × hero->xpValue` |
| Sea Chest (82) | `0x5291F6` | `0x52A870`: backpack not full → `artifactValue/10 + 1200 × goldValue`; full → `1200 × goldValue` |
| Seer Hut (83) | `0x529208` | `0x5735A0` — quest reward minus quest cost |
| Crypt (84) / Shipwreck (85) | `0x52922C` | `0x529920` — combat + treasure |
| Shipwreck Survivor (86) | `0x5293EC` | artifact value |
| Shipyard (87) | `0x52923E` | `1000` if the owner is not on our team, `0` if it already is |
| Shrine of Magic I/II/III (88–90) | `0x529270` | `0x5298D0`: 0 if `spellTraits[spell].level > hero->b[0xD0] + 2`, if the hero already knows it, or if the hero has no spellbook; else `AI_get_spell_value(hero, spell)` (`0x527640`) |
| Sirens (92) | `0x529288` | `0x52A960` — XP for sacrificed troops |
| Spell Scroll (93) | `0x529298` | `0x52A8C0` |
| Stables (94) | `0x5292AA` | `0x52AAC0` — +400 mp |
| Temple (96) | `0x5292BC` | morale +1/+2 (day 7) |
| Town (98) | `0x5292F5` | `0x52AB80` — the big town evaluator |
| Learning Stone (100) | `0x529330` | `hero->xpValue × 1000.0f` (const at `0x63E4F0` = 1000 XP) |
| Treasure Chest (101) | `0x529361` | `0x52B4E0`: three tiers, each `max(xpValue × k, goldValue × g)` with (k,g) = (160,320), (320,480), (465,620), plus `artifactValue/20` |
| Tree of Knowledge (102) | `0x529371` | `0x52B790`: `expForNextLevel × xpValue − 1000 × goldValue`, gated on `hero->b[0x7B]` |
| University (104) | `0x529383` | `0x52B5A0`: `(expForNextLevel × xpValue − 10 × gemsValue − 2000 × goldValue) / 3` |
| Wagon (105) | `0x52939E` | artifact or resources |
| War Machine Factory (106) | `0x5293B2` | value of ballista/tent/cart minus cost |
| School of War (107) | `0x5293C4` | +1 attack/defence for 1000 gold |
| Warrior's Tomb (108) | `0x5293D6` | `playerData.artifactValue` (`+0x164`), 0 if visited this week or the backpack is full — **no morale term**; see §4.8a |
| Water Wheel (109) | `0x529418` | gold, halved after the first week |
| Watering Hole (110) | `0x52947F` | morale + mp |
| Windmill (112) | `0x52949A` | resource value |
| Witch Hut (113) | `0x5294D3` | secondary-skill value |
| Boat, Border Guard, Cartographer, Cover of Darkness, dwellings 2/3, Cursed Ground, Event, Eye of Magi, Grail, all Monoliths, Magic Plains, Market of Time, Ocean Bottle, all Random\*, Sanctuary, Sign, Tavern, Den of Thieves, Trading Post, Subterranean Gate, Whirlpool | `0x5294E5` | **value 0 — the AI never targets these on their own** |

That last row is a load-bearing fact: **the SoD AI has no notion of using Monoliths, Subterranean
Gates, Whirlpools, Taverns, Trading Posts or Cartographers as goals.** It only ever walks through a
portal if the path happens to run over it.

### Artifact pickup sub-switch (`jumptable 0x529670`)

`objCell->d[0]` low nibble selects the guard condition:

| nibble | Handler | Meaning |
|---|---|---|
| 0 | `0x528CE2` | free pickup |
| 1 | `0x5281A9` | `AI_pay_for_object(value, 2000 gold, 0, 0)` |
| 2 | `0x528177` | requires `hero->b[0xD0] > 0` (Wisdom) else value 0 |
| 3 | `0x528190` | requires `hero->b[0xCF] > 0` else value 0 |
| 4 | `0x5281C4` | `AI_pay_for_object(value, 2500 gold, n, resource 3)` |
| 5 | `0x5281E4` | `AI_pay_for_object(value, 3000 gold, n, resource 5)` |
| (`& 0xF == 6`) | inline | guarded by creatures → `AI_value_of_combat(guards) + artifactValue` |

`AI_pay_for_object` @ `0x529810`:

```c
if (pd->gold  < goldCost) return 0;
if (pd->res[t] < amount)  return 0;
return (int)(value - goldCost*pd->goldValue - amount*pd->resource_value[t]);
```

### Pandora's Box @ `0x528204` — the most complete evaluator

```
value  = AI_value_of_combat(guards)                        if guarded
       + rewardExp * hero->xpValue
       + AI_resource_cost(playerData, rewardResources)      ; 0x526C70
       + Σ_{i=0..3} rewardPrimarySkill[i] * expForNextLevel*xpValue
       + Σ over reward creature stacks:
             (count - alreadyOwnedOfThatType) * xpValueUnit   (only if the hero
             has a free slot or already owns that creature)
       + artifactCount * playerData.artifactValue (+0x164)
       + Σ over reward spells: AI_get_spell_value(hero, spell)   ; 0x5298D0
       + morale/luck/mp deltas
```

## 4.8a The handlers that the summary table only names

Everything below was left as a bare address in the §4.8 table. Two conventions recur and are worth
stating once:

* **`int *limit` (the `edx` argument of `AI_object_value`) is a movement-point budget, not a bound.**
  Handlers for objects that *grant* movement subtract what they grant from `*limit`; if the grant more
  than covers what is left, they zero it and return the flat sentinel **10 000**. Handlers for objects
  that merely *cost* movement compare `*limit` against `hero->movementRemaining (+0x4D)` and return 0
  when the object is out of reach.
* **`hero + 0x105` is a 32-bit "already got this kind of bonus" word**, one bit per bonus class.
  Almost every one-shot object tests a bit of it first.

### Mana — Magic Well (49) `0x52A510`, Magic Spring (48) `0x52B810`

```c
// Magic Well: __fastcall(hero*, int16 limitValue /*edx*/)
if (hero->b[0x105] & 1) return 0;
uint32 dest = pack(hero->destX /*+0x35*/, hero->destY /*+0x39*/, hero->destZ /*+0x3D*/);
if (map_position_valid(&dest) /*0x4B1330*/ && limitValue > 300) {
    int obj = cellAt(dest)->d[0x1E];
    if (obj != 49 && obj != 48) return 0;      // not already heading for a mana source
}
return hero->d[0x48E];                          // value of a full mana refill, §4.9b

// Magic Spring: __fastcall(hero*, mapCell* /*edx*/) ; ret 4, stack arg = limitValue
if (!((objCell->d[0] >> 6) & 1)) return 0;      // spring already drunk this week
   ... identical destination test ...
return hero->d[0x48A];                          // value of mana up to 2× maximum
```

Both numbers are precomputed once per turn by `hero::AI_update_valuations` (§4.9b), so the handlers
themselves are trivial. The "> 300" test is what stops a hero with plenty of movement left from
detouring for mana.

### Movement grants — Oasis (56), Watering Hole (110), Rally Flag (64), Stables (94)

```c
// 0x52A1E0  __fastcall(hero*, uint32 visitedBit /*edx*/) ; stack: int16 mpGrant, int *limit
int morale_and_movement(hero *h, uint32 bit, int16 mpGrant, int *limit)
{
    if (h->d[0x105] & bit) return 0;
    if (*limit < mpGrant) { *limit = 0; return 10000; }
    *limit -= mpGrant;
    if (armyGroup::is_empty(&h->army)) return 0;                       // 0x44AB20
    double f = AI_value_of_morale(hero::get_morale(h, 0, 0, 1), +1);
    int    w = armyGroup::get_AI_value(&h->army)
             * (hero::get_primary_skill_sum(h) + 40) / 40;
    return ftol((double)w * f);
}
```

* **Oasis (56)** `0x529105` → `morale_and_movement(h, 0x80, 400, limit)`
* **Watering Hole (110)** `0x52947F` → `morale_and_movement(h, 0x40, 200, limit)`

So the "+400 movement points" is priced exactly once, by *charging it to the budget*: an Oasis is
worth `10000` when the hero could not otherwise finish its journey, and only a morale bonus when it
could.

**Rally Flag (64)** `0x52A5F0` does the same with a 200-point grant and bit `0x10000`, then adds the
raw luck and morale fractions:

```c
if (h->d[0x105] & 0x10000) return 0;
int mp;
if (*limit >= 200) {
    *limit -= 200;
    mp = armyGroup::is_empty(&h->army) ? 0
       : ftol((double)w * AI_value_of_morale(hero::get_morale(h, 0, 0, 1), +1));
} else { *limit = 0; mp = 10000; }
return ftol( AI_value_of_morale(hero::get_morale(h, 0, 0, 1), +1)
           + AI_value_of_luck  (hero::get_luck  (h, 0, 0, 1), +1)
           + (double)mp );
```

Note the last two terms are **not** scaled by army value. They are fractions around `0.017`, so after
`ftol` they contribute nothing. That is a shipped quirk, not a transcription slip — see the note on
Fountain and Idol of Fortune below.

**Stables (94)** `0x52AAC0` — `__fastcall(hero*, int *limit /*edx*/)`

```c
int v = 0;
if (!(h->b[0x105] & 2)) {
    int grant = (int16)((8 - gpGame->w[0x1F63E] /*day of week*/) * g_mpPerDay /*0x698A94*/ / 2);
    if (*limit >= grant) { *limit -= grant; v = 50;    }
    else                 { *limit  = 0;     v = 10000; }
}
int cavaliers = hero::count_of_creature(h, 10 /*Cavalier*/);           // 0x4E2340
if (cavaliers == 0) return v;
int gain = (traits[11].AI_value - traits[10].AI_value) * cavaliers;
if (hero::count_of_creature(h, 11 /*Champion*/) != 0)
    gain = ftol((double)gain * 1.2);                                   // 0x63AC20
return v + gain;
```

`g_mpPerDay` at `0x698A94` is runtime-filled (§4E), so do not read it out of the image.

### Obelisk (57) `0x52A2B0`

```c
// __fastcall(int *obeliskCell /*ecx*/, int player /*edx*/)
int obelisk = *obeliskCell;                                     // the cell's first dword
int p       = player;                                           // hero->b[0x22], from 0x529120
if (gpGame->b[0x4E3E9 + obelisk] & (1 << p)) return 0;          // this player already dug it up
if (!gpGame->b[0x1F696])                          return 0;     // no Grail on this map
// if the player has already worked out the dig spot, further obelisks are worthless
if (playerData[p].guessX == gpGame->w[0x1F690]
 && playerData[p].guessY == gpGame->w[0x1F692]
 && playerData[p].guessZ == gpGame->b[0x1F694])   return 0;

pair grail = { 2 /*The Grail*/, -1 };
return AI_get_value_of_artifact_for_player(&grail, p) / gpGame->b[0x4E3E8];   // ÷ obelisk count
```

`gpGame + 0x4E3E9` is a **48-byte array indexed by obelisk**, each byte a bitmask of the players that
have visited it; `gpGame + 0x4E3E8` is the map's obelisk count. (An earlier draft of this section had
the two indices the wrong way round — §4.14 works through the whole chain.)

An obelisk is worth **the Grail divided by the number of obelisks on the map**, and the Grail's value
comes from §4.9a — `income(5000, gold)` plus creature growth on all seven dwelling levels, evaluated
against the player's best-placed hero. This is what makes an AI with a strong Grail town chase
obelisks and an AI without one ignore them.

### Redwood Observatory (58) / Pillar of Fire (60) — `0x432220`

```c
// __fastcall(int colour /*cl*/, int radius /*edx*/) ; stack: uint32 packedCoord
double  r2   = (double)radius + 0.5;                       // 0x63AC70
uint32  mine = 1u << colour;
int     score = 0;
for (int y = max(0, cy - radius); y <= min(MAP_HEIGHT-1, cy + radius); ++y)
for (int x = max(0, cx - radius); x <= min(MAP_WIDTH -1, cx + radius); ++x) {
    if (sqrt((double)((x-cx)*(x-cx) + (y-cy)*(y-cy))) > r2)  continue;
    if (visibility_mask(x, y, z) /*0x4F79B0*/ & mine)        continue;   // already seen
    ++score;
    mapCell *c = &gpGame->cells[...];                       // gpGame+0x1FC40, stride 0x12
    if (c->b[0x0D] & 0x10)                                   // cell carries an object
        score += g_objectScoutValue[c->d[0x1E]];             // int32 table at 0x6925AC
}
return score;
```

So a scouting object is worth **one point per newly revealed tile, plus a per-object-type bonus for
anything it reveals**. `g_objectScoutValue` at `0x6925AC` is indexed by `MapObjectType`.

### Seer Hut (83) — `0x5735A0`

```c
// __fastcall(quest* /*ecx*/) ; stack: hero*
int reward = quest::AI_reward_value(&q->reward /*this+5*/, hero);      // 0x573A70
if (!(q->b[4] & (1 << hero->owner)))
    return max(reward, 20);                    // the hero has not been told the terms yet

questObject *qo = *(questObject**)q;
if (!qo) return 0;
int deadline = qo->d[0x3C];
if (deadline >= 0 && deadline < today()) return 0;         // expired
if (!qo->vft[2](hero))                   return 0;         // the hero cannot satisfy it
return reward - qo->vft[1](hero->owner);                   // reward minus what handing it over costs
```

with

```c
int today() {                                  // the game calendar, decoded here for the first time
    return ((gpGame->w[0x1F642] /*month*/ * 4 + gpGame->w[0x1F640] /*week*/) - 5) * 7
           + gpGame->w[0x1F63E] /*day of week, 1…7*/;
}
```

**`gpGame + 0x1F63E` is the day of the week, not a scenario mode word.** Every other site that reads
it — the `== 7` test in `AI_hero_turn`, the week term in `AI_town_capture_value`, the `8 − x` term in
the Stables handler, the Idol of Fortune branch — is a *day-of-week* test. `+0x1F640` is the week
within the month (1…4) and `+0x1F642` the month.

### Hill Fort (35) — `0x528DF0`

```c
int res[7];  memcpy(res, gpCurPlayer->resources /*+0x9C*/, 28);
int total = 0;
for (int i = 0; i < 7; ++i) {
    int t = hero->army.type[i];   if (t == -1) continue;
    int up = creature_upgrade_of(gpGame, t);              // 0x529710
    if (up == -1) continue;

    int cost[7];
    upgrade_cost(t /*ecx*/, up /*edx*/, hero->army.count[i], cost);    // 0x54E750
    cost[6] = ftol((float)cost[6] * g_hillFortDiscount[traits[t].level /*+0x04*/]);

    for (int j = 0; j < 7; ++j) if (cost[j] > res[j]) goto next;       // unaffordable
    total += (traits[up].AI_value - traits[t].AI_value) * hero->army.count[i];
    for (int j = 0; j < 7; ++j) res[j] -= cost[j];
  next: ;
}
return total;
```

`g_hillFortDiscount` (floats at `0x63EB4C`) = **{0.0, 0.25, 0.5, 0.75, 1.0, 1.0, 1.0}** indexed by
creature level — level 1 upgrades are free, level 2 cost a quarter, and level 5+ pay full price.
Resources are spent greedily in slot order, so the *first* upgradable stack gets first claim on the
treasury.

### Water Wheel (109) `0x529418`, Windmill (112) `0x52949A`, Wagon (105) `0x52B710`

```c
// Water Wheel
if (object_visited_this_week(objCell, week) /*0x529690*/)
    return ftol((double)(int16)((objCell->b[0] & 0x1F) * 500) * pd->resource_value[6]);
return ftol(pd->resource_value[6] * 1000.0);                       // 0x640590

// Windmill
if (object_visited_this_week(objCell, week) && ((objCell->d[0] >> 13) & 0xF) == 0) return 0;
return gpCurPlayer->d[0x160] /*average non-gold resource value*/ * 9 / 2;

// Wagon
if (owner in [0,8) && ((objCell->d[0] >> 5) & (1 << owner))) return 0;   // already searched
int a = playerData[owner].d[0x160] * 7 / 4;
return ftol(gpCurPlayer->f[0x164] /*average artifact value*/ * 2.0f / 5.0f + (float)a);
```

The Water Wheel's base gold is **`(objCell->b[0] & 0x1F) × 500`** — the low five bits of the cell word
carry the accumulated weeks. The Wagon mixes a fixed fraction of the average artifact value with a
fixed fraction of the average resource value rather than reading its actual roll.

### School of War (107) `0x52B790`, Warrior's Tomb (108) `0x5293D6`, Witch Hut (113) `0x52B900`

```c
// School of War — there is NO valuation of a primary-skill point; it is priced purely as XP
if (hero->d[0x7B] & (1 << objCell->d[0])) return 0;             // this school already used
playerData *pd = hero::owner_data(hero);                        // 0x4E56B0
if (pd->gold /*+0xB4*/ < 1000) return 0;
return ftol((float)experience_for_level(hero->level) * hero->xpValue /*+0x109*/
            - pd->resource_value[6] * 1000.0);

// Warrior's Tomb — no morale-penalty term after all
if (object_visited_this_week(objCell, week)) return 0;
if (hero::artifact_count(hero, 1) >= 64)     return 0;
return ftol(gpCurPlayer->f[0x164]);                             // average artifact value

// Witch Hut
int owner = hero->b[0x22];
if (owner < 0 || owner >= 8 || !((objCell->d[0] >> 5) & (1 << owner)))
    return ftol((float)experience_for_level(hero->level) * hero->xpValue);   // unscouted
if (hero->d[0x101] >= 8) return 0;                              // already 8 secondary skills
int skill = (int8)((int)(objCell->d[0] << 12) >> 25);           // signed 7-bit field, bits 13…19
if (skill == -1 || hero->b[0xC9 + skill]) return 0;
if (!hero::can_learn_skill(hero, skill, 1)) return 0;           // 0x524DD0
return hero::AI_secondary_skill_value(hero, skill, 1);          // 0x524690 — §4.12
```

**The skill a Witch Hut teaches is in the map cell**, bits 13…19 of `objCell->d[0]`, sign-extended
so that `−1` means "no skill". The AI does not need any server roll to price it.

### Spell Scroll (93) — `0x52A8C0`

```c
if (hero::artifact_count(hero, 1) >= 64) return 0;              // 0x4D90C0, backpack full
int spell = objCell->d[0] & 0xFF;
int v = 10;
if (objCell->d[0] >> 31) {                                      // guarded
    mapItem *m = map_item_at(gpMapMgr /*0x699268*/, objCell);   // 0x49F040
    if (m->b[0x10] && armyGroup::count(&m->guards /*m+0x14*/))
        v = AI_value_of_combat(hero, 0, &m->guards, objCell) + 10;
}
if (!hero->b[0x430 + spell])
    v += AI_get_value_of_artifact(hero, 0, /*type=*/1 /*Spell Scroll*/, spell, 0);
return v;
```

### Sirens (92) — `0x52A960`

```c
armyGroup tmp = hero->army;                       // 14 dwords copied off the hero
int before = armyGroup::get_AI_value(&tmp);
int hpLost = 0;
for (int i = 0; i < 7; ++i) {
    int t = tmp.type[i];  if (t == -1) continue;
    int n = tmp.count[i]; if (n <= 1)  continue;
    int keep = (int16)ftol((float)n * 0.7);       // 0x63E688 — Sirens take 30 %
    hpLost += traits[t].d[0x4C] /*hit points*/ * (n - keep);
    tmp.count[i] = keep;
}
int xpGain   = ftol(hero::xp_reward_factor(hero) /*0x4E4840*/ * (float)hpLost);
int loss     = before - armyGroup::get_AI_value(&tmp);
int weighted = loss * (hero::get_primary_skill_sum(hero) + 40) / 40;

if (hero->f[0x109] /*xpValue*/ == 0.0f) return -weighted;       // 0x63AC64
// re-derive the xp value against the *reduced* army — a smaller army makes XP worth less
float xpv = ((float)g_xpBase + (float)armyGroup::get_AI_value(&tmp))
          / (float)(40 * experience_for_level(hero->level));
return ftol(xpv * (float)xpGain - (float)weighted);
```

### Refugee Camp (78) — `0x52A700` / `0x52A710`

```c
// 0x52A700 unpacks the creature offer out of the object and tail-calls 0x52A710
int type  = objCell->w[0x22];
int count = objCell->w[0x00];

// 0x52A710  __fastcall(hero*, int type /*edx*/) ; stack: int16 count
type_AI_army_planner plan;
plan.init(hero->owner, type, &count, 0);                         // 0x42CF50
bool bonus  = playerData::AnyHeroHasArtifact(&playerData[hero->owner], 0x81);   // 0x4BACB0
int  morale = hero::get_morale(hero, 0, 0, 1);
return plan.value_of_adding(&hero->army, morale,
                            gpCurPlayer->resources /*+0x9C*/, bonus);           // 0x42D780
```

A Refugee Camp is priced by the **army planner** (§4B.4), not by a bespoke formula — the same
`value_of_adding` that dwellings and town recruitment use.

### Campfire (12), Fountain of Fortune (30), Idol of Fortune (38)

All three read their reward straight out of the object's cell word; nothing is rolled by the server
at valuation time.

```c
// Campfire — 0x52851E
int resType = objCell->d[0] & 0x0F;
int amount  = (int16)(objCell->d[0] >> 4);
return ftol((double)(amount * 100) * pd->resource_value[6]
          + (double)amount * pd->resource_value[resType]);

// Fountain of Fortune — 0x52894F
if (hero->d[0x105] & (0x20 | 0x8000000 | 0x10000000 | 0x20000000)) return 0;
if (*limit > hero->d[0x4D]) return 0;
int bonus = 1;
int owner = hero->b[0x22];
if (owner >= 0 && owner < 8 && ((objCell->d[0] >> 5) & (1 << owner)))
    bonus = (int)(objCell->d[0] << 15) >> 28;      // signed nibble, bits 13…16
return ftol(AI_value_of_luck(hero::get_luck(hero, 0, 0, 1), bonus));

// Idol of Fortune — 0x528B13
if (hero->d[0x105] & (0x10 | 0x2000000)) return 0;
if (*limit > hero->d[0x4D]) return 0;
if (gpGame->w[0x1F63E] == 7)                       // last day of the week: BOTH
    return ftol(AI_value_of_morale(hero::get_morale(hero,0,0,1), +1)
              + AI_value_of_luck  (hero::get_luck  (hero,0,0,1), +1));
if (hero->d[0x105] & 1) return ftol(AI_value_of_luck  (hero::get_luck  (hero,0,0,1), +1));
return                         ftol(AI_value_of_morale(hero::get_morale(hero,0,0,1), +1));
```

**Fountain of Fortune and Idol of Fortune are worth zero to the shipped AI.** `AI_value_of_luck` and
`AI_value_of_morale` return a *fraction of army value* (§4.9); every other consumer multiplies by
`armyGroup::get_AI_value` before truncating, but these three sites — plus the luck/morale tail of
Rally Flag — pass the bare fraction to `ftol`. `ftol(0.0173)` is `0`. Reproduce it as written; the
AI genuinely never detours for either object.


## 4.9 Valuation primitives

### Experience → value (`hero::AI_update_valuations` @ `0x527770`)

```c
hero->xpValue /*float @+0x109*/ =
      (2500.0f + (float)armyGroup::get_AI_value(&hero->army))
    / (float)(40 * experience_for_level(hero->level));
```

This single float is what turns every XP-granting object (Arena, Learning Stone, Scholar, Tree of
Knowledge, Mercenary Camp, Marletto Tower, Pandora, Sirens…) into a comparable number. It rises with
army strength and falls as the hero levels — which is why high-level AI heroes stop detouring for
experience.

The same function computes, using `type_spellvalue::get_best_spell_value` at
current / +1 stats:

* `hero+0x47E` = value of **+1 spell power** (floored at 10)
* `hero+0x482` = value of **+1 spell duration** (the `type_duration_artifact` consumer — see §4.9b for the probe that proves it)
* `hero+0x486` = value of **+1 knowledge** (floored at 10)
* `hero+0x48E` / `hero+0x48A` = value of a full / double mana refill

The exact probe sequence is in §4.9b.

`experience_for_level` @ `0x4DA420`: table of `int16` at `0x679C86` for levels ≤ 12, then
geometric with ratio `1.2` (`double @0x63AC20`).

### `armyGroup::get_AI_value` @ `0x44AC80`

```c
int v = 0;
for (int i = 0; i < 7; ++i)
    if (types[i] != -1) v += traits[types[i]].AI_value /*+0x40*/ * counts[i];
return v;
```

`TCreatureTypeTraits` stride is **116 (0x74)**, `AI_value` at `+0x40`, `fight_value` at `+0x3C`.
The array is at `0x6703B8` but is **loaded at runtime from `CRTRAITS.TXT`** — it is all zeros in the
image, so do not read it statically.

### Luck & morale — `value_of_luck_and_morale` @ `0x435850`

`__fastcall(int current /*ecx*/, int change /*edx*/, double good, double bad) → double`

```c
if (change > 0) {
    if (current >= 3) return 0.0;
    if (current + change > 3) change = 3 - current;
    if (current >= 0)               return change * good;
    if (current + change <= 0)      return -(change * bad);
    return (current + change) * good + current * bad;
} else {
    if (current <= -3) return 0.0;
    if (current + change < -3) change = -3 - current;
    if (current <= 0)               return -(change * bad);
    if (-change <= current)         return change * good;
    return -(current * good) - (current + change) * bad;
}
```

Wrappers: `AI_value_of_morale(m, d)` @ `0x435830` uses `good = 0.0173`, `bad = −0.0833`;
`AI_value_of_luck(l, d)` @ `0x435960` uses `good = 0.0173`, `bad = −0.0122`.

Interpretation: **one point of positive morale or luck is worth 1.73 % of your army's value; one point
of *negative* morale costs 8.33 %** (negative luck only 1.22 %). That asymmetry is why the AI is so
eager to fix bad morale and comparatively indifferent to bad luck.

Object handlers convert the fraction into an absolute number with

```
value = value_of_morale(...) * armyGroup::get_AI_value(&hero->army) * (AI_priority(hero) + 40) / 40
```

### `hero::get_morale` @ `0x4E39B0` and `hero::get_luck` @ `0x4E36C0`

Every morale and luck valuation in this report calls one of these two, and both were previously left
as bare addresses. `__thiscall(hero*, int, bool suppressed, bool clamp)` — `ret 0xC`. Call sites pass
the three stack arguments right-to-left, so `get_morale(h, 0, 0, 1)` means `suppressed = 0`,
`clamp = 1`.

```c
int hero::get_morale(hero *h, int, bool suppressed, bool clamp)
{
    if (suppressed) return 0;

    int m = g_leadership_bonus[h->b[0xCF] /*Leadership = 0xC9+6*/];      // int32[4] @0x63E9A8 = {0,1,2,3}
    if (h->b[0xCF] > 0 && g_heroSpecialty[h->d[0x1A]].type  == 0
                       && g_heroSpecialty[h->d[0x1A]].param == 6)        // a Leadership specialist
        m = ftol((h->level * 0.05f + 1.0f) * (float)m);                  // 0x63EAE4, 0x63B6E0

    if (has_artifact_or_its_set(h, 108 /*Pendant of Courage*/))    m += 3;
    if (has_artifact_or_its_set(h,  45 /*Still Eye of the Dragon*/)) m += 1;
    if (has_artifact_or_its_set(h,  49 /*Badge of Courage*/))      m += 1;
    if (has_artifact_or_its_set(h,  50 /*Crest of Valor*/))        m += 1;
    if (has_artifact_or_its_set(h,  51 /*Glyph of Gallantry*/))    m += 1;

    // the Grail: +2 only from a CASTLE-faction Grail (the Colossus)
    for (each town of h->owner)
        if ((*(uint64*)&town->d[0x158] & g_grailMask /*0x66CE68*/) && town->b[4] == 0)
            { m += 2; break; }

    m += (int8)h->b[0x11A];                       // accumulated terrain / event morale
    if (h->d[0x105] & 0x800000) m += 500;         // the "maximum morale" flag
    return clamp ? clamp(m, -3, +3) : m;
}
```

`get_luck` is the same shape with:

* `g_luck_bonus[h->b[0xD2] /*Luck = 0xC9+9*/]` — int32[4] at `0x63E998`, **`{0, 1, 2, 3}`**;
* artifacts **85** Hourglass of the Evil Hour, **108** Pendant of Courage, **45** Still Eye of the
  Dragon, **46** Clover of Fortune, **47** Cards of Prophecy;
* accumulator at `h->b[0x11B]`, and the "maximum luck" flag is bit **`0x400000`** of `h->d[0x105]`
  (morale uses bit `0x800000`);
* no Grail term at all;
* the clamp is the shared helper `0x4E6750` rather than inline code.

Note the `0x400000` bit is tested **before** the `suppressed` early-out in `get_luck`: a hero with
guaranteed maximum luck returns `+3` even when the caller asks for the suppressed value.

Both tables are `int32`, not float — reading `0x63E9A8` or `0x63E998` as `f32` yields denormals
(trap 13). The bonus is the plain secondary-skill level.

### `experience_for_level` @ `0x4DA420` — there is no division by zero at level 0

```c
int experience_for_level(int level)
{
    int n = level + 1;                                    // ← the +1
    if (n <= 12) return (int16)g_xpTable[n];              // int16[] @0x679C86
    int last = (int16)g_xpTable[12], prev = (int16)g_xpTable[11];
    ... geometric continuation, ratio 1.2 (double @0x63AC20) ...
}
```

The function returns the experience needed for the **next** level, so `experience_for_level(0)`
indexes entry 1 and returns a positive number. `AI_update_valuations`' divisor
`40 × experience_for_level(level)` is therefore never zero, and no guard is required.


### Resources — `AI_resource_cost` @ `0x526C70`

```c
int v = 0;
for (int i = 0; i < 7; ++i) v = (int)(res[i] * pd->resource_value[i] + (double)v);
return v;
```

`pd->resource_value[7]` (doubles at `playerData+0x128`, gold at `+0x158`) is recomputed once per turn
in `0x429D50` from: weekly income × 7, minus the cost of a full week of creature recruitment in every
owned town, adjusted by supply/demand and by what the AI's build plan needs. Scarce resources get a
high value, so a Sulfur mine can outrank a gold mine for a Dungeon AI.

### Artifacts and spells

Both are big enough to have their own sections: **§4.9a** (artifacts) and **§4.9b** (spells) below.

## 4.9a Artifact valuation, in full

This is the single most load-bearing valuation in the adventure AI: it prices artifact objects,
Black Market, Wagon, Warrior's Tomb, War Machine Factory, Sea Chest, Treasure Chest and — through
`hero::total_artifact_value` — every tavern hero the AI considers buying.

### The three entry points

```c
// 0x433AA0  __fastcall(this = {int artifactType; int arg2;} /*ecx*/, int player /*edx*/)
int AI_get_value_of_artifact_for_player(pair *a, int player)
{
    if (a->type == -1) return 0;
    playerData *pd = &gpGame->d[0x20AD0 + player * 0x168];
    int best = 10;                                   // floor
    for (int i = 0; i < pd->heroCount /*+0x01*/; ++i) {
        hero *h = &gpGame->heroes[pd->heroIds[i]];   // gpGame + 0x21620, stride 1170
        int v = hero::total_artifact_value(h, /*dl=*/0, a->type, a->arg2);
        if (v > best) best = v;
    }
    return best;
}
```

So an artifact lying on the map is worth **what the owner's *best-placed* hero would gain from it**,
never less than 10.

```c
// 0x4339E0  __fastcall(hero* /*ecx*/, char exact /*dl*/) ; ret 8
//   stack: int artifactType, int arg2
int hero::total_artifact_value(hero *h, char exact, int type, int arg2)
{
    int slot = 0;
    while (slot < 19 && !hero::can_equip(h, type, slot)) ++slot;   // 0x4E2550

    int gain = AI_get_value_of_artifact(h, /*edx=*/exact, type, arg2, /*arg3=*/0);
    if (gain < 0) gain = 0;
    if (slot >= 19) return gain;            // no free slot at all → backpack value only

    // a slot exists → the artifact currently there must be given up
    int loss = 0;
    for (int s = 0; s < 19; ++s) {
        if (!hero::slot_is_occupied(h, type, s))  continue;        // 0x4E2840
        artifact *cur = &h->equipped[s];                           // h + 0x12D, stride 8
        loss = AI_get_value_of_artifact(h, /*edx=*/1, cur->type, cur->arg2, /*arg3=*/0);
    }
    int net = gain - loss;
    return (net > 0) ? net : 0;
}
```

Note the loop writes `loss` unconditionally rather than accumulating or maximising — the **last**
occupied slot wins. That is the shipped behaviour, not a transcription slip.

### `AI_get_value_of_artifact` @ `0x4336C0`

`__fastcall(hero* /*ecx*/, char mode /*dl*/)`, `ret 0xC` — stack args
`int type`, `int param`, `int flag`.

`mode` (the `dl` byte) means *"evaluate as if already equipped"*: when set, spell-granting effects
skip the "hero already has this from another artifact" test, and morale/luck effects subtract the
artifact's own contribution before measuring. `flag` (the third stack arg) means *"cheap estimate"*:
several effect classes return 0 rather than doing real work when it is set.

```c
int AI_get_value_of_artifact(hero *h, char mode, int type, int param, int flag)
{
    if (type == -1) return 0;
    switch (type) {
      case 1: return spell_scroll_arm(...);      // 0x4336F9  Spell Scroll
      case 4: return ballista_arm(...);          // 0x43373E  Ballista
      case 5: return ammo_cart_arm(...);         // 0x4337D4  Ammo Cart
      case 6: return first_aid_tent_arm(...);    // 0x433837  First Aid Tent
      case 2: case 3: break;                     // Grail, Catapult → the generic path
      default: break;
    }

    int v = 0;
    // victory-condition override, §4C
    if ((gpGame->b[0x1F89C] == 0 || gpGame->b[0x1F89C] == 10)
        && gpGame->d[0x1F8A0] == type)
        v = 5000000;

    for (type_artifact_effect *e : g_artifactEffects[type])       // 0x692E18, 144 × 16 bytes
        v += e->vft[1](h, mode, flag);

    // combination artifacts: an artifact that is part of a set also carries the set's effects
    int combo = g_artifactTraits[type].d[0x14];                   // *(void**)0x660B68, stride 32
    if (combo == -1) return v;
    uint32_t *members = (uint32_t*)((char*)*(void**)0x660B6C + combo * 24 + 4);  // bitset over 144
    for (int a = 0; a < 144; ++a)
        if (members[a >> 5] & (1u << (a & 31)))
            for (type_artifact_effect *e : g_artifactEffects[a])
                v += e->vft[1](h, mode, flag);
    return v;
}
```

`g_artifactEffects` is `std::vector<type_artifact_effect*> [144]` at **`0x692E18`**, 16 bytes per
entry (`{allocator, _First, _Last, _End}`), default-constructed by the static initialiser at
`0x432460`. It is **runtime-filled** — see the next subsection.

### The four hard-coded artifact arms

**Spell Scroll (type 1)** @ `0x4336F9` — `param` is the spell id.

```c
if (h->b[0x3EA + spell]) return 0;                 // already knows it permanently
if (!mode && h->b[0x430 + spell]) return 0;        // already granted by something else
return AI_get_spell_value(h, spell);               // 0x527640, §4.9b
```

`hero + 0x3EA` and `hero + 0x430` are two 70-byte arrays: *known* spells and *available* spells.

**Ballista (type 4)** @ `0x43373E`

```c
int atk  = clamp(h->b[0x476], 0, 99) + 1;          // attack
int base = (int)(sqrt((double)atk) * 500.0);       // 0x63AC30 = 500.0
base += base * (int8)h->b[0xDD] / 2;               // 0xC9+20 = Artillery: ×1, 1.5, 2, 2.5

int army   = armyGroup::get_AI_value(&h->army);
int scaled = army * (hero::get_primary_skill_sum(h) + 40) / 40;    // 0x4E5960

return (scaled * base) / (scaled + base);          // harmonic combination
```

`hero::get_primary_skill_sum` @ `0x4E5960` sums the four primary skills at `hero+0x476`, each clamped
to ≤ 99, and — for spell power and knowledge only — counts a zero as 1. This is the "plus
`get_primary_skill_sum` weighting" term that `AI_hire_hero` also uses.

**Ammo Cart (type 5)** @ `0x4337D4`

```c
int v = 0;
for (int i = 0; i < 7; ++i) {
    int t = h->army.type[i];
    if (t == -1 || !(traits[t].flags /*+0x10*/ & 4)) continue;     // shooters only
    v += traits[t].AI_value /*+0x40*/ * h->army.count[i] / 40;
}
return v;
```

**First Aid Tent (type 6)** @ `0x433837`

```c
int heal = (int)(hero::first_aid_amount(h) * 25.0f);               // 0x4E4920, 0x63B76C = 25.0f
int best = 0;
for (int i = 0; i < 7; ++i) {
    int t = h->army.type[i];  if (t == -1) continue;
    int hp = traits[t].d[0x4C];
    int cand = (heal >= hp) ? traits[t].AI_value
                            : traits[t].AI_value * heal / hp;
    if (cand > best) best = cand;
}
return best;
```

`hero::first_aid_amount` @ `0x4E4920` = `g_firstaid_factor[skillLevel]` (floats at `0x63EA98`),
scaled by `1 + 0.05 × hero->level` (`0x63EAE4 = 0.05f`) when the hero is standing on a specific
object type, then `+1.0f`.

### The 24 effect classes

Each effect object is `{void **vft; int magnitude; int aux;}` (8 or 12 bytes). `vft[0]` is the scalar
deleting destructor (`0x433080` for every derived class); `vft[1]` is the evaluator, called as
`(hero*, char mode, int flag)`.

Throughout: `army` = `armyGroup::get_AI_value(&h->army)` (§4.9); `mode` and `flag` are as above.

| # | class (NH3API name) | vft | evaluator | value |
|---|---|---|---|---|
| 0 | `type_might_artifact` | `0x63B6C8` | `0x4325A0` | `flag ? 0 : army × mag / 40` |
| 1 | `type_power_artifact` | `0x63B6D0` | `0x4325E0` | `flag ? 0 : h->d[0x47E] × mag` |
| 2 | `type_knowledge_artifact` | `0x63B6D8` | `0x432610` | `flag ? 0 : h->d[0x486] × mag` |
| 3 | `type_morale_artifact` | `0x63B6FC` | `0x432780` | see below |
| 4 | `type_luck_artifact` | `0x63B704` | `0x4327F0` | see below |
| 5 | `type_scouting_artifact` | `0x63B6B8` | `0x432510` | `h->maxMovement (+0x49) × mag / 100` |
| 6 | `type_necromancy_artifact` | `0x63B6E4` | `0x432640` | see below |
| 7 | `type_combat_artifact` | `0x63B6C0` | `0x432560` | `army × mag / 100` |
| 8 | `type_movement_artifact` | `0x63B6EC` | `0x4326E0` | `(army + 2500) × mag / 100` |
| 9 | `type_spellcaster_artifact` | `0x63B6F4` | `0x432720` | `0` unless `h->d[0x47E] != 0` **and** Wisdom `h->b[0xD0] != 0`; then `army × mag / 100` |
| 10 | `type_duration_artifact` | `0x63B70C` | `0x432860` | `flag ? 0 : h->d[0x482] × mag` |
| 11 | `type_school_artifact` | `0x63B714` | `0x432890` | see below |
| 12 | `type_tome_artifact` | `0x63B734` | `0x432C20` | see below |
| 13 | `type_antimagic_artifact` | `0x63B71C` | `0x432A50` | see below |
| 14 | `type_antimorale_artifact` | `0x63B724` | `0x432B20` | see below |
| 15 | `type_antiluck_artifact` | `0x63B72C` | `0x432BA0` | see below |
| 16 | `type_income_artifact` | `0x63B73C` | `0x432D20` | `ftol(mag × AI_player[h->owner].resource_value[aux] × 3.0)` |
| 17 | `type_creature_growth_artifact` | `0x63B744` | `0x432D70` | see below |
| 18 | `type_spell_artifact` | `0x63B74C` | `0x432F90` | see below |
| 19 | `type_shooter_bonus_artifact` | `0x63B754` | `0x4330B0` | `Σ shooter stacks (AI_value × count) × mag / 100` |
| 20 | `type_angelic_alliance_artifact` | `0x63B75C` | `0x433130` | see below |
| 21 | `type_undead_king_cloak_artifact` | `0x63B764` | `0x4333A0` | see below |
| 22 | `type_elixir_of_life_artifact` | `0x63B778` | `0x433520` | `Σ stacks with flag 0x10 (AI_value × count) / 8` |
| 23 | `type_statue_of_legion_artifact` | `0x63B770` | `0x433580` | scans the owner's towns for the five Legion parts (`0x640558 = {118,119,120,121,122}`) |

`0x63B6B0` is the abstract base vftable (`{0x4324D0 scalar-deleting-dtor, _purecall}`).

**Morale / luck** (`0x432780`, `0x4327F0`)

```c
if (flag) return 0;
int cur = hero::get_morale(h, 0, 0, 0);        // 0x4E39B0   (luck: 0x4E36C0)
if (mode) cur -= mag;                          // undo our own contribution
double f = AI_value_of_morale(cur, mag);       // 0x435830   (luck: 0x435960), §4.9
return ftol(f * (double)army);
```

**Anti-morale / anti-luck** (`0x432B20`, `0x432BA0`) — Spirit of Oppression, Hourglass of the Evil
Hour. These are worth what *zeroing* the bonus is worth:

```c
int v = ftol(AI_value_of_morale(0, 2) * (double)army);      // the enemy's typical +2 removed
if (!flag) {
    int m = hero::get_morale(h, 0, 0, 1);
    if (m > 0) v = ftol(AI_value_of_morale(m, -m) * (double)army + (double)v);
}
return v;                                                   // note: our own loss is ADDED, not subtracted
```

**Necromancy** (`0x432640`) and **Cloak of the Undead King** (`0x4333A0`)

```c
int headroom = ftol((1.0f - hero::necromancy_fraction(h, 0)) * 100.0f);   // 0x4E3CD0, 0x63AC68 = 100.0f
if (mode) { if (headroom > 0) headroom = 0; headroom += mag; }
else      { headroom = max(headroom, mag); }
if (headroom <= 0) return 0;
return army * headroom / 250;
```

The Cloak (`mag = 30`) takes the same path when the hero has **no** Necromancy. With Necromancy at
level 1/2/3 it instead prices the *upgrade* of the raised creature — Walking Dead (58), Wight (60),
Lich (64):

```c
int  skel  = traits[56].AI_value;                       // *(int*)(traitsBase + 0x19A0)
double r   = (double)(traits[creature].AI_value - skel) / (double)skel;
int  necro = <the same headroom computation as above>;  // army × headroom / 250
return ftol(r * (double)necro);
```

**School artifact** (`0x432890`) — the Orbs. `mag` is a percentage boost to spell power, `aux` is the
school mask (**1 = Air, 2 = Fire, 4 = Water, 8 = Earth**).

```c
if (flag) return 0;
type_spellvalue ctx(h, 0);                              // 0x526D40, §4.9b
if (ctx.spellPower <= 0) return 0;
int sp = clamp(h->b[0x478], 1, 99);
int lo, hi;
if (mode) { lo = 100 * sp / (mag + 100);  hi = sp; }    // strip our own boost off
else      { lo = sp;                      hi = sp * (mag + 100) / 100; }

int acc = 0;
for (int s = 0; s < 70; ++s) {
    if (!h->b[0x430 + s])                       continue;   // spell not available
    if (!(spellTraits[s].d[0x1C] & aux))        continue;   // wrong school
    if (!(spellTraits[s].d[0x0C] & 0x200))      continue;   // not power-scaled
    ctx.spellPower = lo; int a = ctx.value_of_spell(s);     // 0x5273D0
    ctx.spellPower = hi; int b = ctx.value_of_spell(s);
    int delta = b - a;
    acc = (mag < 0) ? min(acc, delta) : max(acc, delta);
}
return acc;
```

**Tome artifact** (`0x432C20`) — `aux` is the same school mask; the artifact grants *every* spell of
that school, so it is worth the best one the hero does not already have.

```c
if (flag) return 0;
type_spellvalue ctx(h, 0);
if (ctx.spellPower <= 0) return 0;
int best = 0;
for (int s = 0; s < 70; ++s) {
    if (h->b[0x3EA + s])                 continue;          // already known
    if (!mode && h->b[0x430 + s])        continue;          // already available
    if (!(spellTraits[s].d[0x1C] & aux)) continue;
    best = max(best, ctx.value_of_spell(s));
}
return best;
```

**Spell artifact** (`0x432F90`) — Titan's Thunder. `mag` is the spell id.

```c
if (flag) return 0;
if (h->b[0x3EA + mag])            return 0;
if (!mode && h->b[0x430 + mag])   return 0;
type_spellvalue ctx(h, 0);
if (ctx.spellPower <= 0) return 0;
return ctx.value_of_spell(mag);
```

**Anti-magic artifact** (`0x432A50`) — Recanter's Cloak (`mag = 3`), Orb of Inhibition (`mag = 0`).

```c
int v = (mag == 0) ? army / 5 : army / 8;
if (flag || !mode) return v;
int sp = clamp(h->b[0x478], 1, 99);
return v - sp * ((mag == 0) ? 50 : 25);        // it silences us too
```

**Creature growth** (`0x432D70`) — the Legion parts and the Grail. `mag` is a dwelling index, `aux`
the extra creatures per week.

```c
uint64_t need    = g_dwellingMask[mag];        // 0x66CE88, 8 bytes per entry
uint64_t needUpg = g_dwellingMaskUpg[mag];     // 0x66CEC0

int value_for_town(town *t) {
    uint64_t built = *(uint64_t*)&t->d[0x158];
    if (!(built & need))                  return 0;
    if (t->owner /*+0x0C*/ != h->d[0x1A]) return 1;          // someone else's town → token 1
    int slot = mag + ((built & needUpg) ? 7 : 0);
    int cre  = g_gDwellingType[t->faction /*+0x04*/ * 14 + slot];   // 0x6747B4
    return traits[cre].AI_value * aux;
}

if (flag) {                                    // "the town I am standing in"
    int idx = map_town_at(h->x, h->y, h->z);   // 0x4BB870
    if (idx < 0) return 0;
    return value_for_town(&gpGame->towns[idx]);
}
int best = 0;                                  // otherwise: my best town
playerData *pd = &gpGame->d[0x20AD0 + h->owner * 0x168];
for (int i = 0; i < pd->townCount; ++i)
    best = max(best, value_for_town(&gpGame->towns[pd->townIds[i]]));
return best;
```

**Angelic Alliance** (`0x433130`, `mag = 8`) — worth the troops whose alignment penalty it removes.

```c
uint32_t held[2] = { *0x44A460(), 0 };         // alignment bitset accumulator
int mixed = 0;
for (each hero of h->owner, then each town of h->owner)
    for (int i = 0; i < 7; ++i) {
        int t = slot type; if (t == -1) continue;
        if (gpGame->d[0x1F698] == 0 && (t >= 0x70 && t <= 0x73)) continue;  // base elementals
        int al = traits[t].alignment /*+0x00*/;  if (al == -1) continue;
        if (al >= 9) 0x434AD0(&held);           // grow the bitset
        if (!(held[al >> 5] & (1u << (al & 31)))) continue;
        mixed += traits[t].AI_value * count;
    }
int base = flag ? 0 : army * mag / 40;
return base + mixed * 5 / 100;
```

`gpGame->d[0x1F698]` — the same word the army planner reads — is what decides whether the four base
elementals (types `0x70…0x73`) count as alignment-bearing. It is 0 in a normal game.

### The artifact → effect binding table

The `g_artifactEffects` vectors are built once at start-up by the loader at **`0x4340E0`** (called
from `0x4ED650`), driven by an int32 stream at **`0x63AC7C…0x63B66C`**:

```
stream := { artifactId, { classId, params… }*, −1 }*  , terminated by a negative artifactId
```

`classId` indexes the jump table at `0x434530` (24 entries, the class list above). Parameter counts:
classes 11 (`aux`, `mag`), 16 and 17 (`mag`, `aux`) take two ints; classes 14, 15, 20, 21, 22, 23 take
none; every other class takes one (`mag`). Class 12 stores its single int into `aux` and leaves
`mag = 0`.

Decoded, with names from `ARTRAITS.TXT` (row order = artifact id, header rows skipped):

| id | artifact | effects |
|---|---|---|
| 2 | The Grail | income(5000, 6); creature_growth(0, 5); creature_growth(1, 4); creature_growth(2, 3); creature_growth(3, 2); creature_growth(4, 1); creature_growth(5, 1); creature_growth(6, 1) |
| 7 | Centaurs Axe | might(1) |
| 8 | Blackshard of the Dead Knight | might(2) |
| 9 | Greater Gnoll's Flail | might(3) |
| 10 | Ogre's Club of Havoc | might(4) |
| 11 | Sword of Hellfire | might(5) |
| 12 | Titan's Gladius | might(9) |
| 13 | Shield of the Dwarven Lords | might(1) |
| 14 | Shield of the Yawning Dead | might(2) |
| 15 | Buckler of the Gnoll King | might(3) |
| 16 | Targ of the Rampaging Ogre | might(4) |
| 17 | Shield of the Damned | might(5) |
| 18 | Sentinel's Shield | might(9) |
| 19 | Helm of the Alabaster Unicorn | knowledge(1) |
| 20 | Skull Helmet | knowledge(2) |
| 21 | Helm of Chaos | knowledge(3) |
| 22 | Crown of the Supreme Magi | knowledge(4) |
| 23 | Hellstorm Helmet | knowledge(5) |
| 24 | Thunder Helmet | knowledge(12); power(-3) |
| 25 | Breastplate of Petrified Wood | power(1) |
| 26 | Rib Cage | power(2) |
| 27 | Scales of the Greater Basilisk | power(3) |
| 28 | Tunic of the Cyclops King | power(4) |
| 29 | Breastplate of Brimstone | power(5) |
| 30 | Titan's Cuirass | power(12); knowledge(-3) |
| 31 | Armor of Wonder | might(2); power(1); knowledge(1) |
| 32 | Sandals of the Saint | might(4); power(2); knowledge(2) |
| 33 | Celestial Necklace of Bliss | might(6); power(3); knowledge(3) |
| 34 | Lion's Shield of Courage | might(8); power(4); knowledge(4) |
| 35 | Sword of Judgement | might(10); power(5); knowledge(5) |
| 36 | Helm of Heavenly Enlightenment | might(12); power(6); knowledge(6) |
| 37 | Quiet Eye of the Dragon | might(2) |
| 38 | Red Dragon Flame Tongue | might(4) |
| 39 | Dragon Scale Shield | might(6) |
| 40 | Dragon Scale Armor | might(8) |
| 41 | Dragonbone Greaves | power(1); knowledge(1) |
| 42 | Dragon Wing Tabard | power(2); knowledge(2) |
| 43 | Necklace of Dragonteeth | power(3); knowledge(3) |
| 44 | Crown of Dragontooth | power(4); knowledge(4) |
| 45 | Still Eye of the Dragon | luck(1); morale(1) |
| 46 | Clover of Fortune | luck(1) |
| 47 | Cards of Prophecy | luck(1) |
| 48 | Ladybird of Luck | luck(1) |
| 49 | Badge of Courage | morale(1) |
| 50 | Crest of Valor | morale(1) |
| 51 | Glyph of Gallantry | morale(1) |
| 52 | Speculum | scouting(1) |
| 53 | Spyglass | scouting(1) |
| 54 | Amulet of the Undertaker | necromancy(5) |
| 55 | Vampire's Cowl | necromancy(10) |
| 56 | Dead Man's Boots | necromancy(15) |
| 57 | Garniture of Interference | combat(1) |
| 58 | Surcoat of Counterpoise | combat(3) |
| 59 | Boots of Polarity | combat(5) |
| 60 | Bow of Elven Cherrywood | shooter_bonus(2) |
| 61 | Bowstring of the Unicorn's Mane | shooter_bonus(5) |
| 62 | Angel Feather Arrows | shooter_bonus(7) |
| 63 | Bird of Perception | combat(1) |
| 64 | Stoic Watchman | combat(2) |
| 65 | Emblem of Cognizance | combat(3) |
| 66 | Statesman's Medal | combat(1) |
| 67 | Diplomat's Ring | combat(2) |
| 68 | Ambassador's Sash | combat(3) |
| 69 | Ring of the Wayfarer | combat(5) |
| 70 | Equestrian's Gloves | movement(12) |
| 71 | Necklace of Ocean Guidance | movement(25) |
| 72 | Angel Wings | movement(50) |
| 73 | Charm of Mana | spellcaster(2) |
| 74 | Talisman of Mana | spellcaster(4) |
| 75 | Mystic Orb of Mana | spellcaster(6) |
| 76 | Collar of Conjuring | duration(1) |
| 77 | Ring of Conjuring | duration(2) |
| 78 | Cape of Conjuring | duration(3) |
| 79 | Orb of the Firmament | school(1, 100) |
| 80 | Orb of Silt | school(8, 100) |
| 81 | Orb of Tempestuous Fire | school(2, 100) |
| 82 | Orb of Driving Rain | school(4, 100) |
| 83 | Recanter's Cloak | antimagic(3) |
| 84 | Spirit of Oppression | antimorale |
| 85 | Hourglass of the Evil Hour | antiluck |
| 86 | Tome of Fire Magic | tome(2) |
| 87 | Tome of Air Magic | tome(1) |
| 88 | Tome of Water Magic | tome(4) |
| 89 | Tome of Earth Magic | tome(8) |
| 90 | Boots of Levitation | combat(5) |
| 91 | Golden Bow | shooter_bonus(5) |
| 92 | Sphere of Permanence | combat(2) |
| 93 | Orb of Vulnerability | combat(10) |
| 94 | Ring of Vitality | combat(1) |
| 95 | Ring of Life | combat(1) |
| 96 | Vial of Lifeblood | combat(2) |
| 97 | Necklace of Swiftness | combat(5) |
| 98 | Boots of Speed | movement(25) |
| 99 | Cape of Velocity | combat(10) |
| 100 | Pendant of Dispassion | combat(1) |
| 101 | Pendant of Second Sight | combat(10) |
| 102 | Pendant of Holiness | combat(1) |
| 103 | Pendant of Life | combat(5) |
| 104 | Pendant of Death | combat(5) |
| 105 | Pendant of Free Will | combat(1) |
| 106 | Pendant of Negativity | combat(10) |
| 107 | Pendant of Total Recall | combat(1) |
| 108 | Pendant of Courage | combat(10) |
| 109 | Everflowing Crystal Cloak | income(1, 4) |
| 110 | Ring of Infinite Gems | income(1, 5) |
| 111 | Everpouring Vial of Mercury | income(1, 1) |
| 112 | Inexhaustible Cart of Ore | income(1, 2) |
| 113 | Eversmoking Ring of Sulfur | income(1, 3) |
| 114 | Inexhaustible Cart of Lumber | income(1, 0) |
| 115 | Endless Sack of Gold | income(1000, 6) |
| 116 | Endless Bag of Gold | income(750, 6) |
| 117 | Endless Purse of Gold | income(500, 6) |
| 118 | Legs of Legion | creature_growth(1, 5) |
| 119 | Loins of Legion | creature_growth(2, 4) |
| 120 | Torso of Legion | creature_growth(3, 3) |
| 121 | Arms of Legion | creature_growth(4, 2) |
| 122 | Head of Legion | creature_growth(5, 1) |
| 123 | Sea Captain's Hat | movement(5) |
| 124 | Spellbinder's Hat | spellcaster(20) |
| 125 | Shackles of War | combat(5) |
| 126 | Orb of Inhibition | antimagic(0) |
| 127 | Vial of Dragon Blood | might(10) |
| 128 | Armageddon's Blade | might(6); power(3); knowledge(6) |
| 129 | Angelic Alliance | angelic_alliance |
| 130 | Cloak of the Undead King | undead_king_cloak |
| 131 | Elixir of Life | elixir_of_life |
| 132 | Armor of the Damned | combat(15); might(6); shooter_bonus(10) |
| 133 | Statue of Legion | statue_of_legion |
| 134 | Power of the Dragon Father | might(12); power(6); knowledge(6); combat(10) |
| 135 | Titan's Thunder | spell(57) |
| 136 | Admiral's Hat | movement(12) |
| 137 | Bow of the Sharpshooter | shooter_bonus(7) |
| 138 | Wizard's Well | spellcaster(10) |
| 139 | Ring of the Magi | duration(50) |
| 140 | Cornucopia | income(4, 4); income(4, 5); income(4, 1); income(4, 3) |

Nine artifact ids (0 Spell Book, 1 Spell Scroll, 3 Catapult, 4 Ballista, 5 Ammo Cart,
6 First Aid Tent, plus 141–143) register no effects at all: the first six are handled by the
hard-coded arms above, and the last three are the Cataclysm/Vial placeholders that never reach the
AI.

**Cross-checks on the decode.** *Titan's Thunder* → `spell(57)`, and spell 57 is Titan's Lightning
Bolt. *Everflowing Crystal Cloak* → `income(1, 4)` and *Ring of Infinite Gems* → `income(1, 5)`,
matching resource ids 4 = crystal, 5 = gems. *Tome of Fire Magic* → `tome(2)` and *Orb of
Tempestuous Fire* → `school(2, 100)`, fixing the school mask as 1 = Air, 2 = Fire, 4 = Water,
8 = Earth. *The Grail* → `income(5000, 6 /*gold*/)` plus creature growth on all seven dwelling
levels. Every one of those is independently checkable against the shipped `ARTRAITS.TXT`, so the
stream format is not a guess.

**Consequence for the AI's taste in artifacts.** Because `type_combat_artifact` is `army × mag / 100`
and `type_might_artifact` is `army × mag / 40`, both scale linearly with the army the hero is already
carrying — a rich hero pays far more for the same artifact than a poor one. The stat artifacts
(`power`, `knowledge`, `duration`) instead scale with the hero's own spell valuations, which is why a
might hero and a magic hero bid very differently on the same Black Market.

## 4.9b The spell valuer — `type_spellvalue`

Everything that prices a spell — Shrines, Pandora spells, the Mage Guild half of
`AI_town_visit_value`, the Tomes and Orbs, Spell Scrolls, and the four magic-school arms of the
level-up chooser — goes through this one class.

### Layout and construction — `0x526D40`

```c
struct type_spellvalue {                 // 0x38 bytes as constructed on the stack
    hero *h;              // +0x00
    int   armyValue;      // +0x04   armyGroup::get_AI_value(&h->army)
    int   spellPower;     // +0x08   clamp(h->b[0x478], 1, 99)
    int   duration;       // +0x0C   spellPower + hero::spell_duration_bonus(h)   ; 0x4E4DB0
    int   mana;           // +0x10   ftol(hero::mana_multiplier(h) * (knowledge*10))  ; 0x4E48B0
    char  flag;           // +0x14
    struct { int type; int totalValue; int16 count; } *first, *last, *end;   // +0x18/+0x1C/+0x20
};

type_spellvalue::type_spellvalue(hero *h, char flag)
{
    this->h = h;  this->flag = flag;  this->first = this->last = this->end = 0;
    if (!hero::has_artifact(h, 0 /*Spell Book*/))   return;    // 0x4D91F0 — everything stays 0
    if ( hero::has_artifact(h, 126 /*Orb of Inhibition*/)) return;

    this->armyValue  = armyGroup::get_AI_value(&h->army);
    this->spellPower = clamp(h->b[0x478], 1, 99);
    this->duration   = this->spellPower + hero::spell_duration_bonus(h);
    int kn           = clamp(h->b[0x479], 1, 99);
    this->mana       = ftol(hero::mana_multiplier(h) * (float)(kn * 10));

    for (int i = 0; i < 7; ++i) {
        int t = h->army.type[i];  if (t == -1) continue;
        push_back({ t, traits[t].AI_value * h->army.count[i], (int16)h->army.count[i] });
    }
    std::sort(first, last);          // 12-byte records; insertion sort below 16 elements
}
```

A hero without a Spell Book — or carrying the Orb of Inhibition — gets `spellPower == 0`, and every
caller treats that as "spells are worth nothing to this hero". That single test is the gate the whole
subsystem hangs on.

### `value_of_spell(spell)` @ `0x5273D0`

```c
int type_spellvalue::value_of_spell(int spell)
{
    type_spell_traits *st = &g_spellTraits[spell];    // static array at 0x685450, stride 0x88; §4E.5
    int level = hero::get_spell_school_level(h, spell, hero::spell_mastery(h));  // 0x4E5080 / 0x4E4FA0
    int cost  = hero::get_spell_cost(h, spell, 0, -1);                           // 0x4E5240
    if (cost > this->mana) return 0;                  // cannot cast it at all
    int casts = (cost > 0) ? this->mana / cost : 1000;

    switch (st->d[0x0C] & 0x1F8000) {
      case 0x008000: return value_of_buff(spell, level, casts, this->armyValue);   // 0x5270E0
      case 0x010000: return value_of_buff(spell, level, 1,     this->armyValue);   // one cast only
      case 0x020000: return value_of_both_sides_spell(spell, level, casts);        // 0x5271C0
      case 0x040000: return value_of_mass_effect(spell, level, casts);             // 0x527290
      case 0x080000: return value_of_damage(spell, level, casts);
      case 0x100000: return value_of_utility(spell, level, casts);
      default:       return 1;
    }
}
```

`0x1F8000` is a six-bit *category* field in the spell flags word at `spellTraits + 0x0C`. It is the
only classification the AI uses; the school mask lives at `+0x1C` and the spell level at `+0x18`.

**Damage spells** (`0x080000`)

```c
int power = this->spellPower + level;
int dmg   = st->d[0x68 + level*4] * power;
double f  = (double)(dmg * 10) / (double)this->armyValue;
if (!(f <= 0.9)) f = 0.9;                              // 0x63B7E0
f *= g_castCurve[min(casts, 13)][0];                   // 0x640398, 14 rows × 4 doubles (32 b)
if (!(f <= 3.9)) f = 3.9;                              // 0x640578
return ftol((double)this->armyValue * f);
```

So a damage spell is capped at **3.9 × the caster's army value**, and the per-cast return saturates
along `g_castCurve` column 0.

**Utility spells** (`0x100000`)

```c
double r = sqrt((double)casts) * 0.001 + 0.009;        // 0x63A6A0, 0x640580
return ftol(r * (double)(st->d[0x68 + level*4] * this->armyValue));
```

**Buff / debuff spells** (`0x008000`, `0x010000`) — `value_of_buff` @ `0x5270E0`

```c
int value_of_buff(int spell, int level, int casts, int ref)
{
    int amount = st->d[0x68 + level*4] * (this->spellPower + level);
    if (spell == 57) amount = *(int*)(spellTraitsBase + 0x1E7C);   // = spells[57].effect[0] = 600, §4E.5
    int mp = hero::spell_effect_amount(h, spell, amount, 0);       // 0x4E5760
    double x = (double)(mp * 10) / (double)ref;

    int d = 0;                                     // locate x in the descending breakpoint column
    while (d < 13 && x < g_castCurve[d][1]) ++d;
    double lin = x * g_castCurve[d][2] + g_castCurve[d][3];
    double sat = x * g_castCurve[min(casts, 13)][0];
    return ftol((double)ref * min(lin, sat));
}
```

**Spells that hit both sides** (`0x020000`) — `0x5271C0`. §5B.4 proves what this category holds:
Armageddon, Death Ripple, Destroy Undead — spells whose damage lands on the caster's own army too.
The valuation is therefore "how much of my army is *immune*":

```c
int immune = 0;
for (int i = 0; i < 7; ++i) {
    int t = h->army.type[i];  if (t == -1) continue;
    double eff = armyGroup::spell_effect_fraction(spell, t, h, 0);   // 0x44A4D0 — EFFECTIVENESS
    immune = ftol((double)traits[t].AI_value * (double)h->army.count[i]
                   * (1.0 - eff)                                     // 0x63AC50 = 1.0
                + (double)immune);
}
if (immune == 0) return 0;
int net = immune - this->armyValue + value_of_buff(spell, level, casts, immune);
return (net < 0) ? 0 : net;
```

`0x44A4D0` returns **effectiveness** (`1.0` = fully affected, `0.0` = immune), so `1.0 - eff` is the
immune fraction. A caster whose whole army is immune scores `immune == armyValue` and keeps the buff
term; a caster whose army is vulnerable scores `immune ~ 0`, lands at `-armyValue + buff`, and gets
clamped to 0. **That is the entire reason an AI Warlock with Efreet or Black Dragons casts
Armageddon and one without them never does.**

**Mass-effect spells** (`0x040000`) — `0x527290`

```c
bool isMass = spell_affects_all_at_level(st, level);   // 0x59E060
int  n      = this->duration * casts;
int  limit;
if (isMass) {
    limit = (n < 7) ? 1 : min(n / 7, this->duration);
    n     = 7;
} else {
    limit = stackCount();                              // (last - first) / 12
}
int acc = 0;
for (int i = 0; i < min(stackCount(), limit); ++i) {
    if (st->d[0x00] > 0 && armyGroup::spell_effect_fraction(spell, stacks[i].type, h, 0) == 0.0)
        continue;                                      // immune
    acc += st->d[0x68 + level*4] * stacks[i].totalValue * n;
}
return acc / 700;
```

Because the stack list was sorted in the constructor, "the first `limit` stacks" is a deterministic
choice, not an arbitrary one — swapping in a different sort changes the number.

### `get_best_spell_value(schoolMask)` @ `0x5275B0`

```c
int type_spellvalue::get_best_spell_value(int mask)
{
    bool recanter = hero::has_artifact(h, 83 /*Recanter's Cloak*/);
    if (this->spellPower == 0) return 0;
    int best = 0;
    for (int s = 0; s < 70; ++s) {
        if (!h->b[0x430 + s])                   continue;      // not available to this hero
        if (!(g_spellTraits[s].d[0x0C] & mask)) continue;
        if (recanter && g_spellTraits[s].d[0x18] > 2) continue; // Cloak blocks level 3+
        best = max(best, value_of_spell(s));
    }
    return best;
}
```

### `AI_get_spell_value(hero, spell)` @ `0x527640`

The public entry — what a Shrine, a Pandora spell or a Spell Scroll is worth.

```c
int AI_get_spell_value(hero *h /*ecx*/, int spell /*edx*/)
{
    type_spellvalue ctx(h, 0);
    if (ctx.spellPower <= 0) return 0;
    int v   = ctx.value_of_spell(spell);
    int grp = g_spellTraits[spell].d[0x0C] & 0x38000;
    if (grp == 0) return v;                       // nothing else competes with it
    int best = ctx.get_best_spell_value(grp);     // the best spell of the same kind we already have
    if (best >= v) return 1;                      // strictly worse than what we have → token value
    return v - best;                              // marginal improvement only
}
```

This is why the AI is indifferent to a Shrine of Magic Incantation once it already knows a good
spell of the same category: `0x38000` covers the buff / one-shot / resurrection groups, and within a
group only the *increment* over the current best counts.

### `hero::AI_update_valuations` @ `0x527770` — where `+0x47E … +0x48E` come from

```c
type_spellvalue ctx(hero, 0);
hero->f[0x109] = ((float)g_xpBase /*0x67814C*/ + (float)ctx.armyValue)
               / (float)(40 * experience_for_level(hero->level));

int base = ctx.get_best_spell_value(0x1F8000);          // all six categories

ctx.spellPower++; ctx.duration++;
hero->d[0x47E] = max(10, ctx.get_best_spell_value(0x1F8000) - base);   // +1 SPELL POWER
ctx.spellPower--; ctx.duration--;

ctx.duration++;
hero->d[0x482] = ctx.get_best_spell_value(0x1F8000) - base;            // +1 SPELL DURATION
ctx.duration--;

ctx.mana += 30;
hero->d[0x486] = max(10, (ctx.get_best_spell_value(0x1F8000) - base) / 3);  // +1 KNOWLEDGE
ctx.mana -= 30;

int maxMana = ctx.mana;
ctx.mana = hero->w[0x18];                               // current mana
int v0   = ctx.get_best_spell_value(0x1F8000);
hero->d[0x48E] = (hero->w[0x18] >= maxMana)     ? 0
               : (ctx.mana += maxMana - hero->w[0x18],
                  ctx.get_best_spell_value(0x1F8000) - v0);            // FULL MANA REFILL
hero->d[0x48A] = (hero->w[0x18] >= 2*maxMana)   ? 0
               : (ctx.mana = hero->w[0x18] + (2*maxMana - hero->w[0x18]),
                  ctx.get_best_spell_value(0x1F8000) - v0);            // MANA TO DOUBLE MAXIMUM
```

Five cached numbers, one probe each. This corrects an earlier gloss in §2: **`+0x482` is the value of
one point of spell *duration*, not knowledge** — the `type_duration_artifact` class (Ring of the Magi,
`duration(50)`) is its only consumer, and the probe that produces it increments `duration` alone.
`+0x486` is knowledge, which is consistent with every other site that reads it (Garden of Revelation,
Library of Enlightenment, Tower's town special, Intelligence, Mysticism).

`+0x48E` and `+0x48A` are the numbers the Magic Well and Magic Spring handlers return — see §4.8.


## 4.10 Kingdom management — `type_AI_player::manage_kingdom` @ `0x428DD0`

```
1.  reserved_funds[i] -= playerData->income[i]                       (clamped at 0)
2.  0x4280E0 twice with descriptor {0x63B67C, player} and {0x63B670, player}
       → the two "kingdom goal" evaluators
3.  compute_resource_values(player)                                  ; 0x429D50
4.  while (do_one_purchase())   ;                                    ; 0x42AE00
       — one building / creature / market action per iteration, repeated
         until nothing more is worth buying
5.  0x431360  — town build-order planning
6.  0x428740  — shared "value of a purchase" helper (also used from 0x429110, 0x4297C0, 0x42AC20)
7.  for each own town: recompute the AI build flags
8.  for each other player on our team (and separately, each ally):
       0x429110  — cross-player resource trading / gifting
9.  for each resource i: if playerData->res[i] < 0
       → Log("Warning!  AI player has %i %s.\n", amount, resourceName)   (string @0x660838)
```

The purchase loop `0x42AE00` (1 824 bytes) is a straight greedy: it enumerates every possible spend
(build a structure, buy creatures, buy an artifact from a market, trade at a marketplace), scores each
with `0x428740`, takes the best positive one, and repeats.

## 4.11 Pre-combat evaluation — `AI_value_of_combat` @ `0x427330`

`__fastcall(hero* attacker /*ecx*/, hero* defender /*edx*/, armyGroup* defArmy, town* defTown, NewmapCell* cell) → int32`

This is the single most-called AI function (18 direct call sites) — it decides whether the AI attacks
anything, and it feeds the danger map.

```c
double atkMod = (double)attacker->f[0x47A];        // hero's own combat modifier
if (victoryCondition == 5 && targetHeroId == attacker->id) atkMod *= 0.5;

bool anyComputer = attacker->is_computer() || defender->is_computer();   // 0x4D9050
double defMod = 1.25;                                                   // default
if (anyComputer || (defTown && is_computer(defTown->owner)))
    defMod = *(double*)(0x6604D0 + difficulty * 8);

type_AI_combat_data A(attacker, &attacker->army, atkMod, defender, defTown, cell);   // 0x423EE0
type_AI_combat_data D(defender, defArmy,        defMod, attacker, NULL,    cell);
A.simulate_combat(D);                                                   // 0x426BC0

if (A.total_combat_value == 0) return -1'000'000'000;                   // we are wiped out

A.adjust_army(true);
int   armyVal  = gpGame->army_value(defArmy);                           // 0x4CA3B0
float f1       = attacker->AI_something();                              // 0x4E4840
int   xpReward = (int)( ((2500 + survivingArmyValue) / (40*expForNextLevel))
                        * (int)(f1 * armyVal) );                        // 0x527710
int   losses   = armyGroup::get_AI_value(&attacker->army)
               - armyGroup::get_AI_value(&A.armyAfter);
int   value    = xpReward - losses;

if (value < 0 && losses * 4 < armyBefore) value = 0;   // small losses → treat as neutral

if (defender || defTown) {
    int colour = defender ? defender->owner : defTown->owner;
    value = (int)( type_AI_player::get_attack_bonus(colour) * (float)defArmyValue
                 + (float)value );                                       // 0x428710
    if (victoryCondition == 5 && targetHeroId == defender->id && ...) value += 5'000'000;
    if (lossCondition  == 1 && targetHeroId == defender->id)          value += 5'000'000;
}
return value;
```

### The difficulty table at `0x6604D0`

| difficulty | multiplier applied to the **defender's** simulated army |
|---|---|
| 0 (Easy) | **0.5** |
| 1 (Normal) | **0.5** |
| 2 (Hard) | **1.0** |
| 3 (Expert) | **1.25** |
| 4 (Impossible) | **1.25** |

On Easy/Normal the AI simulates its opponents at **half strength** — which makes it *reckless*, not
weaker. On Expert/Impossible it inflates the enemy by 25 %, making it cautious and consequently much
better at not throwing armies away. This is the single biggest behavioural lever in the whole AI.

`get_attack_bonus(colour)` @ `0x428710` returns `0.0` for an unowned target, and `0.5`
(`0x6604F8` / `0x6604FC`) for both computer- and human-owned targets in the stock build. It is the
"killing an owned army is worth half its value on top of the XP" term — and it is exactly the hook
NH3API exposes as `set_attack_bonuses`.

### Quick combat simulation — `type_AI_combat_data` @ `0x423EE0…0x427750`

The simulator abstracts a battle into a vector of `type_monster_data` (`0x48` bytes each) bucketed by
**speed category**:

```c
category = (14 - 2*tactics_advantage + speed) / speed;   // clamped to [0..4]
if (creature is a shooter)     category = const_ranged (0);
if (behind walls && !flying)   category = max(category, wall_speed_limit);
```

`simulate_combat` (`0x426BC0`) then plays 5 abstract "rounds" (one per speed category), each round:

1. `cast_spells(defender, round)` — both sides cast their best spell (`0x425BD0`), ordered by whose
   fastest stack is faster.
2. ranged exchange (`do_ranged_combat`)
3. melee exchange by category (`choose_melee` @ `0x4267C0`, `do_general_melee` @ `0x4264D0`)
4. `inflict_damage` distributes damage down the value-sorted stack list (`0x426300` / `0x426170`)

`do_aftermath` (`0x426EE0`) applies necromancy, town-tower fire and the surviving-army bookkeeping.
`AI_quick_combat` @ `0x4270C0` and `AI_auto_combat` @ `0x427210` are the user-visible entry points
(the latter is what the "quick combat" button runs).

---

## 4.12 Level-up secondary-skill choice

Reached from `hero::LevelUp` (`0x4DA720`), which calls the dialog (`0x4F8880`) for humans and
`0x52BBD0` for the AI.

### The chooser — `hero::AI_choose_secondary_skill` @ `0x52BBD0`

`__thiscall(hero* this, int skillA /*edx*/, int skillB, bool useArmy)` → the chosen skill id, `ret 8`.

```c
bool haveA = this->b[0xC9 + skillA] > 0;
bool haveB = this->b[0xC9 + skillB] > 0;

if (haveA == haveB) {                                   // both new, or both upgrades
    int vA = AI_secondary_skill_value(this, skillA, useArmy);   // 0x524690
    int vB = AI_secondary_skill_value(this, skillB, useArmy);
    return (vA >= vB) ? skillA : skillB;                 // ties go to skillA
}

// exactly one is already known — normalise so skillA is the known one
if (!haveB) swap(skillA, skillB);
return 0x524DD0(this, skillA, useArmy) ? skillA : skillB;
```

**`hero + 0xC9` is the 28-byte secondary-skill level array** (0 = absent, 3 = expert). *This corrects
§4.8*, where `+0xC9` was glossed as per-creature-type "already have" flags — the Pandora evaluator
indexes it by skill id, not creature type.

Note the asymmetry: when the two offers are **not** in the same have/don't-have state, the values are
never compared at all. The AI runs a separate predicate (`0x524DD0`) on the already-known skill and
takes it if that passes, otherwise takes the new one.

### The evaluator — `hero::AI_secondary_skill_value` @ `0x524690`

`__thiscall(hero* this, int skill /*edx*/, bool useArmy)` → `int`, `ret 4`. 1 680 bytes, a 28-arm
switch (index table `0x524CA4`).

```c
int cur = this->b[0xC9 + skill];
if (cur == 3) return 0;                       // already expert
if (cur == 0 && this->d[0x101] == 8) return 0;   // all eight skill slots full

int army = 1000, shooters = 500;
if (useArmy) {
    for (slot = 0; slot < 7; slot++) {
        int t = this->d[0x91 + slot*4];  if (t == -1) continue;
        int v = traits[t].AI_value /*+0x40*/ * this->d[0xAD + slot*4];
        army += v;
        if (traits[t].flags /*+0x10*/ & 4) shooters += v;      // SHOOTING, §4E
    }
}
switch (skill) { … }
```

So almost every skill is priced as **a fraction of the hero's current army value**, with a floor of
1 000 (and 500 for the shooter sub-total). A hero with no army still gets a sensible ordering.

### The complete table

| # | Skill | Value | Arm |
|---|---|---|---|
| 0 | Pathfinding | `army × T[cur] / 4.0`, `T` = `0x681860` (doubles) | `0x524739` |
| 1 | Archery | `A[cur] × shooters`, `A` = `0x640380` | `0x52475F` |
| 2 | Logistics | `army / 10` | `0x524772` |
| 3 | Scouting | `army / 20` | `0x524B05` |
| 4 | Diplomacy | `army / 100` | `0x52478D` |
| 5 | Navigation | `army × 1.0 / 2.0` | `0x5247A8` |
| 6 | Leadership | `army / 50` | `0x5247CA` |
| 7 | Wisdom | `useArmy` → `clamp(+0x478,1,99) × +0x47E / 2`; otherwise `clamp(+0x478,1,99) × 25` | `0x5247E5` |
| 8 | Mysticism | from value-of-+1-knowledge `+0x486`, `/10` | `0x524861` |
| 9 | Luck | `army / 50` — **same arm as Leadership** | `0x5247CA` |
| 10 | Ballistics | `army / 8` | `0x524893` |
| 11 | Eagle Eye | **0 unless `this->b[0xD0]`** (**Wisdom**, §4.17), then from `+0x47E` | `0x5248A6` |
| 12 | Necromancy | `army / 20` — **same arm as Scouting** | `0x524B05` |
| 13 | Estates | from owner `+0x22` and table `0x64038C[cur]` | `0x5248E5` |
| 14 | Fire Magic | `0x524D20(this, 14)` — the spellbook counterfactual below | `0x524939` |
| 15 | Air Magic | `0x524D20(this, 15)` | `0x52497F` |
| 16 | Water Magic | `0x524D20(this, 16)` | `0x5249AE` |
| 17 | Earth Magic | `0x524D20(this, 17)` | `0x5249DD` |
| 18 | Scholar | **0 unless `this->b[0xD0]`** (Wisdom), then from `+0x478` × `+0x47E` | `0x524A10` |
| 19 | Tactics | `army / 50` | `0x524A96` |
| 20 | Artillery | `0` when `useArmy` and no Ballista (artifact 4); otherwise `clamp(attack +0x476, 0, 99) × 10` | `0x524AB1` |
| 21 | Learning | `army / 20` — **same arm as Scouting** | `0x524B05` |
| 22 | Offense | `army × 7 / 100` | `0x524B20` |
| 23 | Armorer | `army × 7 / 100` | `0x524B44` |
| 24 | Intelligence | knowledge `+0x479` (clamped 99) × `+0x486` | `0x524B68` |
| 25 | Sorcery | spell power `+0x478` (clamped 99) × `+0x47E` | `0x524BEA` |
| 26 | Resistance | `army / 40` | `0x524C60` |
| 27 | First Aid | `0` when `useArmy` and no First Aid Tent (artifact 6); otherwise the flat constant **250** | `0x524C7B` |

Division magics: `0x66666667`+`sar 2`→/10, +`sar 3`→/20, +`sar 4`→/40; `0x51EB851F`+`sar 4`→/50,
+`sar 5`→/100. Offense and Armorer compute `7×army` via `lea ecx,[edi*8]; sub ecx,edi` before the /100.

### What the table says about AI behaviour

* **Necromancy is not special-cased.** It shares an arm with Scouting and Learning at `army / 20`.
  The AI has no notion that Necromancy compounds.
* **Luck and Leadership are literally the same code** — the AI cannot distinguish morale from luck
  when choosing skills, despite valuing them very differently elsewhere (§4.9).
* **Offense and Armorer are the joint most valuable** at 7 % of army value — a hero with a
  10 000-value army scores them at 700, against Logistics' 1 000 and Ballistics' 1 250.
* **Ballistics outranks almost everything** (`army / 8`), which is why AI heroes so often carry it.
* Four skills return **0 outright** without a prerequisite: Eagle Eye and Scholar need **Wisdom**
  (`+0xD0`, §4.17), Artillery needs a Ballista (artifact 4), First Aid needs a Tent (artifact 6). They are
  never picked otherwise.
* The four magic schools all route through one shared helper `0x524D20` with the school id.

### The two per-level tables, resolved

Skills 1 (Archery) and 13 (Estates) index a small table by the **current** skill level (0 = absent,
1 = basic, 2 = advanced; level 3 returns 0 earlier), so the entry is the value of **gaining the next
level**. Both tables are static in the image:

| Table | Level 0→1 | 1→2 | 2→3 |
|---|---|---|---|
| Archery `0x640380` | 16 | 7 | 11 |
| Estates `0x64038C` | 725 | 725 | 1550 |

Archery's shared tail at `0x524B4D` is `imul` by `0x51EB851F` then `sar 5` — a **division by 100**:

```c
archeryValue = archeryTable[cur] * shooters / 100;
```

**Estates cross-checks the reading.** H3's Estates yields +125 / +250 / +500 gold per day, so the
*marginal* gains are 125, 125, 250 — a 1 : 1 : 2 ladder. The table is 725 : 725 : 1550, the same
1 : 1 : 2.14. These are hand-scaled marginal-value tables, not mechanical mirrors.

Archery's mechanical marginal damage is +10, +15, +25 percentage points — a 1 : 1.5 : 2.5 ladder.
The table is 16 : 7 : 11. So the AI **deliberately front-loads Archery**: acquiring it at all is
weighted roughly 3.5× what a linear reading would give, and the two upgrades are weighted at about
0.45× each. The design intent reads as *"pick up Archery early, don't bother maxing it."* The
non-monotonic shape is genuine, not a misread index.

**Depth marker:** two tables in this section, `0x681860` (Pathfinding, per level) and `0x681878`
(Navigation), read as uniform `1.0` in the image and are probably runtime-filled (trap 5). The
divisors `0x640570` = `4.0` and `0x63AC40` = `2.0` are static and confirmed. `0x524D20` and
`0x524DD0` are specified above.

### The two helpers §4.12 depends on

**`0x524D20(hero*, int school)`** — the value of a magic school (skills 14–17). It is a
**counterfactual**: raise the skill by one level, re-price the hero's whole spellbook, and take the
difference.

```c
spellbook_valuer v(&scratch, hero);           // 0x526D40
int before = v.sum(0x1F8000);                 // 0x5275B0 — sum of spell values under the
                                              //   same 6-bit category mask as §5B.4
int cur = hero->b[0xC9 + school];
hero->b[0xC9 + school] = (cur == 0) ? 3 : cur + 1;   // pretend expert if absent, else +1 level
… re-price and diff …
```

Note the asymmetry: a school the hero does **not** have is evaluated as if it jumped straight to
**expert (3)**, while one already held is evaluated at only +1 level. That biases the AI towards
picking up new magic schools over deepening existing ones.

**`0x524DD0(hero*, int skill, bool useArmy)`** — the tie-break predicate used when one offered skill
is already known and the other is not (§4.12). It ranks the skill against everything the hero's
**class** could ever learn:

```c
int val[28];
for (int s = 0; s < 28; s++) {
    if (hero->b[0xC9 + s] > 0) { val[s] = 0; continue; }        // already known ⇒ 0
    heroClass* hc = (heroClass*)[0x67DCEC] + hero->d[0x30]*64;  // hero + 0x30 = class id
    if (hc->b[0x18 + s] != 0 || s == skill)                     // class may learn it
        val[s] = AI_secondary_skill_value(hero, s, useArmy);    // 0x524690
    else
        val[s] = 0;
}
… rank `skill` among val[] …
```

New struct facts: **`hero + 0x30` is the hero-class id**, and the class table lives at `[0x67DCEC]`
with **stride 64**, carrying a 28-byte "can this class learn skill *s*" array at **`+0x18`**.


### The magic-school counterfactual — `0x524D20`

```c
int magic_school_value(hero *h /*ecx*/, int skill /*edx*/)
{
    type_spellvalue ctx(h, 0);                                 // 0x526D40, §4.9b
    int before = ctx.get_best_spell_value(0x1F8000);
    int saved  = h->b[0xC9 + skill];
    h->b[0xC9 + skill] = (saved == 0) ? 3 : saved + 1;         // ← 0 becomes EXPERT, not Basic
    int after  = ctx.get_best_spell_value(0x1F8000);
    h->b[0xC9 + skill] = saved;
    return after - before;
}
```

Two things fall out of this. First, **acquiring a magic school from scratch is priced as if it went
straight to Expert** — only *upgrades* are priced one level at a time, so the AI's appetite for a
school is front-loaded exactly like Archery's. Second, a hero with no spell book scores **0** for all
four schools, because `type_spellvalue`'s constructor bails and `get_best_spell_value` returns 0 for
`spellPower == 0` — which is why might heroes never take a magic school.

### The ranking rule — `0x524DD0`

Used by `AI_choose_secondary_skill` when exactly one of the two offers is already known, and by the
Witch Hut and Magic University handlers.

```c
bool is_skill_worth_taking(hero *h /*ecx*/, int skill /*edx*/, bool useArmy /*stack*/)
{
    int val[28], order[28];
    uint8 *cls = (uint8*)0x67DCEC + h->d[0x30] * 64;           // per hero class, 64 bytes
    for (int s = 0; s < 28; ++s) {
        order[s] = s;
        if (h->b[0xC9 + s] > 0)              { val[s] = 0; continue; }  // already known
        if (!cls[s + 0x18] && s != skill)    { val[s] = 0; continue; }  // class cannot learn it
        val[s] = AI_secondary_skill_value(h, s, useArmy);               // 0x524690
    }
    stable_sort_ascending(order, by val);                       // bubble sort, 0x524E3A…0x524E76

    int free = 8 - h->d[0x101];                                 // remaining skill slots
    for (int i = 27; i >= 0; --i) {                             // best first
        int s = order[i];
        if (h->b[0xC9 + s] != 0) continue;
        if (s == skill) return true;
        if (--free <= 0) return false;
    }
    return true;
}
```

So the predicate is **"does `skill` rank inside the hero's remaining free slots when every skill the
class can still learn is sorted by value?"** — not "is it at least as good as anything else". A hero
with seven skills already keeps only its single best remaining option; a hero with two keeps its top
six.

### What `useArmy` means at the call sites

`useArmy` does two things: it turns on the army/shooter accumulation at the top of
`AI_secondary_skill_value`, **and** it enables the four prerequisite gates. With `useArmy == false`,
Eagle Eye, Scholar, Artillery and First Aid are all valued unconditionally — the Wisdom, Ballista and
Tent tests are inside `if (useArmy)` blocks. `hero::LevelUp` passes **`true`**; the Witch Hut and
Magic University handlers pass `1` as well. There is no call site in the image that passes `false`
from AI code, so the `useArmy == false` arms are effectively dead for the adventure AI — but they are
reachable and are documented above because the same evaluator is shared.


## 4.13 Hero visits a town — `0x42BA60`

1 104 bytes, called from `0x5253D0` (the per-hero turn wrapper). This is the driver that runs when an
AI hero is standing in one of its own towns: it merges the garrison into the hero, then spends the
treasury on the town's dwellings. It is a *parameterisation* of the §4B.4 planner, not new machinery.

```c
// ecx: caller context (first word = player index), [ebp+8] = hero, [ebp+0x0C] = town
playerData* pd = &gpGame->d[0x20AD0 + playerIdx*0x168];

type_AI_army_planner p;
AI_army_planner::init_from_town(&p, pd, town);            // 0x42D1B0

hero* townHero = (town->d[0x0C] == -1) ? NULL             // town + 0x0C = visiting hero id
               : &heroArray[town->d[0x0C]];

bool bonus  = playerData::AnyHeroHasArtifact(pd, 0x81);   // 0x4BACB0
p.dst       = hero + 0x91;                                 // the real hero army, in place
p.src       = town::GetGarrison(town);                     // 0x5C1460
p.mode      = bonus;
p.morale    = hero::get_morale(hero, 0, 0, 1);             // 0x4E39B0

int diff = get_primary_skill_sum(hero);                    // 0x4E5960
if (townHero) diff -= get_primary_skill_sum(townHero);
p.skillDiff = max(diff, 0);

merge_duplicate_stacks(p.dst);                             // 0x42D870
normalise(&p);                                             // 0x42C5B0
while (take_best_stack(&p, slot_count(p.src) > 1) > 0) ;   // 0x42C280 / 0x44ACC0
writeback(p.dst);                                          // 0x42D8E0

AI_army_planner::recruit(&p, hero + 0x91,
        hero::get_morale(hero, 0, 0, 1),
        town::GetGarrison(town), pd + 0x9C /*resources*/, 1, bonus);   // 0x42D690

// then a difficulty-gated tail keyed on gpGame->b[0x1F6D8] and town->b[0x02]
```

The important parameter is `skillDiff`. §4B.4 showed `take_best_stack` scores every move as
`skillDiff × valueOfAdding / 40`, so when the visiting hero is **no stronger than the hero already
sitting in the town**, `skillDiff` is 0 and **not a single creature moves**. That is the rule that
keeps AI garrisons intact when a weak hero passes through, and it is the same rule that funnels
armies into one main hero (§4B.4).

Compare with §4B.10a, which runs the identical sequence for a *hypothetical* hire with
`skillDiff = max(sum(candidate), 0)` — no giver, so nothing is subtracted.

Note the `[ebp+0x0B]` read at `0x42BAA2`: it takes the fourth byte of the `hero` pointer argument and
stores it as a flag, so that flag is always 0 in practice. Same codegen quirk as trap 11.

### The dwelling pass, and the Easy-mode skip

After the exchange and recruitment above, `0x42BA60` runs a second phase over the town's dwellings —
but only if it gets past this gate at `0x42BC0D`:

```c
if (gpGame->b[0x1F6D8] /*difficulty*/ != 0) goto dwellingPass;   // difficulty ≥ 1: always

int myTeam = (curPlayer < 0) ? curPlayer : gpGame->b[0x1F879 + curPlayer];
if (myTeam >= 0)
    for (int p = 0; p < 8; p++)
        if (gpGame->b[0x1F879 + p] == myTeam && is_computer(gpGame, p))
            goto dwellingPass;                                   // a computer ally exists

return;                                                          // otherwise skip it entirely
```

**On difficulty 0, an AI whose team contains no other computer player skips the dwelling pass
altogether.** It is a deliberate handicap, and the team test means it only applies when the AI is
genuinely alone against humans — an AI with a computer ally plays the pass normally even on Easy.
`gpGame + 0x1F879` is the per-player team table (§4C.3, condition 8).

The pass itself iterates **30 entries of the building-bit table starting at index 30**
(`0x66CE88` = `0x66CD98 + 30*8`, §4C.3) — buildings 30–59, the dwellings — calling `0x5C0F20` for
each and re-reading the hero's morale, so newly bought creatures are priced with the army as it
stands after each purchase.

## 4.14 Where to dig for the Grail — `0x52C9B0`

The AI does compute a Grail location, and the way it does it is the same template-matching the
puzzle-map screen uses. `0x4BAE50` (37 bytes, called from `begin_turn`, §4G.3) is the AI-side
accessor:

```c
void AI_update_grail_guess(playerData *pd, int player) {
    int coord;
    this->d[0x39] = *AI_reduce_grail_area(&coord, player);   // 0x52C9B0
}
```

### The state it reads

| address | meaning |
|---|---|
| `gpGame + 0x4E3E8` | `int8` — **number of obelisks on this map** |
| `gpGame + 0x4E3E9` | `int8[48]` — one byte **per obelisk**, a bitmask of the players that have visited it |
| `gpGame + 0x1F690 / +0x1F692 / +0x1F694` | the true dig site `x` / `y` (int16) and `z` (int8) |
| `gpGame + 0x1F696` | `int8` — this map has a Grail at all |
| `gpGame + 0x1F6B0 + player*4` | `int32` — the player's puzzle art set (its town alignment) |
| `0x6976E8` | 48-bit bitset — which puzzle pieces are currently **removed** |

**This corrects §4.8a**: the obelisk array is indexed by *obelisk*, with a bit per *player*, not the
other way round. The Obelisk handler is

```c
// 0x52A2B0  __fastcall(int *obeliskCell /*ecx*/, int player /*edx*/)
int obelisk = *obeliskCell;
if (gpGame->b[0x4E3E9 + obelisk] & (1 << player)) return 0;      // we have already dug it up
```

### How many puzzle pieces the player has earned — `0x4BAF00`

```c
// __fastcall(gpGame /*ecx*/, int player, bool buildBitset)
int bit = 1 << player;
int visited = 0;
for (int i = 0; i < 48; ++i) if (gpGame->b[0x4E3E9 + i] & bit) ++visited;

int total   = gpGame->b[0x4E3E8];
int missing = 48 - total;                       // maps with fewer than 48 obelisks
float f = (float)visited / (float)total;
int n = ftol((float)missing * ((f + 1.0f) * f / 2.0f) + (float)visited);   // 0x63B9E0 = 2.0f
if (visited == total) n = 48;                   // all obelisks → the whole puzzle
n += (int8)playerData[player].b[0x38];          // the map's per-player bonus pieces
if (n > 48) n = 48;

if (!buildBitset) {
    // seed a private RNG from the player and scatter n pieces into the bitset at 0x6976E8
    srand(player * 0x67BCD + 0x677BD);          // 0x50C5F0
    …pick n distinct piece indices with rand_range… // 0x50B230
}
return n;
```

On a map with all 48 obelisks the relationship is linear; on a map with fewer, the quadratic term
`missing × (f+1)f/2` fills in the difference, so partial progress is worth proportionally more.
**Which** pieces come off is randomised per player from a fixed seed, so two AI players with the same
obelisk count see different parts of the map.

### The difficulty gate

```c
int n = AI_get_puzzle_pieces(gpGame, player, /*buildBitset=*/false);
double revealed = (double)n / 48.0;                          // 0x640640 = 48.0
if (g_grailGuessThreshold[gpGame->b[0x1F6D8]] > revealed) return invalidCoord;
```

`g_grailGuessThreshold` (doubles at `0x6822C8`) = **{1.1, 0.5, 0.25, 0.0, 0.0}**.

The `1.1` at index 0 is unreachable — `revealed` never exceeds 1.0 — so at that difficulty the AI
**never** hunts for the Grail; at indices 3 and 4 it starts guessing before it has visited a single
obelisk. Together with the two other difficulty tables (§4G.1 and §5D.2) this fixes the direction of
`gpGame + 0x1F6D8`: see the table at the end of this section.

### Building the stencil

```c
// 19 x 17 = 323 bytes, one per puzzle grid cell, all set to 1
uint8 mask[17][19];  memset(mask, 1, 323);

int set = gpGame->d[0x1F6B0 + player*4];          // 0 if −1
for (int piece = 0; piece < 48; ++piece) {
    if (bitset_test(0x6976E8, piece)) continue;   // this piece has been REMOVED — skip it
    int n = g_pieceOrder[48*set + piece];                            // int16 @ 0x681F64
    sprintf(name, "puz%s%02d.pcx", g_puzPrefix[set], n);             // 0x681880 / 0x682314
    image *img = load_pcx(name);                                     // 0x55A800
    int y = g_pieceY[96*set + n] - 8;                                // int16 @ 0x6818A4
    int x = g_pieceX[96*set + n] - 8;                                // int16 @ 0x681904
    stencil_blit(img, mask, y, x);                                   // 0x52C8B0
    img->vft[1]();                                                   // free
}
```

`stencil_blit` samples the piece's bitmap on a **32-pixel grid** over the 608 × 544 puzzle image
(608/32 = 19, 544/32 = 17 — that is where the grid dimensions come from) and clears
`mask[cell] = 0` wherever the piece has a non-transparent pixel. So after the loop **`mask[cell] != 0`
means "no piece covers this cell — the player can see the terrain there"**, which is precisely the
information visiting obelisks buys.

### The candidate template

```c
rec cand[19][17];                                  // 16 bytes each, 0x1430 total, on the stack
for (each) { cand.d[0] &= ~0x3FF;  cand.b[4] = 0xFF;
             cand.d[8] = (cand.d[8] & ~0x1F) | 0x1F;      // terrain field = 0x1F ("unknown")
             cand.b[0xC] = (cand.b[0xC] & ~4) | 1; }

mapCoords origin = map_view_origin(gpGame);        // 0x4CEA70
for (int row = 0; row < 17; ++row)
for (int col = 0; col < 19; ++col) {
    mapCoords p = { origin.x + col, origin.y + row, gpGame->b[0x1F694] };
    if (!map_position_valid(&p)) continue;         // 0x4B1330
    if (mask[row][col] == 0)     continue;         // still covered by a piece
    build_record(&cand[col][row], cell_at(p), p);  // 0x52C770
}
```

`build_record` @ `0x52C770` packs, into 16 bytes, everything the puzzle image can betray about a tile:

```c
r->d[0] &= ~0x3FF;                                  //   (x field cleared)
r->b[0x0C] |= 4;                                    //   "this cell is a live candidate"
r->b[0x04]  = 0xFF;
r->d[0x08]  = (cell->d[4] & 0x1F)                   //   bits 0…4  terrain type
            | (((cell->d[4] >> 16) & 0x0F) << 5)    //   bits 5…8  object / decoration
            | ((cell->d[8] & 0x0F) << 9);           //   bits 9…12 variant
r->b[0x0C] bit 0 = is_passable(cell);               // 0x4FCCF0
r->b[0x0C] bit 1 = (p == the true dig site);
```

A cell that was never filled in keeps its initialised terrain field of `0x1F`, whose **bit 4 is set** —
that is the sentinel the refinement pass tests for "still unknown".

### The reduction — `0x52CF10`

This is the part that was previously unexpanded, and it is **template matching**, not geometry.

```c
// PASS 1 — the bounding box of the live template cells
bool any = false;  int firstCol, firstRow;
int minCol = 19, maxCol = 0, minRow = 17, maxRow = 0;
for (int col = 0; col < 19; ++col)
for (int row = 0; row < 17; ++row) {
    if (!(cand[col][row].b[0x0C] & 4)) continue;
    if (!any) { firstCol = col; firstRow = row; any = true; }
    minCol = min(minCol, col); maxCol = max(maxCol, col + 1);
    minRow = min(minRow, row); maxRow = max(maxRow, row + 1);
}
if (!any) return invalidCoord;

// PASS 2 — slide the template over every tile of every level
int xLo = max(max(firstCol - minCol, firstCol - 9), 0);
int xHi = min(min(MAP_WIDTH  - maxCol + firstCol, MAP_WIDTH  + firstCol - 9), MAP_WIDTH);
int yLo = max(max(firstRow - minRow, firstRow - 8), 0);
int yHi = min(min(MAP_HEIGHT - maxRow + firstRow, MAP_HEIGHT + firstRow - 8), MAP_HEIGHT);

int bestScore = 0, plausibleAnchors = 0;  mapCoords best = invalid;
for (int z = 0; z <= gpGame->b[0x1FC48]; ++z)
for (int ay = yLo; ay < yHi; ++ay)
for (int ax = xLo; ax < xHi; ++ax) {
    int matches = 0;
    for (int row = 0; row < 17; ++row)
    for (int col = 0; col < 19; ++col) {
        mapCoords p = { ax + col - firstCol, ay + row - firstRow, z };
        if (!map_position_valid(&p))                       continue;
        if (!(cand[col][row].b[0x04] & 4))                 continue;   // template cell is blank
        if (!(map_visibility_mask(p) & (1 << player)))     continue;   // we have not explored it
        rec probe;  build_record(&probe, cell_at(p), p);
        if ((cand[col][row].d[0x00] ^ probe.d[0x00]) & 0x3FF)  continue;
        if ( cand[col][row].b[0x04] ^ probe.b[0x04])           continue;
        if ((cand[col][row].d[0x08] ^ probe.d[0x08]) & 0x001F) continue;   // terrain
        if ((cand[col][row].d[0x08] ^ probe.d[0x08]) & 0x1FE0) continue;   // object / variant
        if ((cand[col][row].b[0x0C] ^ probe.b[0x0C]) & 1)      continue;   // passability
        ++matches;
    }
    if (matches == 0 || matches < bestScore || matches * 2 < bestScore) continue;
    ++plausibleAnchors;
    if (matches <= bestScore) continue;
    if (matches > bestScore * 2) plausibleAnchors = 1;      // a decisively better fit
    bestScore = matches;
    best = { ax + 9, ay + 8, z };                           // the CENTRE of the template
}
return best;
```

**The AI is doing exactly what a human does with a puzzle map**: the pieces it has removed expose a
patch of terrain, and it slides that patch across every part of the map it has already explored until
it finds where the terrain agrees. The `plausibleAnchors` counter tracks how ambiguous the fit still
is — a match more than twice as good as anything else resets it to 1.

The template can only score on tiles the player has **explored**, so scouting and obelisk-visiting
compound: more obelisks widen the stencil, more exploration gives the stencil somewhere to land.

### The refinement

`0x52C9B0` finishes by looking around the returned centre for somewhere actually diggable:

```c
if (centre.x < 0) return centre;                    // the reducer gave up

int bestDist = 0x7FFF;  mapCoords answer = invalid;
for (int y = centre.y - 2; y <= centre.y + 2; ++y)
for (int x = centre.x - 2; x <= centre.x + 2; ++x) {
    mapCoords p = { x, y, centre.z };
    if (!map_position_valid(&p))    continue;
    if (!is_passable(cell_at(p)))   continue;                    // 0x4FCCF0
    rec *r = &cand[(x - origin.x) + 9][(y - origin.y) + 8];
    if (r->d[0x08] & 0x10) {                                     // still an unknown cell
        int d = abs(centre.x - x) + abs(centre.y - y);            // Manhattan
        if (answer.invalid || d < bestDist) { bestDist = d; answer = p; }
    } else if (r->b[0x0C] & 2) {
        return p;                                                 // the TRUE dig site — stop
    }
}
return answer;
```

Two things fall out. The AI will dig on the **nearest still-unidentified passable tile** within two
squares of its best guess — so it can dig in the wrong place and will keep refining as more obelisks
come in. And the `r->b[0x0C] & 2` branch means that **once the template pins the real site, the AI
returns it exactly and immediately** — no further search, no error.

### The three difficulty tables, together

All three are indexed by `gpGame->b[0x1F6D8]`, and read together they fix its direction:

| difficulty | AI combat modifier vs a human (`0x6604D0`) | computer / human attack bonus (§4G.1) | Grail threshold (`0x6822C8`) | supply tests 2 & 3 (§4G.6) | AI hero movement (§6A) |
|---|---|---|---|---|---|
| 0 Easy | 0.50 | +1.00 / −0.40 | 1.10 — never hunts | run | — |
| 1 Normal | 0.50 | +0.50 / +0.50 | 0.50 | skipped | — |
| 2 Hard | 1.00 | +0.25 / +0.75 | 0.25 | skipped | — |
| 3 Expert | 1.25 | 0.00 / +1.00 | 0.00 — always hunts | skipped | **+75** (+125 Explorer) |
| 4 Impossible | 1.25 | −0.25 / +1.25 | 0.00 | skipped | **+75** (+125 Explorer) |

**Index 0 is Easy and index 4 is Impossible** — the menu order. §6A sets out the evidence, which
is the AI movement grant at `0x4E4B58`: a flat bonus to every AI hero can only be a hard-mode cheat,
and it fires exactly at indices 3 and 4.

Every row then reads as a handicap. On **Easy** the AI discounts a human army to half strength before
deciding whether to attack and gives itself the largest self-assessment bonus — so it starts fights
it cannot win — while also refusing top-tier creatures (§4G.6) and never spending turns on the Grail.
On **Impossible** it judges a human army at full weight, rates itself lowest, takes only winnable
battles, and hunts the Grail from turn one. The default (non-human opponent) combat modifier is
`1.25`, so AI-vs-AI fights are always judged at the most conservative setting regardless of
difficulty.


## 4.15 There is no tactics-phase AI

Searched for and **not found**, which is itself the result. The Tactics secondary skill is read in
exactly four places inside the AI ranges — `0x421000`, `0x421280`, `0x424120` and `0x428DD0` — and
all four are already documented; none of them places stacks. The tactics-phase code proper is
`0x448260` (1 424 bytes, from `0x478D80`), and its callee list is entirely window and dialog
routines (`0x59xxxx`, `0x5Axxxx`) — it is the human UI.

So an AI hero with Tactics gets the **battlefield advantage** the skill confers, but never
rearranges its stacks to exploit it. The only place Tactics changes an AI decision is the
quick-combat simulator, where it shifts the speed category (§5B.2):

```c
category = (14 - 2*tactics_advantage + speed) / speed;   // clamped to [0..4]
```

## 4.16 Start-of-turn refresh — `type_AI_player::begin_turn` @ `0x4297C0`

Step 3 of the turn driver (§4.1). 336 bytes; it is pure orchestration — every number it produces is
computed by a routine already specified here.

```c
0x4B8AF0(…);                                   // player bookkeeping
0x429910(this);
0x4280E0(this);                                // uses the [0x63B670] / [0x63B67C] pair
0x429AD0(this);
type_AI_player::compute_wants(this);           // 0x428740 — §4A.1
AI_update_grail_guess(this, player);           // 0x4BAE50 — §4.14
```

So each AI turn begins by recomputing the resource valuations and shopping list (§4A) and refreshing
the Grail estimate (§4.14), in that order, before any hero moves.

## 4.17 Two leaf valuations

**`hero::AI_get_spell_value(int spell)` @ `0x5298D0`** — used by the object dispatch (§4.8) whenever
an object grants a spell:

```c
if (spellTraits[spell].level /*+0x18*/ > hero->b[0xD0] + 2) return 0;   // Wisdom gate
if (hero->b[0x3EA + spell])            return 0;                        // already known
if (!hero::HasArtifact(hero, 0 /*Spellbook*/)) return 0;
return 0x527640(hero, spell);                                           // the real valuer
```

**`hero + 0xD0` is the Wisdom skill level** — skill 7, so `0xC9 + 7` (§4.12). The gate
`level > Wisdom + 2` is exactly H3's rule: no Wisdom caps you at level 2, expert Wisdom (3) allows
level 5. `hero + 0x3EA` is a per-spell "already known" array, distinct from the spellbook flags at
`hero + 0x430` used in §5B.4.

**`AI_melee_exchange_value` @ `0x435C70`** is a 64-byte wrapper: it evaluates
`get_total_hits(attacker, 0)` and `get_total_hits(defender, 0)` (`0x443080`) and tail-calls
`0x435B90` with both, plus the caller's three remaining arguments. All the arithmetic is in
`0x435B90`.
### The two leaf evaluators

**`0x527640(hero, spell)`** — the spell valuer behind `AI_get_spell_value`. It is a
**counterfactual on the spellbook**, the same family as §4.12's magic-school arm:

```c
spellbook_valuer v(&ctx, hero);                       // 0x526D40 — shared ctor
if (ctx.d[0x08] <= 0) return 0;                       // hero has no spells to compare against
int cur = 0x5273D0(&ctx);                             // current total spellbook value
int cat = spellTraits[spell].d[0x0C] & 0x38000;       // narrower mask than §5B.4's 0x1F8000
if (cat == 0) …                                       // uncategorised spell
int with = 0x5275B0(&ctx, cat);                       // value restricted to that category
if (with < cur) …                                     // only an improvement counts
```

`0x526D40` / `0x5273D0` / `0x5275B0` are the shared spellbook-valuation trio; §4.12's `0x524D20`
uses the same three with mask `0x1F8000`.

**`0x435B90(est, attacker, …)`**, `ret 0x18` — the arithmetic behind `AI_melee_exchange_value`:

```c
if (arg6) arg6 = 0x4428F0(attacker, 0);               // stack still able to act
if (est->b[0x09]) {                                   // "kills only" mode
    if (arg2 - attacker->d[0x534] <= 0) return 0;
    if (arg4 - attacker->d[0x534] <= 0) return 0;     // neither exchange kills ⇒ worthless
}
… forwards to the shared exchange evaluator …
```

`army + 0x534` is the hits threshold both comparisons subtract. When `est->b[0x09]` is set the
routine returns **0 for any exchange that does not kill**, which is what makes the "kills only" mode
in §5.4 behave as a hard filter rather than a preference.

# 4A. Adventure-AI economy — the exact model

## 4A.1 `type_AI_player::compute_wants` @ `0x428740`

Runs at the start of every AI turn (from `begin_turn` @ `0x4297C0`) and again after every purchase.

```c
// 1. supply = what we will have after two turns of income
for (i = 0; i < 7; ++i) resource_supply[i] = pd->resources[i] + 2 * pd->income[i];

// 2. demand = the largest single cost among all buildings we could build next
memset(resource_demand, 0, 28);
for (each town) {
    mask = town::get_buildable_mask(town);                       // 0x5C0F20
    for (b = 0; b < 44; ++b)
        if (mask & bit(b))
            for (i = 0; i < 7; ++i)
                resource_demand[i] = max(resource_demand[i], town::get_building_cost(town,b)[i]);
}

// 3. creature wish-list: 145 records {int value; int _; int16 count;}
for (each town, each of the 14 dwelling levels)
    rec[gDwellingType[townType*14 + level]].count += town::weekly_growth(level);
for (i = 0; i < 145; ++i) rec[i].value = traits[i].AI_value * rec[i].count;
sort(rec, descending by value);
// the top 3 entries add their creature costs into resource_demand

// 4. resource values
int nMarkets = number of towns with building 14 (Marketplace);
double base  = tradeRate[clamp(nMarkets, 1, 10)];               // 0x678344
for (i = 0; i < 7; ++i) {
    int dem = resource_demand[i], sup = resource_supply[i];
    double v;
    if (dem == 0)          v = base;
    else if (dem <= sup)   v = ((double)dem + (double)(sup - dem) * base) / (double)sup;
    else {
        v = (double)dem;
        if (sup > 1) v /= (double)sup;
        v = max(v, 1.0 / base);
    }
    v *= baseResourceValue[i];                                   // 0x68C482
    AI_player.resource_value[i] = playerData.resource_value[i] = v;   // pd + 0x128
}
playerData.d[0x160] = ftol(Σ_{i<6} resource_value[i]) / 5;       // "average non-gold value"
```

Constants:

```
tradeRate  @0x678344 (index = #marketplaces, 1..10) = 0.10 0.15 0.20 0.25 0.30 0.35 0.40 0.45 0.50 0.50
baseValue  @0x68C482 = wood 250, mercury 500, ore 250, sulfur 500, crystal 500, gems 500, gold 1
```

Reading the formula: with no demand a resource is worth only the market rate (0.10 with one market,
0.50 with ten) times its base value — i.e. a surplus resource is worth what you could trade it for.
As soon as something the AI wants to build needs it, the value climbs toward the full base value,
and if supply cannot cover the demand it is floored at `baseValue / tradeRate` — up to **10× the
market value** for a bottleneck resource with a single marketplace. That is why an AI short on
sulfur will happily walk a hero across the map for a sulfur pile.

## 4A.2 `AI_resource_cost` @ `0x526C70`

```c
int v = 0;
for (i = 0; i < 7; ++i) v = ftol( res[i] * pd->resource_value[i] + (double)v );
return v;
```

## 4A.3 `type_AI_player::get_total_value` @ `0x42A150`

```c
bool needTrade = false;
for (i = 0; i < 7; ++i)
    if (cost[i] > pd->resources[i] && pd->income[i] == 0) needTrade = true;
if (needTrade) {
    if (!AI_plan_trades(cost, 1, &tmp, &flag)) return -1;    // 0x42A2B0
    if (!AI_do_trades (cost, &tmp, &flag))     return -1;    // 0x42A580
}
int c = 0;
for (i = 0; i < 7; ++i) c = ftol( cost[i] * AI_player.resource_value[i] + (double)c );
return base * 1000 / c;                                      // value per 1000 units of cost
```

`AI_plan_trades` (`0x42A2B0`) keeps a hard floor: `reserved_funds[i]` is clamped up to **20**, and the
tradeable surplus of resource *i* is additionally capped by `resource_supply[i] − resource_demand[i]`.

## 4A.4 Town building — `type_AI_player::AI_build_one_building` @ `0x42AE00`

```c
best = 0; bestTown = 0; bestBuilding = -1;
for (each town T) {
    if (T already built something this turn) continue;
    buildable = town::get_buildable_mask(T);
    int value[44];
    for (b = 0; b < 44; ++b) {
        if (!town::type_can_have(T, b) || (T->built & bit(b)) || b == 26 /*GRAIL*/)
            { value[b] = -1; continue; }
        value[b] = evaluate(T, b);                        // table below
    }
    int total[44] = {0};
    for (b = 0; b < 44; ++b) {
        if (value[b] <= 0) continue;
        closure = prerequisite closure of b (table 0x697798, indexed townType*44 + b) & ~T->built;
        cost    = Σ town::get_building_cost(T, x) over the closure;
        int v   = AI_player::get_total_value(value[b], cost);
        if (v < 0) continue;
        for (x in closure) total[x] += v;                 // propagate value to prerequisites
    }
    for (b = 0; b < 44; ++b)
        if ((buildable & bit(b)) && total[b] > best) { best = total[b]; bestTown = T; bestBuilding = b; }
}
if (!bestTown) return false;
cost = town::get_building_cost(bestTown, bestBuilding);
AI_player::reserve_funds(cost, true);                     // 0x42A470
... affordability check ...
town::build(bestTown, bestBuilding);                      // 0x5BF3C0
type_AI_player::compute_wants();                          // 0x428740
return true;
```

**The prerequisite-closure step is what makes the AI build Forts and Mage Guilds at all** — those
buildings have no direct value; they inherit it from whatever they unlock.

### Per-building evaluators

| Building ids | Evaluator | Formula |
|---|---|---|
| 8 Citadel, 9 Castle | `0x42B670` | Σ over the 14 dwelling levels of `traits[creature].AI_value` for the extra weekly growth they give (only from scenario week 5 onward); plus **5 000 000** if this town matches a "build this structure" victory condition |
| 10–13 Village/Town/City/Capitol Hall | `0x42B8B0` | income-based, using `pd->goldValue` and per-hall gold rates (`0x63B698`, `0x63B6A0`, `0x63B6A8`); plus **5 000 000** for the victory condition |
| 15 Resource Silo | `0x42AF52` | `−1` if the town is under threat, else `7 × AI_resource_cost(silo output)` |
| 18 Horde 1, 24 Horde 2 | `0x42B790` | `traits[ct].AI_value × hordeGrowth`; adds `cost × hordeGrowth` to the running cost; `−1` if the creature is flagged unaffordable |
| 19/25 Horde upgrades | `0x42B800` | same, gated on the upgrade bitmask `0x66CD90` |
| 30–36 Dwellings 1–7 | `0x42B520` | `traits[ct].AI_value × weeklyGrowth`; running cost `creatureCost × weeklyGrowth`; `−1` if flagged |
| 37–43 Dwelling upgrades | `0x42B5B0` | `(AI_value(upgraded) − AI_value(base)) × creaturesAvailable` |
| everything else | town-type table `0x42B4FC` | per-faction special buildings only; the default is **0** |

The "creature flagged" byte array comes from `0x429D50`, which marks a creature type as undesirable
when the player cannot afford its recruitment cost, when it is a level-6/7 creature, or when the
strongest enemy could out-value the player's own dwellings with it.

## 4A.5 `AI_compute_resource_supply_and_threats` @ `0x429D50`

```c
for (i = 0; i < 7; ++i) supply[i] = 7 * pd->income[i];
for (each town, each of 14 dwelling levels)
    supply -= creatureCost(gDwellingType[…]) * weeklyGrowth;      // a full week of recruitment
// then, over all players on other teams, find the strongest dwelling portfolio
for (ct = 0; ct < 145; ++ct) {
    threatFlags[ct] = 0;
    if (any of the first 6 resources has cost[ct][i] > 0 && supply[i] <= 0) threatFlags[ct] = 1;
    if (difficulty == 0) continue;
    if (traits[ct].level == 6)                                    threatFlags[ct] = 1;
    if (traits[ct].growth * traits[ct].AI_value + ourTotal > enemyBest) threatFlags[ct] = 1;
}
```

---


# 4G. The remaining kingdom-side routines

Everything in this section was previously named-but-not-specified.

## 4G.1 `advManager::AI_prepare` @ `0x527960` — and where `playerData + 0x164` comes from

Called once per AI turn, before `AI_take_turn`'s two passes.

```c
void AI_prepare(int player)
{
    playerData *pd = gpCurPlayer;                     // 0x69CCB0
    *(int*)0x691680 = *(int*)0x691684 = 0;

    // 1. refresh every hero's cached valuations (§4.9b)
    for (int i = 0; i < pd->heroCount; ++i)
        hero::AI_update_valuations(&gpGame->heroes[pd->heroIds[i]]);   // 0x527760

    // 2. the average artifact value
    int sum = 0, n = 0;
    for (int a = 7; a < 144; ++a) {                   // ids 0…6 (book, scroll, grail, machines) excluded
        if (g_artifactTraits[a].b[0x1C] != 0) continue;    // 0x660B68, stride 32 — "not AI-tradable"
        ++n;
        pair q = { a, -1 };
        sum += AI_get_value_of_artifact_for_player(&q, player);        // 0x433AA0, §4.9a
    }
    pd->f[0x164] = (float)((double)sum / (double)n);

    // 3. the difficulty-derived combat bonuses
    int d = (int8)gpGame->b[0x1F6D8];
    if (d == 0) { g_attack_computer_bonus = 1.00f;  g_attack_human_bonus = -0.40f; }
    else        { g_attack_human_bonus    = (d + 1.0f) * 0.25f;        // 0x64058C = 0.25f
                  g_attack_computer_bonus = 0.75f - d * 0.25f; }       // 0x640588 = 0.75f
}
```

**`playerData + 0x164` is the mean of `AI_get_value_of_artifact_for_player` over every artifact
whose traits byte `+0x1C` is zero** — i.e. the average artifact on this map, evaluated against this
player's best-placed hero. It is recomputed every turn, so it tracks the player's heroes as they
grow. Pandora's Box, creature banks, Corpse, Sea Chest, Treasure Chest, Wagon and Warrior's Tomb all
read it.

**`gpGame + 0x1F6D8` runs 0 = Easy to 4 = Impossible**, matching the menu order. (An earlier draft
of this report asserted the opposite; §6A gives the evidence that settles it.)

| `+0x1F6D8` | computer attack bonus | human attack bonus |
|---|---|---|
| 0 Easy | **+1.00** | −0.40 |
| 1 Normal | +0.50 | +0.50 |
| 2 Hard | +0.25 | +0.75 |
| 3 Expert | 0.00 | +1.00 |
| 4 Impossible | −0.25 | +1.25 |

Read with that ordering the table makes sense as a handicap: on **Easy** the AI wildly overrates its
own armies (+1.00) and *underrates* the human's (−0.40), so it charges into fights it loses; on
**Impossible** it underrates itself and overrates the human, so it only takes battles it will win.

The image's static values for `g_attack_computer_bonus` / `g_attack_human_bonus` (`0x6604F8` /
`0x6604FC`) are both `0.5`, i.e. the level-1 pair — do not read them statically.

## 4G.2 `AI_pick_special_hero` @ `0x526A90` — PASS 1's selector

`__fastcall(int player /*ecx*/, bool *onlyOne /*edx*/)` → `hero*` or `NULL`.

```c
*onlyOne = true;
hero *best = NULL;  int bestSkills = 0;

for (int i = 0; i < pd->heroCount; ++i) {
    hero *h = &gpGame->heroes[pd->heroIds[i]];
    if (h->d[0x4D] /*movement left*/ <= 0) continue;
    if (h->b[0x11C] /*done this turn*/)    continue;
    if (best) *onlyOne = false;

    int skills = hero::get_primary_skill_sum(h);          // 0x4E5960
    if (best) {
        bool candCommitted = (h   ->b[0x44] != 0xFF);     // has a previous destination
        bool bestCommitted = (best->b[0x44] != 0xFF);
        if (candCommitted && !bestCommitted) continue;                    // prefer UNcommitted
        if (candCommitted == bestCommitted && skills >= bestSkills) continue;  // prefer WEAKER
    }
    best = h;  bestSkills = skills;
}
if (best) return best;

// nobody can move — try to wake a garrisoned hero instead
*onlyOne = false;
mapManager::refresh(gpMapMgr, 0, 1);                      // 0x417680
pd->d[0x04] = -1;
if (pd->heroCount >= 8) return NULL;
for (int t = 0; t < pd->townCount; ++t) {
    town *tw = &gpGame->towns[pd->townIds[t]];
    if (tw->w[0x0C] < 0)  continue;                       // no garrisoned hero
    if (tw->d[0x10] >= 0) continue;                       // a visiting hero is in the way
    hero *g = &gpGame->heroes[tw->w[0x0C]];
    if (!armyGroup::is_nonempty(&g->army)) continue;      // 0x44ADA0
    if (g->d[0x4D] == 0 || g->b[0x11C]) continue;
    town::move_garrison_hero_out(tw);                     // 0x5BE390
    return g;
}
return NULL;
```

The "special" hero is therefore the **least developed hero that is not already committed to a
destination** — the scout. PASS 1 exists to move the throwaway hero first, so that PASS 2's
better heroes see a map it has already partly uncovered. When every hero is spent, PASS 1 pulls a
garrisoned hero out of a town instead, which is why AI towns so often empty themselves late in a turn.

## 4G.3 `type_AI_player::begin_turn` @ `0x4297C0`, in full

```c
playerData *pd = &gpGame->playerData[this->player];

// 1 & 2 — clear the per-hero turn flags, for field heroes and garrisoned heroes alike
for (int i = 0; i < pd->heroCount; ++i) {
    hero *h = &gpGame->heroes[pd->heroIds[i]];
    h->b[0x43] = 0;      // "destination reachable"
    h->b[0x11C] = 0;     // "done for this turn"
}
for (int t = 0; t < pd->townCount; ++t) {
    town *tw = &gpGame->towns[pd->townIds[t]];
    if (tw->d[0x0C] < 0) continue;
    hero *g = &gpGame->heroes[tw->d[0x0C]];
    g->b[0x43] = 0;  g->b[0x11C] = 0;
}

recompute_all_player_income();                       // 0x4B8AF0 - below

// 3 — the Eye-of-the-Magi scouting value (below)
bool arm = (gpGame->b[0x1F6D8] > 0) && (pd->townCount > 0);
this->d[0x04] = AI_eye_of_the_magi_value(this->player, arm);   // 0x429910

// 4 — the two kingdom-goal passes (below)
run_goal_pass({ (goal_vft*)0x63B67C, this->player });
run_goal_pass({ (goal_vft*)0x63B670, this->player });

// 5 — the economy
compute_weekly_recruitment_cost(this);               // 0x429AD0 - below, feeds 4G.6
type_AI_player::compute_wants(this);                 // 0x428740 — §4A.1

// 6 — the Grail estimate
AI_update_grail_guess(pd, this->player);             // 0x4BAE50 — §4.14
```

### `recompute_all_player_income` @ `0x4B8AF0`

Rebuilds `playerData->income[7]` (`+0x108`) from scratch for every player in the game, then walks
every town on the map adding its yields. One term is worth naming because it explains two otherwise
mysterious town fields:

```c
if ((town.built & g_mysticPondMask /*0x66CE20*/) && town->b[0x34] > 0)
    playerData[owner].income[town->d[0x38]] += town->b[0x34];
```

`town + 0x34` is the Rampart **Mystic Pond**'s rolled amount and `town + 0x38` its rolled resource;
both are re-rolled weekly by `0x4C8160` with `rand(0,3)` into the 4-entry table at `0x63E668` for the
resource and `rand(1,4)` for the quantity.

**`town + 0x34` is not `playerData + 0x34`.** Both structures are `0x168` bytes and both are walked
with a `+= 0x168` stride; the Mystic Pond amount and the AI personality (§6A.1) sit at the same
offset in different records. This collision cost three separate misreadings during the audit.

### `compute_weekly_recruitment_cost` @ `0x429AD0`

Walks the player's towns and, for each of the 14 dwelling slots, calls `creature_recruit_cost`
(`0x54E7C0`) for the creatures currently available, accumulating the total. That total is what
`AI_compute_resource_supply_and_threats` (§4G.6) subtracts from weekly income to decide which
resources are scarce: **a full week of recruitment in every owned town is treated as already
committed** before anything else is valued.

### `AI_eye_of_the_magi_value` @ `0x429910`

```c
// __fastcall(int player /*ecx*/, bool enabled /*dl*/)
if (!enabled) return 0;                       // Easy, or the player owns no town
int total = 0;
for (int z = 0; z <= gpGame->b[0x1FC48]; ++z)
for (int x = 0; x <  MAP_WIDTH;  ++x)
for (int y = 0; y <  MAP_HEIGHT; ++y) {
    mapCell *c = cell_at(x, y, z);
    if (c->d[0x1E] != 27 /*Eye of the Magi*/) continue;
    if (!(c->b[0x0D] & 0x10))                 continue;
    total += scouting_value(player, /*radius=*/10, packed(x,y,z));   // 0x432220, §4.8a
}
return total;
```

So `type_AI_player + 0x04` is **the total map area every Eye of the Magi would reveal**, priced with
the same routine the Redwood Observatory uses. The §4.8 handler for **Hut of the Magi** (object 37)
simply returns this cached number, and §4F.5 shows it being invalidated the moment a hero walks into
a Hut. Note the two different objects: the value is accumulated over the **Eyes** (object 27), and
consumed by the **Hut** (object 37) — visiting the Hut is what triggers the reveal at every Eye.
The whole scan is skipped on Easy or while the player is townless.

## 4G.4 The kingdom-goal pass — `0x4280E0`

```c
struct kingdom_goal { goal_vft *vft; int player; };
// goal_vft = { void reset(); bool skip(town*); void visit(town*); void dtor(); }

void run_goal_pass(kingdom_goal *g)
{
    g->vft->reset();
    for (int p = 0; p < 8; ++p) {                                     // every OTHER player
        if (g->player >= 0 && p >= 0
            && gpGame->b[0x1F879 + p] == gpGame->b[0x1F879 + g->player]) continue;   // same team
        if (gpGame->b[0x1F636 + p]) continue;                         // not in the game
        playerData *pd = &gpGame->playerData[p];
        for (int i = 0; i < pd->heroCount; ++i) {
            hero *h = &gpGame->heroes[pd->heroIds[i]];
            int mp = hero::max_movement(h);                           // 0x4E4D90
            h->d[0x11D] = 0;
            searchArray::compute(gpSearchArray, h, h->pos, /*no goal*/,
                                 mp + 800, (h->d[0x105] >> 18) & 1, 3, mp + 800, 0);
            visit_our_towns(g, h, gpSearchArray);                     // 0x4282B0
        }
    }
}

// 0x4282B0
void visit_our_towns(kingdom_goal *g, hero *enemy, searchArray *sa)
{
    playerData *pd = &gpGame->playerData[g->player];                  // OUR towns
    for (int i = 0; i < pd->townCount; ++i) {
        town *t = &gpGame->towns[pd->townIds[i]];
        if (g->vft->skip(t)) continue;
        if (sa->cellAt(t->entrance /*+0x05,+0x06,+0x07*/).reachable)
            g->vft->visit(t);
    }
}
```

The enemy hero is given **`maxMovement + 800`** of range, i.e. roughly a turn and a half.

| goal | vftable | `reset` | `skip` | `visit` |
|---|---|---|---|---|
| A | `0x63B670` | `0x428260` — clears `town->b[3]` for every town on the map | `0x5543F0` — always false | `0x428570` — `++town->b[3]` |
| B | `0x63B67C` | `0x5BC690` — a bare `ret` | `0x5543F0` | `0x428580` |

**`town + 0x03` is therefore "how many enemy heroes can reach this town within about 1½ turns".**
That is the AI's entire notion of a town being under threat, and it is the value read by the
Resource Silo, Stronghold and Fortress building arms below.

## 4G.5 The faction-special building table — `0x42B4FC`

`AI_build_one_building`'s per-building switch (index table `0x42B4D8`, jump table `0x42B4B8`,
keyed on `buildingId − 8`) sends every building it does not recognise to a **second** switch on the
town's faction (`town->b[4] − 1`, jump table `0x42B4FC`). Factions 0 (Castle) and 8 (Conflux) are not
in the table at all and score 0 for every special.

| faction | building | value |
|---|---|---|
| 1 Rampart | 22 (Treasury) | `0` unless it is the **last day of the week**; then `ftol(pd->gold × pd->resource_value[6] / 10.0)` |
| 1 Rampart | 17 (Mystic Pond) | `2 × pd->d[0x160]` (twice the average non-gold resource value) |
| 2 Tower | 21 | `100` |
| 3 Inferno | — | 0 |
| 4 Necropolis | 21 (Necromancy Amplifier) | `1000 ×` the number of our heroes whose class (`hero->d[0x30]`) is 9 |
| 4 Necropolis | 17 (Cover of Darkness) | `10` |
| 5 Dungeon | — | 0 |
| 6 Stronghold | 17 | `5000` if the town is **under threat** (`town->b[3] != 0`) and owned (`town->d[0x0C] >= 0`), else 0 |
| 7 Fortress | 21, 22 | `0` unless under threat; then `armyGroup::get_AI_value(garrison) / 20` |

The Resource Silo arm (`0x42AF52`) is the mirror image: **`−1` when the town is under threat**, so
the AI will not spend on economy while an enemy hero is within reach; otherwise
`7 × AI_resource_cost(silo output)`.

That answers "what *under threat* means" for every arm that uses it: it is `town->b[3] != 0`, filled
by goal pass A above.

## 4G.6 `AI_compute_resource_supply_and_threats` @ `0x429D50` — the third test

The routine ends by marking creature types the AI should not chase:

```c
for (int ct = 0; ct < creatureCount; ++ct) {
    flags[ct] = 0;
    int cost[7];
    creature_recruit_cost(ct, cost);                       // 0x54E7C0

    // TEST 1 — a resource we have no supply of
    for (int i = 0; i < 6; ++i)
        if (cost[i] > 0 && supply[i] <= 0) flags[ct] = 1;

    if (gpGame->b[0x1F6D8] != 0) continue;                 // ← only on the hardest difficulty
    for (int p = 0; p < 8; ++p)                            // …and only for an all-AI team
        if (gpGame->b[0x1F879 + p] == myTeam && is_human(p) /*0x4CE940*/) goto next;

    // TEST 2 — top-tier creature
    if (traits[ct].level /*+0x04*/ == 6) flags[ct] = 1;

    // TEST 3
    if (traits[ct].weeklyGrowth /*+0x44*/ * traits[ct].AI_value /*+0x40*/ + ourStock > bestRivalStock)
        flags[ct] = 1;
  next: ;
}
```

with, computed just above and **both zero unless the difficulty word is 0**:

* `bestRivalStock` — the maximum over every player **not on our team** of
  `Σ towns Σ 14 dwelling slots (available × traits[creature].AI_value)`;
* `ourStock` — the same sum for our own player.

**`traits + 0x04` is 0-based** — the test is `== 6`, and level-7 creatures are the ones being
excluded. A reimplementation using a 1-based tier must compare against 7.

Two consequences worth stating plainly: on any difficulty other than the hardest, or on a team that
contains a human, **only test 1 runs**; and even then the AI is only refusing creatures whose cost
includes a resource it has no income for.

## 4G.7 Trading — `0x42A470` and friends

The trade machinery is four routines, and `AI_plan_trades` does commit:

```c
// 0x42A470  AI_plan_and_do_trades(this, int cost[7], int *out, bool commit) ; ret 8
int  spare[7] = {0,0,0};                        // the scratch resource vector
if (!0x42A2B0(this, cost, &plan, &opts)) return partial;      // "can the deficit be covered?"
if (!0x42A580(this, cost, &plan, &opts)) return partial;      // "choose the trades"
if ( 0x42AB40(this, &plan)) {                                 // "are they still valid?"
    ... rebuild the vector, re-plan ...
    0x42AC20(this, &plan);                                    // ← EXECUTE the trades
}
```

`0x42A2B0` answers feasibility, `0x42A580` picks which resource to sell and at what rate,
`0x42AB40` re-validates after the plan is built, and **`0x42AC20` is the commit**. The `commit`
flag is the fourth argument threaded through from `AI_build_one_building`'s call to
`reserve_funds`, which is why the same code both prices a purchase and pays for it.

## 4G.8 `type_AI_player::get_total_value` @ `0x42A150`

```c
playerData *pd = &gpGame->playerData[this->player];
bool needTrade = false;
for (int i = 0; i < 7; ++i)
    if (cost[i] > pd->resources[i] /*+0x9C*/ && pd->income[i] /*+0x108*/ == 0)
        needTrade = true;
if (!needTrade) { /* the plain path — no market involved */ }
else            { /* run the trade planner above and re-price */ }
```

The "divide by zero when cost is 0" worry does not arise: a zero cost vector makes `needTrade`
false, and the plain path never divides by the cost. Guarding it is harmless but unnecessary.


# 4B. Towns, troops and logistics

## 4B.1 `hero::AI_town_value` @ `0x52AB80`

The TOWN branch of `AI_object_value`. `__fastcall(hero*, int x /*edx*/, int y, int z, int16 moveLimit)`.

```c
int idx = gpGame->GetObjectIndexAt(x, y, z);        // 0x4BB870
if (idx < 0) return 0;
town* t = &gpGame->towns[idx];                      // town stride = 360 (0x168)
NewmapCell* cell = &map[(z*H + y)*W + x];

if (t->owner != hero->owner) {
    if (t->owner >= 0 && team[t->owner] == team[curPlayer])
        return AI_town_visit_value(hero, t);        // 0x52B1E0 — allied town, just visit
    if (t->visitingHeroId >= 0)
        AI_value_of_combat(hero, 0, &visitor->army, 0, cell);   // (evaluated, result unused here)
    return AI_town_capture_value(hero, t, moveLimit, cell);     // 0x529CB0
}

// ---- our own town ----
int v = AI_town_recruit_value(hero, t, moveLimit)   // 0x52B090
      + AI_town_visit_value(hero, t);               // 0x52B1E0

// Grail delivery
if (hero_carries_artifact(hero, 2 /*Grail*/)                       // 0x4D91B0
    && town::type_can_have(t, 26 /*GRAIL*/) && !(t->built & grailBit)) {
    if (victoryCondition == 4 && the coords match this town) v += 5'000'000;
    else v += AI_get_value_of_artifact(2 /*Grail*/, hero->owner);
}
if (victoryCondition == 10 && this is the target town && we hold the artifact) v += 5'000'000;

// fortified-town baseline
if (t->b[3] != 0) {
    if (victoryCondition == 6 && the coords match this town) v += 5'000'000;
    else v += 5'000'000 / numTowns;                 // numTowns = (townsEnd-townsBegin)/360
}

// artifact hand-off to the hero garrisoned here
if (t->visitingHeroId >= 0)
    for (each artifact id in the 5-entry table 0x640558..0x64056C)
        if (hero_carries_artifact(hero, id))
            v += total_artifact_value(visitor, id, -1);          // 0x4339E0

// hero swap — the whole arm is skipped on Easy
if (gpGame->b[0x1F6D8] /*difficulty*/ <= 0) return v;
if (garrisonHero) {
    int theirs = hero::get_primary_skill_sum(garrisonHero);      // 0x4E5960
    int ours   = hero::get_primary_skill_sum(this);
    if ((theirs * 5) / 4 >= ours) return v;         // the resident is already ≥ 80 % as strong
}

// what the "..." used to hide: victory condition 6 = "capture a specific town"
if (gpGame->b[0x1F89C] != 6) return v;
int target = map_town_at(gpGame->d[0x1F8B4], gpGame->d[0x1F8B8], gpGame->d[0x1F8BC]);  // 0x4BB870
if (target < 0 || target != this_town_index) return v;
v += 5000000;                                       // §4C
return v;
```

The elided tail is therefore the **ninth victory-condition site** (§4C.3): a town that is the
scenario's designated capture target is worth the same flat 5 000 000 as every other victory
objective. The `× 5 / 4` is the exact threshold — the AI swaps a hero into its own town only when it
is more than 25 % stronger than the hero already standing there — and the whole arm, including the
victory override, is **dead on Easy**.

**`5'000'000 / numTowns` on every fortified town we own** is the AI's structural reason to keep coming
home. With eight towns on the map that is 625 000 per town, which dominates most map objects.

## 4B.2 `hero::AI_town_capture_value` @ `0x529CB0`

```c
int combat = AI_value_of_combat(hero, defenderHero, garrison, t, cell);
if (combat <= -500'000'000) {
    if (pd->townCount > 0) return 0;                 // hopeless and we don't need it
    combat = -2'500'000;                             // townless: still worth considering
}
int v = ftol( town::get_income(t) * pd->goldValue * 3.0 );        // three days of income
if (t->built & specialResourceBit)
    v += 3 * AI_resource_cost(pd, town::resource_production(t));

int weeksAhead = (moveLimit - hero->mp) / hero->maxMp + gpGame->w[0x1F63E] /*day of week, 1…7*/;
bool wholeWeek = (weeksAhead >= 7);
for (level = 0; level < 14; ++level) {
    int n = t->w[0x16 + level*2] + (wholeWeek ? town::weekly_growth(t, level) : 0);
    if (n <= 0) continue;
    ct   = gDwellingType[t->type*14 + level];
    gain = traits[ct].AI_value - AI_resource_cost(pd, creatureCost(ct));
    if (gain > 0) v += gain * n;
}
v = ftol( (type_AI_player::get_attack_bonus(t->owner) + 1.0f) * (float)v ) + combat;
v += (pd->townCount == 0) ? 5'000'000 : 5'000'000 / numTowns;
return v;
```

The **first** town is worth a flat 5 000 000; every one after that is worth `5e6 / numTowns`. That
single line explains most of the AI's early-game rush behaviour.

## 4B.3 `hero::AI_town_visit_value` @ `0x52B1E0`

```c
int v = 0;
if (t->type == 8 /*Conflux*/ && (t->built & bit[0x66CE40])) v = magic_university(hero);  // 0x525BF0, below

if (t->built & buildingBit[0] /*Mage Guild*/) {
    if (!hero_has_spellbook(hero)) {
        if (pd->gold >= 500) v += 1000;                       // buy one
    } else {
        for (lvl = 0; lvl < hero->b[0xD0] + 2; ++lvl)          // guild levels the hero can learn
            for (slot = 0; slot < t->b[0x14]; ++slot) {
                SpellID sp = t->d[0x44 + lvl*0x18 + slot*4];
                if (!hero->b[0x3EA + sp]) v += AI_get_spell_value(hero, sp);   // 0x527640
            }
    }
}

// once-per-hero faction special, keyed on the bitmask hero+0x121 indexed by town id
if (!(hero->bits[0x121] & (1 << t->id))) switch (t->type) {
    case 2 /*Tower*/:      if (built) v += hero->d[0x486];                 // value of +1 knowledge
    case 3 /*Inferno*/:    if (built) v += hero->d[0x47E];                 // value of +1 spell power
    case 4 /*Necropolis*/: (nothing)
    case 5 /*Dungeon*/:    if (built) v += ftol(hero->xpValue * 1000.0f);
    case 6 /*Stronghold*/: if (built) v += ftol(expForNextLevel(level) * hero->xpValue);
    case 7 /*Fortress*/:   if (built) v += ftol(expForNextLevel(level) * hero->xpValue);
}
return v;
```

### The Conflux arm — `0x525BF0`, the Magic University

The one branch `AI_town_visit_value` reaches before anything else, and the only town special that
gets its own routine. 176 bytes, called from `0x5253D0`, `0x528040` and `0x52B1E0`.

```c
// __thiscall(hero *this /*ecx*/, int *skills /*edx*/, bool chargesGold /*[ebp+8]*/)
int hero::AI_magic_university_value(int *skills, bool chargesGold)
{
    if (this->d[0x101] >= 8) return 0;                   // already holds 8 secondary skills
    if (chargesGold && gpCurPlayer->gold /*+0xB4*/ < 2000) return 0;

    heroClass *hc = (heroClass*)[0x67DCEC] + this->d[0x30] * 64;   // 64 bytes per class
    int total = 0;
    for (int i = 0; i < 4; ++i) {                        // the university offers exactly 4
        int s = skills[i];
        if (!hc->b[0x18 + s])            continue;       // this CLASS may not learn it
        if (this->b[0xC9 + s] > 0)       continue;       // the hero already has it
        if (!hero::can_learn_skill(this, s, 1)) continue; // 0x524DD0
        total += hero::AI_secondary_skill_value(this, s, 1);   // 0x524690, §4.12
    }
    return total;
}
```

Three things worth recording:

* **The skill count is `hero + 0x101`**, and the cap is a hard 8 — the routine returns 0 rather
  than valuing a skill the hero could never take.
* **The class filter is `g_heroClass[class].b[0x18 + skill]`**, a per-skill "this class may learn
  it" byte inside the same 64-byte class record whose `f[0x08]` §5D.1 uses for the combat-strength
  draw. `hero + 0x30` is the class id, as §4.12 already established.
* **`useArmy` is `1` at both call sites**, consistent with §4.12's finding that no AI call site in
  the image ever passes `false`.

The gold test reads `gpCurPlayer` (`0x69CCB0`) rather than the town's owner, so a Magic University
is priced against **whoever's turn it is**. That is harmless in practice — the routine is only
reached from the AI's own turn — but it is the same `gpCurPlayer` shortcut §4G.1 relies on.

## 4B.4 `type_AI_army_planner` — every troop decision

A ~`0x54`-byte scratch object used for hero↔hero exchanges, garrison pickups and recruitment.
Constructors `0x42CB70`, `0x42CE30`, `0x42CF50`.

| off | meaning |
|---|---|
| +0x00 | `armyGroup*` destination (scratch copy of the receiving army) |
| +0x04 | `armyGroup*` source (scratch copy of the offered troops) |
| +0x08 | mode flag |
| +0x0A | int16 morale of the receiving hero |
| +0x1C | int16 **primary-skill difference** (receiver − giver, clamped ≥ 0) |
| +0x24 | `int* funds` |
| +0x28 | flag |
| +0x30 / +0x34 | offer vector begin / end (12-byte records `{int type; int16* pAvailable; int16 available; int8 flag;}` — see `init_from_town` below) |

### `AI_evaluate_troop_exchange(hero* recv, armyGroup* offered, hero* giver, bool mode)` @ `0x42C4A0`

```c
copy recv->army → dst;  copy offered → src;
this->morale    = hero::get_morale(recv, 0, 0, 1);
this->skillDiff = max(get_primary_skill_sum(recv) - (giver ? get_primary_skill_sum(giver) : 0), 0);
merge duplicate creature types inside dst;
normalise();                                            // 0x42C5B0
int total = 0, gain;
do { gain = take_best_stack(slot_count(src) > 1); total += gain; } while (gain > 0);
return total;
```

### `take_best_stack(bool allowPartial)` @ `0x42C280`

```c
prepare();                                              // 0x42C060 — locate the free/worst dst slot
int best = 0;
for (slot = 0; slot < 7; ++slot) {
    int ct = src.type[slot];  if (ct == -1) continue;
    int n  = src.count[slot];
    int v  = value_of_adding(ct, n, &dstSlot, allowPartial ? 0 : 1);      // 0x42C830
    if (!allowPartial && n > 1) {                       // also try leaving one behind
        int v2 = value_of_adding(ct, n - 1, &dstSlot2, 0);
        if (v2 > v) { v = v2; n = n - 1; dstSlot = dstSlot2; }
    }
    int score = (this->skillDiff * v) / 40;
    if (score > best) { best = score; remember (slot, ct, n, dstSlot); }
}
if (best > 0) perform the move;
return best;
```

**`score = skillDiff × valueOfAdding / 40`.** If `skillDiff` is 0 — the receiving hero is no stronger
than the giver — the score is 0 and **no troops move at all**. That is the rule that makes AI armies
funnel into one main hero and never trickle back out.

### `buy_one_stack(bool reserveFunds)` @ `0x42D420`

```c
prepare();
for (each offer in the 12-byte offer vector) {
    if (offer.available <= 0) continue;
    int n;
    if (offer.free) n = offer.available;
    else {
        cost = creature_cost(offer.type);                       // 0x54E7C0
        if (reserveFunds) AI_player[player].reserve_funds(cost, offer.available);
        n = offer.available;
        for (r = 0; r < 7; ++r) if (cost[r] > 0) n = min(n, funds[r] / cost[r]);
    }
    if (n <= 0) continue;
    int v = value_of_adding(offer.type, n, &dstSlot, 0);
    keep the best;
}
buy the winner, deduct the funds, return its value (0 when nothing is worth buying);
```

`AI_evaluate_purchase` (`0x42D780`, `0x42D690`) just wraps this in the same
`do { … } while (gain > 0)` loop, so the AI keeps buying until nothing improves the army.

### `value_of_adding(creatureType, int16 count, int16* outSlot, bool mustFit)` @ `0x42C830`

The 832-byte core behind every troop decision. Three terms: raw value, an alignment gate, and a
**movement penalty**.

```c
TCreatureTypeTraits* tr = &traits[ct];
int value = tr->AI_value * count;
bool ok   = false;

// ---- 1. alignment ----
int align = (gpGame->d[0x1F698] == 0 && ct in {112,113,114,115} /*elementals*/)
          ? -1 : tr->town /*+0x00*/;
if (this->mode) {                                  // restrict to alignments already present
    bitset allowed = armyGroup::get_alignments(dst);            // 0x44A460
    if (!(allowed & (1 << align))) align = lowest set bit of allowed;
}

// ---- 2. may we mix this alignment in at all? ----
if (!this->alignSeen[0x0F + align] && armyGroup::slot_count(dst) > 0) {
    int maxAlign = (gpGame->d[0x1F698] != 0 || ct is not an elemental)
                 ? (tr->town == 4 /*Necropolis*/ ? 2 : 1) : 1;
    if (armyGroup::count_alignments(dst, mode) + this->morale < maxAlign) {   // 0x44AE60
        int keep = 0;                                // value of the stacks we would have to keep
        for (slot = 0; slot < 7; ++slot) {
            int t2 = dst.type[slot];  if (t2 == -1) continue;
            if ((traits[t2].flags & 0x20000) || t2 == 78 || t2 == 79) continue;  // alignment-free
            keep += traits[t2].AI_value * dst.count[slot];
        }
        if (!((tr->flags & 0x20000) || ct == 78 || ct == 79)) keep += value;
        ok = (keep >= 10 * value);                   // the mix must be worth ≥ 10× the newcomer
    }
}

// ---- 3. movement penalty ----
int slowest = 20;
for (slot = 0; slot < 7; ++slot)
    if (dst.type[slot] != -1) slowest = min(slowest, traits[dst.type[slot]].speed /*+0x50*/);
if (slowest > tr->speed) {                           // the newcomer would slow the whole hero down
    int armyVal = armyGroup::get_AI_value(dst) + 500;
    value += ftol( (double)movementForSpeed[tr->speed] * armyVal
                 / (double)movementForSpeed[slowest] - armyVal );      // negative
}
// ---- 4. pick the destination slot — see below ----
```

`movementForSpeed[]` is the runtime table at **`0x698A98`** — the adventure-map movement points a
hero gets for a given slowest-stack speed (also read by `hero::get_movement`, `0x4E4C69`). So the
penalty is exactly **the loss of adventure-map movement, priced as that fraction of the whole army's
value (+500)**. It is why a fast AI hero will walk past a pile of slow creatures it could otherwise
afford, and why the value can come out negative and cause `take_best_stack` to leave troops behind.

The `keep >= 10 * value` test is the alignment gate: the AI will only introduce a second town's
creatures when the army it already has is worth at least ten times the newcomer — Necropolis is the
one faction allowed two alignments (`maxAlign = 2`), which is the undead-morale exemption.
Creature types 78 and 79 and anything with trait flag `0x20000` are alignment-free and never count.

#### Step 4 — choosing the destination slot (`0x42CA9E` onward)

The tail of `value_of_adding` decides *where* the creatures would go and adjusts the value for what
that costs. `outSlot` is pre-set to `-1`.

```c
*outSlot = -1;

// (a) already have this creature type? → merge, free of charge
for (i = 0; i < 7; i++)
    if (dst->type[i] == ct) {
        *outSlot = i;
        return mustFit ? -1 : value;      // note: -1, not 0
    }

// (b) new type. If the alignment gate fired, or the caller demands a guaranteed
//     fit, skip the free-slot search entirely and go straight to displacement.
if (!ok /*the `keep >= 10*value` result*/ && !mustFit) {
    if (armyGroup::slot_count(dst) < 6 || this->src /*+0x04*/ == NULL)
        for (i = 0; i < 7; i++)
            if (dst->type[i] == -1) { *outSlot = i; return value; }
}

// (c) displace something
int16 slot = pick_slot_to_displace(this, (tr->flags /*+0x10*/ >> 2) & 1, ok);   // 0x42C690
*outSlot = slot;
if (slot < 0) return 0;                   // nothing may be displaced ⇒ worthless

int victim = dst->type[slot];
return value - traits[victim].AI_value /*+0x40*/ * dst->count[slot] /*+0x1C*/;
```

Three things a reimplementation has to get right:

* **Merging is free.** An existing stack of the same type absorbs the newcomer at full `value` with
  no penalty at all — which, combined with the `skillDiff × v / 40` scaling in `take_best_stack`, is
  why AI heroes consolidate into a few large same-type stacks rather than spreading out.
* **The `mustFit` early return is `-1`, not `0`.** `take_best_stack` compares against `best = 0`, so
  a `-1` is discarded exactly like a refusal — but `buy_one_stack` and any other caller that treats
  "negative" and "zero" differently will see the distinction.
* **The free-slot search is skipped when the army already holds 6 stacks *and* a source army
  exists.** With a source present the AI would rather evict a weak stack than fill its last slot; with
  no source (`this->src == NULL`, i.e. recruitment rather than exchange) it will happily use slot 7.

### `pick_slot_to_displace(bool newcomerHasFlag2, bool checkAlign)` @ `0x42C690`

`__thiscall`, `ret 8`, returns the slot index to evict or `-1`.

```c
int nFlagged = 0;
for (i = 0; i < 7; i++) {
    int t = dst->type[i];
    if (t != -1 && (traits[t].flags & 4)) nFlagged++;
}
bool protectClass  =  newcomerHasFlag2 && nFlagged >  3;
bool keepLastOfClass = !newcomerHasFlag2 && nFlagged == 1;

int best = -1, bestVal = 0;
for (slot = 0; slot < 7; slot++) {
    int t = dst->type[slot];
    if (t == -1) continue;
    TCreatureTypeTraits* tr = &traits[t];

    if (checkAlign) {
        int align = (gpGame->d[0x1F698] == 0 && t in {112,113,114,115}) ? -1 : tr->town;
        if (this->mode /*+0x08*/) {
            bitset* a = armyGroup::get_alignments(dst);        // 0x44A460
            if (!(a & (1 << align))) align = lowest set bit of a;
        }
        if (this->alignSeen[0x0F + align] != 1) continue;      // not an accepted alignment
    }

    if (protectClass    && !(tr->flags & 4)) continue;   // only evict members of the class
    if (keepLastOfClass &&  (tr->flags & 4)) continue;   // never evict the last one

    int score = dst->count[slot] * tr->AI_value;
    if (best < 0 || score < bestVal) { bestVal = score; best = slot; }
}
return best;
```

The victim is always the **lowest total-value stack** among the eligible ones — `count × AI_value`,
the same product used everywhere else in the planner.

`traits + 0x10` is confirmed as the creature flags dword: `value_of_adding` itself tests bit `0x20000`
on it at `0x42C9A3`/`0x42C9D3` for the alignment-free exemption. **Bit 2 (`& 4`)** is the flag this
routine protects. It is tested in only 12 places image-wide — `0x42DB20` (×4), `0x42C690` (×3),
`0x42D8E0` (×2), `0x524690`, `0x5F5B30`, `0x61FBCA` — all but the last two inside the army planner,
where `0x524690` uses it to keep a *separate running subtotal* of `count × AI_value` for stacks that
have it. **Bit 2 is SHOOTING**, verified in §4E: the flags dword turns out to be static in the image, and
bit 2's membership set matches `crtraits.txt`'s `Shots > 0` column exactly across all 150 creatures.
The behaviour fits — cap the army at 3–4 shooting stacks, and never throw away the only one.

Note that `checkAlign` is passed the **result of the alignment gate**, not a caller flag. So when the
AI is deliberately introducing a second town's creatures, the eviction candidates are restricted to
alignments it has already accepted — it will not solve an alignment problem by creating a new one.

### `AI_army_planner::init_from_town(town*)` @ `0x42D1B0`

`__fastcall(planner* this, playerData* /*edx*/, town*)`. Builds the **offer list** — the set of
creatures this town can currently sell — into the vector at `this + 0x30 .. +0x38`.

```c
offers.clear();                                   // this + 0x2C is the vector header

for (int slot = 0; slot < 14; slot++) {
    int16 avail = town->w[0x16 + slot*2];
    if (avail <= 0) continue;                     // nothing buildable/available in this slot

    int faction = (int8)town->b[0x04];
    int ct = g_gDwellingType[faction*14 + slot];  // [0x6747B4]

    offers.push_back(offer{
        .type      = ct,                          // +0x00
        .pAvailable= &town->w[0x16 + slot*2],     // +0x04  ← pointer back into the town
        .available = avail,                       // +0x08
        .flag      = 0                            // +0x0A
    });
}
```

Two corrections fall out of this:

* **`town + 0x16` is `int16 available[14]`, not `int32[7]`.** Both readings cover the same 28 bytes —
  §4B.10a saved it with `rep movsd; ecx=7` — but the element type and count matter. There are
  **14 dwelling slots** (7 tiers × base/upgraded), and `g_gDwellingType` (`0x6747B4`) is indexed
  `faction*14 + slot`.
* **The offer record's second field is a pointer**, not padding. §4B.4 listed the 12-byte record as
  `{int type; int _; int16 available; int8 free;}`; the `int _` is `int16* pAvailable`, aimed at the
  town's own counter. That is what lets `recruit` decrement the town in place, and it is why
  §4B.10a has to snapshot and restore `town + 0x16` around its simulation.

There is **no filtering and no ordering** here: every slot with stock is offered, in dwelling order.
All the selection pressure lives downstream in `value_of_adding` / `buy_one_stack`.

### `AI_army_planner::recruit(...)` @ `0x42D690`

`__thiscall(planner* this, armyGroup* dst, int16 morale, armyGroup* src, int* funds,
bool reserveFunds, bool flag)`, `ret 0x18`. Six stack args. Called from §4.13, §4B.10a and three
town-screen paths.

```c
this->dst /*+0x00*/    = dst;
this->src /*+0x04*/    = src;
this->b[0x08]          = flag;
this->morale /*+0x0A*/ = morale;
this->funds /*+0x24*/  = funds;                   // → playerData + 0x9C, the live resource array

// 1. fold duplicate creature types in the destination together
for (int i = 1; i < 7; i++) {
    int t = dst->type[i];
    if (t == -1) continue;
    for (int j = 0; j < i; j++)
        if (dst->type[j] == t) {
            dst->count[j] += dst->count[i];
            armyGroup::remove_slot(dst, i);       // 0x44AB60
            break;
        }
}

// 2. spend
normalise(this);                                  // 0x42C5B0
while (buy_one_stack(this, reserveFunds) > 0)     // 0x42D420 — fully specified in §4B.4
    ;

// 3. write the leftovers back into the town
for (offer& o : offers)
    *o.pAvailable = o.available;                  // +0x04 ← +0x08
}
```

So the whole recruitment behaviour is: **offer everything the town has, dedupe the hero's army, then
greedily call `buy_one_stack` until it stops returning a positive gain.** `buy_one_stack` is where
the money decision actually happens, and §4B.4 gives it in full — so with `init_from_town` and this
routine the recruitment chain is closed end to end.

Step 3 is the reason a simulated hire (§4B.10a) must restore `town + 0x16`: `recruit` really does
drain the town's dwellings, through the pointers `init_from_town` stored.

### The remaining planner primitives

These are the container mechanics §4B.4's pseudo-code leans on. None carries a valuation weight, but
they are given here so every call in this report resolves.

**`armyGroup::slot_count()` @ `0x44ACC0`** — the whole function:

```c
int n = 0;
for (int i = 0; i < 7; i++) if (grp->type[i] != -1) n++;
return n;
```

**`merge_duplicate_stacks(armyGroup*)` @ `0x42D870`** — folds repeated creature types together,
lowest slot wins:

```c
for (int i = 1; i < 7; i++) {
    int t = grp->type[i];
    if (t == -1) continue;
    for (int j = 0; j < i; j++)
        if (grp->type[j] == t) {
            grp->count[j] += grp->count[i];
            armyGroup::remove_slot(grp, i);        // 0x44AB60
            break;
        }
}
```

**`prepare(planner*)` @ `0x42C060`** — run at the head of `take_best_stack` and `buy_one_stack`:

```c
this->w[0x0C] = 0x44ABB0(this->dst, &this->b[0x0E]);   // locate the free / weakest slot
if (this->b[0x08] /*mode*/)
    for each alignment i with this->b[0x0F + i] set          // the alignSeen[] array of §4B.4
        … refresh from armyGroup::get_alignments(dst) (0x44A460) …
```

`planner + 0x0C` is the chosen destination slot and `planner + 0x0F` is the `alignSeen[]` array that
`value_of_adding` and `pick_slot_to_displace` both read.

### `normalise(planner*)` @ `0x42C5B0`

The counterpart of `take_best_stack`: it pushes stacks the destination should not keep **back into
the source group**, freeing destination slots.

```c
if (!this->src)                     return;   // nothing to push back into
if (slot_count(this->src) == 7)     return;   // source is full
if (slot_count(this->dst) == 1)     return;   // never strip the last stack

for (int i = 0; i < 7; i++) {
    int t = this->dst->type[i];
    if (t == -1) continue;
    if (slot_count(this->src) == 7) return;   // re-checked every iteration
    if (slot_count(this->dst) == 1) return;
    … value_of_adding (0x42C830) decides; armyGroup::remove_slot (0x44AB60) commits …
}
```

The two guards re-evaluated inside the loop are the important part: the routine will **never empty
the destination** and **never overflow the source**, so a call is always safe regardless of how the
two groups are shaped on entry.

### `writeback(armyGroup*)` @ `0x42D8E0` — the army slot-layout rule

**This section previously described `0x42D8E0` as bookkeeping. That was wrong.** It is the routine
that decides **which slot each stack ends up in**, and since battlefield starting positions follow
slot order, it directly shapes every AI battle line.

Three phases.

```c
// 1. drain the group into a vector of 12-byte records
struct rec { int type; int speed; int16 count; };     // +0x00, +0x04, +0x08
for (int i = 0; i < 7; i++) {
    int t = grp->type[0];
    if (t == -1) break;
    sorted.insert(rec{ t, traits[t].speed /*+0x50*/, grp->count[0] });   // 0x4347A0
    armyGroup::remove_slot(grp, 0);                                       // 0x44AB60
}

// 2. sort — MSVC's std::sort idiom
int n = (end - begin) / 12;
if (n <= 16) insertion_sort(begin, end);              // 0x435310
else { partition(begin, end);                          // 0x434E80
       insertion_sort(begin, begin + 0xC0);            // 0xC0 = 16 records
       for (p = begin + 0xC0; p != end; p += 12) push_heap(p); }   // 0x435020

// 3a. shooters first, walking the sorted list BACKWARDS, into alternating slots
int slot = 0;
for (int k = n - 1; k >= 0; k--) {
    if (!(traits[sorted[k].type].flags /*+0x10*/ & 4)) continue;   // SHOOTING, §4E
    armyGroup::set_slot(grp, sorted[k].type, sorted[k].count, slot);   // 0x44ACE0
    slot += 2;
    if (slot >= 7) slot = 1;                          // 0, 2, 4, 6 → 1, 3, 5
}

// 3b. everyone else, walking FORWARDS, into the first free slot
int i = 0;
for (int k = 0; k < n; k++) {
    if (traits[sorted[k].type].flags & 4) continue;
    while (grp->type[i] != -1) i++;
    armyGroup::set_slot(grp, sorted[k].type, sorted[k].count, i);
}
```

Three behaviours a reimplementation has to reproduce:

* **Shooters are deliberately spread out** — slots 0, 2, 4, 6, then wrapping to 1, 3, 5. They are
  never placed adjacent while a gap exists. This is why AI armies have their characteristic
  interleaved layout, and why AI shooters are awkward to block as a group.
* **The two passes run in opposite directions.** Shooters are taken from the *end* of the sorted
  order, non-shooters from the *start*. With the same comparator, that puts the two classes at
  opposite ends of the sort key.
* **The sort key record carries `traits[type].speed`** (`+0x50`), recorded per stack in phase 1.

The `0xC0` threshold in phase 2 is 16 records × 12 bytes — the standard MSVC `_Sort` cutover, not an
AI constant.

## 4B.4a Army-planner leaves that were only named

### `armyGroup::count_alignments` @ `0x44ABB0`

```c
int armyGroup::count_alignments(uint8 *out /*[ebp+8]*/)
{
    uint8 local[10];
    uint8 *c = out ? out : local;
    memset(c, 0, 10);
    for (int i = 0; i < 7; ++i) {
        int t = this->type[i];  if (t == -1) continue;
        if (traits[t].flags /*+0x10*/ & 0x40) continue;        // exempt from alignment penalties
        int a = (gpGame->d[0x1F698] == 0 && t >= 0x70 && t <= 0x73)
              ? -1                                             // the four base elementals
              : traits[t].alignment /*+0x00*/;
        ++c[a + 1];                                            // −1 lands in bucket 0
    }
    int n = 0;
    for (int a = 0; a < 10; ++a) if (c[a]) ++n;
    return n;
}
```

Two corrections to the obvious reading: creatures with flag `0x40` are dropped **before** counting,
but the `−1` bucket **does** count as an alignment — a mixed army of neutrals and elementals scores
1, not 0. `type_AI_army_planner::recompute` (`0x42C060`) caches the result in `plan->w[0x0C]` and the
10-byte bucket array in `plan + 0x0E`.

`0x44AE60` — the routine NH3API's symbol list points at for this name — is the *consumer*: it calls
`count_alignments` and `hero::get_morale`, and turns the bucket array into a morale modifier.

### `normalise` @ `0x42C5B0` — and the predicate it applies

```c
void type_AI_army_planner::normalise()
{
    if (!this->dst) return;
    if (armyGroup::slot_count(this->dst) == 7) return;     // 0x44ACC0 — destination is full
    if (armyGroup::slot_count(this->src) == 1) return;     // source is down to its last stack
    for (int i = 0; i < 7; ++i) {
        int ct = this->src->type[i];  if (ct == -1) continue;
        if (armyGroup::slot_count(this->dst) == 7) return;
        if (armyGroup::slot_count(this->src) == 1) return;
        int n = this->src->count[i];
        armyGroup::remove_slot(this->src, i);              // 0x44AB60 — take it out first
        this->recompute();                                 // 0x42C060
        int16 slot;
        if (value_of_adding(ct, n, &slot, 0) > 0) continue; // 0x42C830 — keep the move
        armyGroup::set_slot(this->dst, ct, n, -1);         // 0x44ACE0 — put it back
        …
    }
}
```

**The predicate is strictly `> 0` to keep**, so `value_of_adding == 0` means "give it back". The
stack is removed from the source *before* the evaluation, which is what makes the question
"is this stack worth having at all?" rather than "is it worth adding on top of itself?".

### `plan.armyValueAfter`

It is the planner field at **`plan + 0x18`**, maintained as the planner buys and exchanges. `AI_town_recruit_value`
(`0x52B090`) reads it at `[ebp-0x3C]` where the planner object lives at `[ebp-0x54]`, and the guard is

```c
if (moveLimit >= 400 && plan.d[0x18] < armyGroup::get_AI_value(&hero->army) / 3) return 0;
```

`/3` is the `0x55555556` magic with no shift.

### The public wrapper — `value_of_adding` for a single offer, `0x42D780`

Used by Refugee Camp, creature dwellings and town recruitment alike:

```c
int AI_value_of_creature_offer(planner *p, armyGroup *heroArmy, int16 morale,
                               armyGroup *other, int resources[7], bool angelicAlliance)
{
    armyGroup a = *heroArmy;                     // work on copies — nothing is committed
    armyGroup b;  armyGroup::ctor(&b);           // 0x44AA80
    int res[7];   memcpy(res, resources, 28);
    p->src = &a;  p->morale = morale;  p->resources = res;
    p->angelicAlliance = angelicAlliance;
    p->dst = other ? (b = *other, &b) : &b;

    // merge duplicate creature types inside the copy first
    for (int i = 1; i < 7; ++i)
        for (int j = 0; j < i; ++j)
            if (a.type[j] == a.type[i]) { a.count[j] += a.count[i]; armyGroup::remove_slot(&a, i); }

    p->normalise();                              // 0x42C5B0
    int total = 0, v;
    do { v = p->buy_one_stack(0); total += v; } while (v > 0);   // 0x42D420
    return total;
}
```

### `playerData::AnyHeroHasArtifact(pd, 0x81)` — artifact `0x81` identified

`0x81` is **129 = Angelic Alliance**. It is passed as the planner's `angelicAlliance` flag by
`AI_town_recruit_value`, `AI_hero_visits_town`, `town::garrison` and the Refugee Camp handler, and it
is what tells the planner to ignore alignment mixing entirely. `0x4BACB0` scans the player's field
heroes and then the garrisoned heroes of every town, so a single hero anywhere in the kingdom
carrying the assembled set switches the behaviour on for all of them.

## 4A.4a `town::get_buildable_mask` @ `0x5C0F20` — it does **not** consider affordability

```c
uint64 town::get_buildable_mask(town *t)
{
    uint64 built = *(uint64*)&t->d[0x158];
    uint64 mask  = 0;
    for (int b = 0; b < 44; ++b) {
        uint64 prereq = g_buildingPrereq[t->faction * 44 + b];    // 0x697798, 8 bytes each
        if (gpGame->b[0x1F69D] && b == 32 && t->faction == 0)
            prereq &= ~g_maskAt(0x66CE18);                       // one campaign-flag special case
        uint64 bit = g_buildingBit[b];                           // 0x66CD98, 8 bytes each
        if (built & bit)                continue;                // already standing
        if ((built & prereq) != prereq) continue;                // prerequisites unmet
        mask |= bit;
    }
    mask &= *(uint64*)&t->d[0x160];                              // the map's per-town allow-list
    if (t->b[8] == 0xFF)               mask &= ~g_maskAt(0x66CDC8);   // no Grail dug here
    if (0x4B9F40(&playerData[t->owner])) mask &= ~g_maskAt(0x66CE00);
    return mask;
}
```

There is no resource test anywhere in it. **`AI_build_one_building` therefore evaluates buildings it
cannot currently pay for**, and that is deliberate: the value it computes for an unaffordable
building is what drives `reserve_funds` and, through it, the trade planner. A reimplementation that
filters the mask by affordability will stop the AI from ever saving up.


## 4B.5 `AI_town_recruit_value` @ `0x52B090`

```c
type_AI_army_planner plan(hero->owner, t);                          // 0x42CE30
hero* visitor = (t->visitingHeroId >= 0) ? &heroes[t->visitingHeroId] : 0;
bool  flag    = playerData::get_flag(pd, 0x81);
int exchangeVal = plan.AI_evaluate_troop_exchange(hero, town::garrison(t, visitor, flag));
int buyVal      = plan.AI_evaluate_purchase(&hero->army,
                       town::available_creatures(t, pd->resources, flag),
                       hero::get_morale(hero, 0, 0, 1));
if (moveLimit >= 400 && plan.armyValueAfter < armyGroup::get_AI_value(&hero->army) / 3)
    return 0;                                   // refuse an exchange that guts the hero
return buyVal + exchangeVal / 2;
```

## 4B.6 Ally resource gifting — `type_AI_player::AI_offer_resources_to_ally` @ `0x429110`

Called once per turn from `manage_kingdom`, for every other player on our team.

```c
for (r = 0; r < 7; ++r) {
    int surplus = AI_player.resource_supply[r] - AI_player.resource_demand[r];
    if (surplus <= 0) { give[r] = surplus; continue; }
    int mine = pd->resources[r], theirs = allyPd->resources[r];
    give[r] = min( (mine - theirs) / 2, surplus );          // level the two players out
    give[r] = min( give[r], mine - (r == GOLD ? 10000 : 20) );   // keep a reserve
    give[r] -= AI_player.reserved_funds[r];
    if (give[r] < (r == GOLD ? 1000 : 5)) give[r] = 0;      // don't bother with dribbles
    if (give[r] < 5 * theirs)             give[r] = 0;      // only when the ally is nearly out
}
transfer, then log "Warning!  AI player has %i %s." for any resource that went negative;
```

Three thresholds worth reproducing: the reserve (**10 000 gold / 20 of everything else**), the
minimum gift (**1000 gold / 5 units**), and the `give ≥ 5 × theirs` trigger — allied AIs only bail
each other out when the recipient is genuinely broke, not to top them up.

## 4B.7 Reachability and the only inter-hero coordination — `0x42F570`

`hero::AI_build_reachability(searchArray* sa, int16* visited, int range, int mode)`, called by
`AI_scan_objects` before it enumerates objects.

```c
searchArray tmp;                                        // local scratch, ctor 0x4B1370
mapCoords start = pack(hero->x, hero->y, hero->z);
int seed = sa->valueMap /*+0x6C*/ ? sa->valueMap[cellIndex(start)] : 0;

// 1. our own reachability, bounded by movement points
searchArray::compute(sa, hero, start, /*no goal*/, visited,
                     hero->flags[0x105] >> 18 & 1 /*flying / water-walk*/,
                     range, hero->mp /*+0x4D*/, 0);     // 0x56B440

// 2. for every OTHER hero we own
for (i = 0; i < pd->heroCount; ++i) {
    hero* other = &gpGame->heroes[pd->heroIds[i]];      // pd + 0x08 + i*4
    if (other == hero) continue;
    if (!(sa->cells[cellIndex(other->pos)].flags & 1)) continue;   // we can't reach it anyway

    mapCoords goal; int handicap;
    if (!coords_valid(&other->AI_dest /*+0x35,+0x39,+0x3D*/)) {
        goal = other->pos;  handicap = 0;               // it has no destination yet
    } else {
        goal = other->AI_dest;
        int est = other->w[0x41];                       // its stored cost-to-goal estimate
        handicap = (est > other->mp) ? 0 : est - other->mp;
    }
    // a SECOND Dijkstra, from that hero, with ITS movement allowance
    searchArray::compute(&tmp, other, other->pos, goal, other->maxMp /*+0x49*/,
                         onBoat, 2, other->maxMp, 0);
    // walk tmp's reachable-cell list and suppress those cells in `visited`
}
```

**This is the adventure AI's only inter-hero coordination.** Beyond the shared `value_map`, the sole
mechanism is: before a hero scans for objects, the AI runs a full pathfind from every *other*
friendly hero and strikes out the cells that hero already covers, handicapped by how far it still is
from its own goal. There is no assignment, no negotiation and no re-planning — which is why two AI
heroes rarely fight over the same object yet still wander across each other constantly.

`searchArray::compute` (`0x56B440`) is the engine's shared Dijkstra, not AI code — the human
movement UI calls it too (`0x4194A0`). Its signature as the AI uses it is
`(searchArray* this, hero*, mapCoords start, mapCoords goal, int limit, bool onBoat, int mode,
int movementPoints, int)`. It writes per-cell records of stride `0x1E` into `this->cells` (`+0x24`),
with the movement cost at `+0x18`, the turn count at `+0x1A` and reachability in bit 0 of `+0x04`,
and appends the reachable cells to the vector at `+0x5C`/`+0x60`. Those four fields are everything
the AI reads back out of it.

## 4B.8 Walking the path — `hero::AI_reevaluate_step` @ `0x42F980`

Called from `AI_move_to_destination` for each cell along the chosen route. It is what lets the AI
pick up things it passes over and abandon a march that has become suicidal.

```c
mapCoords here = step->coord;
TObjectCell* oc = map->GetObjectCell(here);                    // 0x412BD0
int objType = oc->d[0x1E];

if ((oc->b[0x0D] & 0x10)                                       // an object stands here
 && objTypeTable[0x660428 + objType*16] != 0                   // of an interactable type
 && cell_is_explored(here)) {                                  // 0x4F79B0 & mask 0x69CCC4
    step->moveCost /*+0x08*/ -= sa->cells[here].cost /*+0x18*/;   // charge this step
}

// running total across the whole march
if (runningValue >= -500'000'000)
    runningValue += hero::AI_object_value(&limit, here);       // 0x528040
if (here == hero->AI_dest) stop;                               // arrived
```

Two rules fall out of this:

* **The AI re-prices every object it walks over**, using the same `AI_object_value` the destination
  chooser used — so a resource pile or a windmill on the way is collected as a side effect rather
  than being planned for.
* **The march aborts as soon as the running total drops below −500 000 000** — the same "certain
  defeat" sentinel the danger map uses (§4.6). A hero that walks into newly-revealed danger
  mid-route stops rather than completing the move it committed to at the start of the turn.

The object-type table at `0x660428` (16-byte stride, indexed by `MapObjectType`) is the same one
`AI_choose_destination` consults before deciding whether to smear an object's value across the map;
a zero entry means "walk over it, never target it".

## 4B.9 Hiring heroes — `type_AI_player::AI_hire_hero` @ `0x431360`

Called once per turn from `manage_kingdom`. Despite sitting between the two build-planning calls,
this is the tavern decision, not a build-order routine.

```c
playerData* pd = &gpGame->playerData[this->player];
if (pd->heroCount /*+0x01*/ >= 8)                      return false;   // engine cap
if (pd->gold     /*+0xB4*/ <  2500 /*[0x67814C]*/)     return false;
if (pd->heroCount >= maxAIHeroes[difficulty])          return false;   // 0x660518

// how many heroes do the HUMAN players collectively own?
int humanHeroes = 0;
for (p = 0; p < 8; ++p)
    if (!gpGame->b[0x1F636 + p] && !is_computer(p))
        humanHeroes += gpGame->playerData[p].heroCount;
if (pd->heroCount > 0 && humanHeroes >= humanHeroCap[difficulty]) return false;   // 0x66052C

// score each of the two heroes the tavern is offering (pd->d[0x28], pd->d[0x2C])
for (candidate in { pd->d[0x28], pd->d[0x2C] }) {
    if (candidate == -1) continue;
    int total = 0;
    for (slot = 0; slot < 64; ++slot) {                 // equipped artifacts, hero+0x1D4, 8 B each
        TArtifact art = offered->equip[slot];
        if (art == -1) continue;
        int best = 10;                                  // floor
        for (each hero h we already own)
            best = max(best, total_artifact_value(h, art, -1));   // 0x4339E0
        total += best;
    }
    for (slot = 0; slot < 19; ++slot) { same over the backpack, hero+0x12D }
    score[candidate] = total;                            // plus get_primary_skill_sum weighting
}
hire the better candidate → 0x431800 (which also hands its artifacts to the right hero);
```

Two clean difficulty tables fall out:

| difficulty | 0 Easy | 1 Normal | 2 Hard | 3 Expert | 4 Impossible |
|---|---|---|---|---|---|
| **max heroes the AI will own** (`0x660518`) | 2 | 3 | 4 | 5 | 6 |
| **human-hero cap that stops AI hiring** (`0x66052C`) | 8 | 11 | 14 | 17 | 20 |

The second table is the interesting one: once the *human* players between them own that many heroes,
an AI that already has at least one hero **stops buying more entirely**. It is a deliberate brake on
AI hero spam that scales with how far ahead the human is.

Note also what the score is made of: a tavern hero is priced almost entirely by **what its artifacts
would be worth to the best hero we already own** (floor 10 per artifact), not by its own stats. That
is why AI players buy and immediately dismiss heroes carrying good relics.

## 4B.10 Hire execution — `type_AI_player::AI_buy_hero` @ `0x431800`

`__fastcall(ecx = playerIdx, edx = hero* candidate)` → `bool`. Called only from `AI_hire_hero`
(§4B.9) once a tavern hero has been picked. It re-prices the candidate from scratch, decides
*which town* to buy them in, builds a Tavern first if it has to, and performs the purchase.
Returns `false` at five distinct points; the caller treats `false` as "give up on this hero".

### Step 1 — price the candidate's baggage

```c
playerData* pd = &gpGame->d[0x20AD0 + playerIdx*0x168];
int total = 0;

// (a) 64 backpack slots, 8 bytes each
for (int s = 0; s < 64; s++) {
    int artId = candidate->d[0x1D4 + s*8];
    int best  = 0;
    if (artId != -1) {
        best = 10;                                   // floor: never worth less than 10
        for (int h = 0; h < pd->b[0x01]; h++)        // every hero we already own
            best = max(best, hero::total_artifact_value(   // 0x4339E0
                                 GetHero(pd->d[0x08 + h*4]), artId, -1, /*flag*/0));
    }
    total += best;
}

// (b) 19 equipped slots, 8 bytes each
for (int s = 0; s < 19; s++) {
    struct { int artId; int sub; } a = { candidate->d[0x12D + s*8], -1 };
    total += AI_get_value_of_artifact(&a, playerIdx);   // 0x433AA0
}
```

Two different valuators, deliberately. The **backpack** is priced by *transfer value* — what the
artifact would be worth if handed to the best hero we already own — because that is what will
actually happen to it. The **equipped** slots are priced by the player-generic
`AI_get_value_of_artifact`. Note the asymmetric floor: a backpack artifact is never worth less
than 10, an equipped one can be worth 0.

### Step 2 — add the army, valued through the AI's own resource prices

```c
for (int slot = 0; slot < 7; slot++) {
    int type = candidate->d[0x91 + slot*4];          // creature type, -1 = empty
    if (type == -1) continue;
    int count = candidate->d[0xAD + slot*4];
    for (int r = 0; r < 7; r++)
        total = ftol( (double)creatureTraits[type].cost[r]    // traits + 0x20 + r*4
                    * pd->resValue[r]                          // pd + 0x128 + r*8, double
                    * (double)count
                    + (double)total );
}
```

So a tavern hero's army is worth exactly what it would cost to *buy* the same creatures, priced
with this player's current resource valuations (§4A). It does **not** use `AI_value_of_combat` —
the AI never asks whether the army is any good in a fight, only what it cost.

Confirmed struct offsets from this function:

| Offset | Type | Meaning |
|---|---|---|
| `hero + 0x91` | `int[7]` | creature type per slot, `-1` = empty |
| `hero + 0xAD` | `int[7]` | creature count per slot |
| `hero + 0x12D` | `8 bytes × 19` | equipped artifact slots |
| `hero + 0x1D4` | `8 bytes × 64` | backpack slots |
| `playerData + 0x01` | `int8` | hero count |
| `playerData + 0x08` | `int[]` | hero ids |
| `playerData + 0x3E` | `int8` | town count |
| `playerData + 0x40` | `int8[]` | town ids |
| `playerData + 0x9C` | `int[7]` | resources on hand (gold at `+0xB4`) |
| `playerData + 0xE2` | `int8` | **is computer-controlled** (`is_computer`, `0x4CE940`) |
| `creatureTraits + 0x20` | `int[7]` | resource cost of one creature |
| `gpGame + 0x21614` | `town*` | town array base, stride 360 |

### Step 3 — the affordability gate

```c
int nHeroes = pd->b[0x01];
int threshold = ftol( (double)nHeroes * pd->resValue[6] * (double)TAVERN_HERO_COST );
                                        // pd + 0x158 = gold value; [0x67814C] = 2500

if (threshold > total && pd->d[0xB4] < nHeroes * TAVERN_HERO_COST)
    return false;                                   // (1)
```

`[0x67814C]` is verified as **2500** — the tavern price. The threshold scales with how many heroes
we already own: the *n*-th hero must be worth `n × 2500` in gold-value units. An AI with five heroes
demands a candidate carrying 12 500 gold-equivalent of loot. The `||` escape means a player who is
simply *rich* (at least `n × 2500` actual gold in the treasury) buys anyway regardless of value.

`threshold` also becomes the initial `best` score for step 4 — so a town must beat the gate, not
merely tie it.

### Step 4 — choose the town

```c
const uint64 TAVERN_BIT = 1ull << 5;      // [0x66CDC0]=32, [0x66CDC4]=0 — verified

town* bestTown = NULL;
int   best     = threshold;

for (int t = 0; t < pd->b[0x3E]; t++) {
    int id = pd->b[0x40 + t];
    town* tn = (id == -1) ? NULL : &((town*)gpGame->d[0x21614])[id];

    if (tn->d[0x10] >= 0) continue;                 // a hero already stands here

    int score = total;
    if ((tn->buildings /*+0x158, 64-bit*/ & TAVERN_BIT) == 0) {
        if (!town::CanBuild(tn, 5))       continue;         // 0x5C0D20
        if (!town::CanAfford(tn, 5))      continue;         // 0x461130
        score -= AI_value_of_cost(playerIdx,                 // 0x526CC0
                     town::GetBuildingCost(tn, 5));          // 0x5C1080
    }

    score += AI_hero_fit_at_town(tn, candidate, &scratch);   // 0x431BD0, §4B.10a

    if (score > best) { best = score; bestTown = tn; }
}
if (!bestTown) return false;                        // (2)
```

The Tavern is building **5** (mask `1<<5`, resolved from the two halves of the 64-bit built-mask at
`0x66CDC0`/`0x66CDC4`). A town without one is still a candidate — the AI simply charges itself the
Tavern's construction cost, converted to value units by the same routine that prices building plans
in §4A. That is why AI players sometimes build a Tavern on the same turn they hire.

`town + 0x10` is the occupying-hero id; a town with a hero standing in it is skipped outright,
because the new hero would have nowhere to appear.

### Step 5 — commit

```c
if ((bestTown->buildings & TAVERN_BIT) == 0) {
    if (!town::Build(bestTown, 5)) return false;    // (3)  0x5BF3C0
    if (pd->d[0xB4] < TAVERN_HERO_COST) return false; // (4) tavern ate the gold
}
town::HireHero(bestTown, candidate, playerIdx);     // 0x5C12E0
return true;
```

Note failure (4): the Tavern is built **first and unconditionally**, and if that construction drops
the treasury below 2500 the hire is abandoned — but the Tavern stays built. This is a real
behavioural quirk worth reproducing: an AI can spend its whole turn's gold on a Tavern and then not
buy the hero it built the Tavern for. The gold check uses the raw resource, not the valuation.

`0x5C12E0` is the shared hire routine (also called from `0x5D82B0`, the human tavern UI): it
deducts 2500 from `playerData + 0xB4`, places the hero, and wires it into the player's hero list.

## 4B.10a Per-town term — `town::AI_hero_arrival_value` @ `0x431BD0`

`__thiscall(town* this, hero* cand /*edx*/, searchArray* scratch)` → `int`, `ret 4`. 1 608 bytes,
called only from `AI_buy_hero`. This is the most elaborate piece of speculative execution in the
whole adventure AI: it **actually performs the hire on the live game state**, measures what the map
looks like afterwards, and then rolls everything back.

### Snapshot / restore set

Four regions are saved on entry and memcpy'd back at the very end:

| Saved | Bytes | Restored at |
|---|---|---|
| `cand + 0x91` — the hero's army (`int type[7]` + `int count[7]`) | 56 | `0x432193` |
| `pd + 0x9C` — the player's 7 resources | 28 | `0x4321A3` |
| `town + 0x16` — `int16 available[14]`, the dwelling stock (§4B.4) | 28 | `0x4321B6` |
| `cand->b[0x22]` — the hero's owner, reset to `0xFF` | 1 | `0x43218F` |

The town garrison is *copied* rather than saved (`town::GetGarrison` @ `0x5C1460` returns `int[14]`),
so the simulation mutates the copy and the real garrison is never touched.

Everything else it changes — the hero's map position, `cand->d[0x109]` (the XP-value cache, zeroed) —
is left changed; those fields are recomputed before they are next read.

### Step 1 — become the town's hero

```c
int8 owner   = town->b[0x01];
playerData* pd = &gpGame->d[0x20AD0 + owner*0x168];

cand->d[0x109] = 0;                       // invalidate cached XP value
cand->b[0x22]  = owner;                   // hero is now ours
pd->d[0xB4]   -= TAVERN_HERO_COST;        // and 2500 gold is gone (for real, temporarily)
```

`town + 0x01` and `hero + 0x22` are both the **owning player index**; `0xFF` on a hero means
unowned. `gpGame + 0x21620` is the hero array (stride 1170), used inline rather than through a
pointer — unlike the town array at `gpGame + 0x21614`, which *is* a pointer.

### Step 2 — run the army planner as if the hero had just arrived

```c
bool bonus = hero::HasArtifact(cand, 0x81)          // 0x4D91F0
          || playerData::AnyHeroHasArtifact(pd, 0x81);   // 0x4BACB0

type_AI_army_planner p;                    // stack, layout per §4B.4
AI_army_planner::init_from_town(&p, owner, town);  // 0x42D1B0 — builds the dwelling offer list
p.dst       = cand + 0x91;                 // the real hero army, in place
p.src       = &garrisonCopy;               // scratch copy of the town garrison
p.mode      = bonus;
p.morale    = hero::get_morale(cand, 0, 0, 1);          // 0x4E39B0
p.skillDiff = max(get_primary_skill_sum(cand), 0);      // 0x4E5960, no giver ⇒ minus 0
p.owner     = owner;
p.funds     = 0;
p.flag      = 1;

merge_duplicate_stacks(p.dst);                          // 0x42D870
normalise(&p);                                          // 0x42C5B0
while (take_best_stack(&p, slot_count(p.src) > 1) > 0)  // 0x42C280 / 0x44ACC0
    ;
writeback(p.dst);                                       // 0x42D8E0

AI_army_planner::recruit(&p, cand + 0x91,
        hero::get_morale(cand, 0, 0, 1),
        &garrisonCopy, pd + 0x9C, 0, bonus);            // 0x42D690
```

That is the *whole* §4B.4 exchange algorithm, applied to `(new hero) ← (town garrison)`, followed by
a recruitment pass that spends the player's real resources on the town's real dwellings. So the score
below is measured with the hero already carrying whatever the town could hand it **and** whatever the
remaining treasury could buy. That is why a rich AI with a full Castle will pay far more for a tavern
hero than a poor one — the hero is valued *after* being equipped by the town.

`init_from_town` (`0x42D1B0`) is the fourth constructor of `type_AI_army_planner` (alongside
`0x42CB70` / `0x42CE30` / `0x42CF50`): it reads `g_gDwellingType` (`0x6747B4`) and `town + 0x16` to
build the 12-byte offer records at `p+0x30..p+0x34`.

### Step 3 — teleport the hero to the town and scan the map

```c
cand->w[0] = town->b[0x05];               // x
cand->w[2] = town->b[0x06];               // y
cand->w[4] = town->b[0x07];               // z

vec<HeroDestination> objects;
AI_scan_objects(cand, scratch, &objects, /*range*/0x7FFF, 1, 0, /*ignoreCost*/false);  // 0x42EDD0
```

`town + 0x05..0x07` are the town's map coordinates as three bytes. The range is `0x7FFF` — the scan
is effectively **unbounded**, so this measures everything the hero could ever reach from that town,
not just this turn's movement.

`scratch` is the caller's private 172-byte `searchArray` (ctor `0x4B1370`, dtor `0x4B13E0`), *not*
`gpSearchArray` — the global is left alone so the scan does not disturb the turn in progress.

### Step 4 — fold the scan into a single number

The scan leaves a Dijkstra tree in `scratch`. Relevant cell fields (stride `0x1E`):

| off | meaning |
|---|---|
| +0x04 bit 0 | cell is in the tree |
| +0x0C | packed coord of the **predecessor** cell; `x ≥ 255` means none |
| +0x10 | accumulated value at this cell |
| +0x18 | move cost (§4B.8) |

```c
int direct = 0;
vec<cell*> touched;

for (HeroDestination& e : objects) {
    cell* c = &scratch->cells[index(e.coord)];
    objCell* oc = map->GetObjectCell(e.coord);              // 0x412BD0

    // (a) one of our own heroes stands here → record, don't count
    if (oc->d[0x1E] == 34 /*MapObjectType::HERO*/ && (oc->b[0x0D] & 0x10)) {
        int hid = oc->d[0x00];
        hero* h = (hid == -1) ? NULL : &heroArray[hid];
        if (h->b[0x22] == owner) { c->d[0x10] = e.value; continue; }
    }

    // (b) credit the value to the branch root, if there is one
    if (c->d[0x10] < 0 && predecessor_valid(c)) {
        cell* p = &scratch->cells[index(c->d[0x0C])];
        if (p->d[0x04] & 1) { touched.push_back(p); p->d[0x04] &= ~1; }
        p->d[0x10] += e.value;
    } else {
        direct += e.value;                                  // (c) uncredited value
    }
}

for (cell* p : touched)
    if (p->d[0x10] > 0) direct += p->d[0x10];

int total = direct;
```

The predecessor test as emitted is literally `(int16)(c->w[0x0C] << 6) >= 0x3FC0`, i.e. the
predecessor's `x` field is ≥ 255 — out of range for any real map (max 252), so it is the "no
predecessor" sentinel.

Cell index math, taken verbatim from the four copies in this function:

```c
int index(uint32 coord) {
    int x = (int16)(lo(coord) << 6) >> 6;
    int y = (int16)(hi(coord) << 6) >> 6;
    int z = (int16)(hi(coord) << 2) >> 12;
    return (y + 2*z*MAP_HEIGHT /*[0x6783CC]*/) * MAP_WIDTH /*[0x6783C8]*/ + x;
}
```

The `2*z` is in the binary (`lea eax,[eax+esi*2]`) and is reproduced here as written; it is the only
part of this function whose *meaning* is not fully pinned down (see §8).

### Step 5 — the crowding penalty

```c
int overlap = 0, n = 1;

for (int h = 0; h < pd->b[0x01]; h++) {
    hero* oh = &heroArray[pd->d[0x08 + h*4]];
    if (oh->w[4] != cand->w[4]) continue;                   // different map level
    cell* c = &scratch->cells[index(pack(oh->x, oh->y, oh->z))];
    if (!(c->d[0x04] & 1)) continue;                        // not inside the new hero's tree

    n++;
    int v = c->d[0x10];
    if (predecessor_valid(c)) {
        cell* p = &scratch->cells[index(c->d[0x0C])];
        if (p->d[0x10] < 0) v += p->d[0x10];
    }
    overlap = max(overlap, v);
}

return (total + overlap) / n;
```

`n` counts **1 plus every hero we already own that stands inside the new hero's reachable set**.
The result is divided by it. This is the AI's only defence against stacking heroes in one corner of
the map: a second hero bought into a region already covered by an existing hero is worth roughly
half, a third roughly a third. With no overlap, `n == 1`, `overlap == 0`, and the score is exactly
the raw scan total.

### Two quirks worth reproducing

1. **The two `bool` parameters are always `false`.** At `0x431DCD` and `0x431E13` the function reads
   `byte ptr [ebp+0x0B]` — the *fourth byte of its single stack argument*, which is the high byte of
   a stack pointer and therefore `0` on any 32-bit Windows stack. `ret 4` confirms there is only one
   argument. The flags fed to `AI_scan_objects` are consequently dead constants.
2. **The gold really is spent.** Between `0x431CF4` and `0x4321B4` the player's treasury is 2 500
   lighter and the town's dwellings are drained by the simulated recruitment. Anything that reads
   `playerData + 0x9C` during that window — there is nothing on this code path, but a reimplementation
   that adds logging or hooks will notice — sees the post-hire state.

## 4B.11 Building the step list — `hero::AI_build_step_list` @ `0x430610`

`__fastcall(hero* this, searchArray* /*edx, ignored*/, exe_vector<cell>* out, mapCoords* teleportOut)`
→ `void`, `ret 8`. 912 bytes, called from `AI_move_to_destination` (`0x42FEE0`) and `0x4302D0`.

Despite taking a `searchArray*` in `edx`, the register is overwritten at `0x43061F` before it is ever
read — the function always uses the global `gpSearchArray` (`[0x699284]`). Both call sites happen to
pass the global anyway, so the two agree; a reimplementation should drop the parameter.

```c
out->erase(out->begin(), out->end());                   // i.e. clear()

searchArray::build_path(gpSearchArray, this, 99999);    // 0x56A0D0

cell** path = gpSearchArray->d[0x4C];
int    n    = (gpSearchArray->d[0x50] - (int)path) / 4;

if (path == NULL || n == 0) {
    this->d[0x4D] = 0;                                  // no route ⇒ burn the hero's turn
    return;
}

for (int i = n - 1; i >= 0; i--) {                      // stored last→first, emitted first→last
    cell* s = path[i];

    if (s->b[0x04] & 0x10) {                            // "this step may not be a normal walk"
        if (abs(x(s->coord) - x(s->exit)) > 1 ||
            abs(y(s->coord) - y(s->exit)) > 1 ||
            ((s->w[0x02] ^ s->w[0x0A]) & 0x3C00))       // different z
        {
            *teleportOut       = s->exit;               // x, y and z written field-by-field
            this->d[0x35]      = x(s->exit);            // AI destination x
            this->d[0x39]      = y(s->exit);            // AI destination y
            this->w[0x3D]      = z(s->exit);            // AI destination z
            return;                                     // stop building — the rest is a new leg
        }
    }
    out->push_back(*s);                                 // 30-byte record, copied verbatim
}
```

### `searchArray::build_path` @ `0x56A0D0`

`__thiscall(searchArray*, hero*, int limit)`. Reads the hero's `AI_dest` (`+0x35/+0x39/+0x3D`), runs
the shared engine path search (`0x56BD30`), and leaves a **vector of `cell*` at `+0x4C .. +0x50`**
ordered *destination → start*. The limit passed by the AI is `99999` (`0x1869F`) — deliberately
larger than any real movement allowance, so the list spans the whole route, not just this turn.
`0x430610` is what reverses it into travel order.

New `searchArray` facts, all confirmed here:

| Offset | Meaning |
|---|---|
| `searchArray + 0x4C` | `cell**` — path result, begin |
| `searchArray + 0x50` | `cell**` — path result, end |
| `cell + 0x00` | packed coord of this cell |
| `cell + 0x04` | flag dword; bit `0x10` = verify-transition, bit `0x200` = **teleport transition** (§4D.1), bit `0x400` = **airborne / water-walking plane** (§4D.1, §4F.3), bit `0x800` = **Dimension Door transition** (§4F.4) |
| `cell + 0x08` | packed coord of the **transition exit** — equals `+0x00` for an ordinary step |
| `cell + 0x0C` | packed coord of the Dijkstra predecessor (§4B.10a) |
| `cell + 0x10` | accumulated value (§4B.10a) |
| `cell + 0x18` | move cost (§4B.8) |

### What this means for behaviour

**Teleports terminate the plan, they do not extend it.** Monoliths, subterranean gates, whirlpools
and boat boardings all set bit `0x10`, and the moment the walker meets one whose exit is not an
adjacent tile on the same level it *stops emitting steps*, rewrites `hero->AI_dest` to the exit tile
and returns. The hero therefore walks only as far as the portal this turn; the destination chooser
runs again next tick with the exit as the new goal. The AI never plans *through* a teleport in one
piece — which is why AI heroes so often stall on the far side of a monolith for a turn.

**A hero with no route loses its turn.** The `path == NULL || n == 0` exit sets `hero->mp` (`+0x4D`)
to zero rather than choosing another destination. The caller does the same: after `0x430610` returns,
`0x42FEE0` recomputes the emitted step count and, if it is zero, also writes `hero->mp = 0`
(`0x43006D`). Two independent paths to the same "give up and sleep" outcome.

**The vector is a `cell` copy, not a pointer list.** Each entry is the full 30-byte search cell copied
by value, so the walker in §4B.8 reads costs and flags from its own snapshot; a later search that
overwrites `gpSearchArray` cannot corrupt a march already in progress.

### Codegen note

At `0x430624` the compiler emitted `cmp eax, eax` — always equal — which makes the `rep movsd` block
at `0x43062E..0x430646` unreachable. It is the inlined tail-move of `erase(begin(), end())`, whose
moved range is provably empty. Not a bug and not dead code that ever mattered; noted only because
`./dr` reports it and it looks alarming. The whole prologue is just `clear()`.

Everything between `0x430723` and `0x4308EC` is likewise inlined `exe_vector<cell>::insert(end, 1, v)`
— capacity check with the `/30` magic (`0x88888889`, `sar 4`), `malloc`, range move, `free`. It
carries no AI semantics; `push_back` is an exact substitute.

## 4B.12 Move execution — `hero::AI_move_to_destination` @ `0x42FEE0`

```c
mapCoords here = pack(hero->x, hero->y, hero->z);
if (here == gpCurPlayer->lastPos) {
    mapCoords goal = pack(hero->AI_dest_x, hero->AI_dest_y, hero->AI_dest_z);
    if (goal == here) {                              // arrived
        if (hero->mp == hero->maxMp) advManager::DoAdvCommand(x, y, z);   // interact with the object
        else hero->mp = 0;                            // stop for this turn
        return;
    }
}
hero::AI_walk_path(gpSearchArray, &pathVec, dest);   // 0x430610
```

`0x430610` only *builds* the step list (§4B.11) — it does not walk it. `0x42FEE0` then iterates the
emitted vector itself: if it is empty it sets `hero->mp = 0` and stops; otherwise it inspects
`step[0].flags` and, when bit `0x200` is set — a **teleport** transition, §4D.1 — lifts the hero off
the map (`0x4175E0`) and re-places it at the exit before stepping. `0x42F980` re-runs `AI_object_value` at each step and aborts the move when the
running total drops below −500 000 000.

## 4B.13 Siege support

`combatManager::AI_use_catapult` @ `0x41EEA0`:

```c
if (side == 1) return false;                       // attacker only
if (cm->siegeState /*+0x132F4*/ == 0) return false;
int standing = 0;
for (k = 0; k < 4; ++k) {                          // wall ids from 0x63ABD0..0x63ABE0
    int idx = wallTargets[0x63BE68][wallIds[k]];
    if (cm->wallState[0x13F60 + idx*4] > 0) standing++;
}
if (standing == 0) return false;
cm->AI_compute_expected_damage(side, 0, 0, est, 0);
int stuck = Σ army::AI_value(s) over own stacks with no planned target (s->d[0x538] == 0);
… fire when `stuck` is large enough …
```

`combatManager::AI_choose_shooter_target` @ `0x41EB80` picks the best target for a shooter or war
machine using `get_ranged_attack_value` (`0x435CB0`), with an area-damage variant for creature types
45, 64 and 65 (the splash shooters) that sums the value over the neighbouring hexes.

---

# 4C. Scenario victory conditions — the only map-specific AI override

There are **no campaign-specific AI overrides in this binary**. What exists instead, and what the
open-items list was really pointing at, is that the AI reads the map's **special victory condition**
and bends nine of its valuations around it. Everything below is per-*scenario*, not per-campaign;
a campaign map gets exactly the same treatment as a single scenario with the same condition.

## 4C.1 Evidence that campaign state never reaches the AI

* Every `gpGame`-relative offset touched by code in the AI ranges (`0x41F000–0x436000`,
  `0x520000–0x52C000`, `0x5BE000–0x5C2000`) falls between `0x1F45C` and `0x21620`. The campaign
  structures live far outside that window.
* The global lists of all five AI entry points — `0x525E80`, `0x5261F0`, `0x5267B0`, `0x428DD0`,
  `0x4221F0` — resolve entirely to `gpGame`, `gpMap`, `gpSearchArray`, the current-player pair,
  `AI_player[]`, the map dimensions and UI/window managers. No campaign pointer appears.
* The `.h3c` scenario-name blob at `0x66CB2C–0x66CC18` has **zero** code references anywhere in the
  image (checked by full immediate scan *and* by byte-level pointer scan).
* `0x691684`, the only AI-private global that looked like a candidate, is an iteration index: it is
  zeroed at `0x4C7097` and compared against `curPlayerData->b[0x01]` (hero count) at `0x525F14` and
  `0x5268D8`.

## 4C.2 The special-victory record

Lives inside `gpGame`. The engine's own checker is `0x5139E0`; the AI reads the same fields.

| Offset | Type | Meaning | How confirmed |
|---|---|---|---|
| `gpGame + 0x1F89C` | `int8` | **special victory condition type**, `0xFF` = none | compared against 3,4,5,6,7,8,9 in AI code and `0xFF` at `0x4F30FC` |
| `+0x1F89D` | `int8` | also allow the standard victory | engine-side only |
| `+0x1F8A0` | `int32` | artifact id | `0x4338E9`, conditions 0 and 10 |
| `+0x1F8A4` | `int32` | creature type (condition 1) | engine-side only — *inferred* |
| `+0x1F8A8` | `int32` | creature count (condition 1) | engine-side only — *inferred* |
| `+0x1F8AC` / `+0x1F8B0` | `int32` | resource type / amount (condition 2) | engine-side only — *inferred* |
| `+0x1F8B4` / `+0x1F8B8` / `+0x1F8BC` | `int32` | target town x / y / z | `0x42B695`, `0x52AF35`, `0x52529E` |
| `+0x1F8C0` | `int8` | required **hall** level → building id `11 + level` | `0x42B90A` |
| `+0x1F8C1` | `int8` | required **fort** level → building id `7 + level` | `0x42B6C2` |
| `+0x1F8D0` | `int32` | target hero id | `0x4275D1`, `0x41E610`, `0x4273A8` |
| `+0x1F8D4` / `+0x1F8D8` / `+0x1F8DC` | `int32` | target monster x / y / z | `0x52A18B` |

Condition numbering, all cross-checked against the AI's own comparisons:

| # | Condition | AI aware? |
|---|---|---|
| 0 | Acquire a specific artifact | **yes** — `0x4336C0` |
| 1 | Accumulate creatures | **no** |
| 2 | Accumulate resources | **no** |
| 3 | Upgrade a specific town | **yes** — `0x42B670`, `0x42B8B0` |
| 4 | Build the Grail structure | **yes** — `0x42EDD0` |
| 5 | Defeat a specific hero | **yes** — `0x427330`, `0x427465`, `0x41E570` |
| 6 | Capture a specific town | **yes** — `0x52AB80`, `0x525200` |
| 7 | Defeat a specific monster | **yes** — `0x52A140` |
| 8 | Flag all creature dwellings | **yes** — `0x529A30` |
| 9 | Flag all mines | **yes** — `0x52A010` |
| 10 | Transport a specific artifact | **yes** — `0x4336C0` |

The AI is blind to conditions **1 and 2** — the two "accumulate" conditions. Neither `+0x1F8A4` nor
`+0x1F8A8` has a single reader in AI code; the only references are the engine checker (`0x513E10`,
`0x513EAE`, `0x513ECE`) and `0x4BFA26`/`0x4BFA35`. An AI on an "accumulate 100 000 gold" map plays
as if the map had no special condition at all.

## 4C.3 The override is always the same number: 5 000 000

Every victory-condition branch injects the literal `0x4C4B40` = **5 000 000**. For scale, the
"certain defeat" sentinel in the danger map is −500 000 000 and a typical good artifact scores in the
low thousands — so 5 000 000 is not a weight, it is a **hard priority override**. Whatever satisfies
the victory condition is evaluated ahead of everything else on the map, and stays ahead until it is
satisfied.

### Condition 3 — upgrade a town

Two sites, both called from `type_AI_player::do_one_purchase` (`0x42AE00`, §4A).

Fort half, `0x42B670`:

```c
if (gpGame->b[0x1F89C] == 3
 && gpGame->d[0x1F8B4] == town->b[0x05]      // x
 && gpGame->d[0x1F8B8] == town->b[0x06]      // y
 && gpGame->d[0x1F8BC] == town->b[0x07])     // z
{
    int fort = (int8)gpGame->b[0x1F8C1];
    uint64 mask = buildingBit[fort + 7];      // table at 0x66CD98, entry i = 1ull << i, stride 8
    if ((town->buildings /*+0x158, 64-bit*/ & mask) == 0)
        bonus = 5'000'000;                    // only while the requirement is unmet
}
```

Hall half, `0x42B8B0`:

```c
if (gpGame->b[0x1F89C] == 3 && same town) {
    if (buildingId >= (int8)gpGame->b[0x1F8C0] + 11)
        bonus = 5'000'000;                    // note: >=, so a higher hall also qualifies
}
```

Building ids resolve exactly as expected: `7/8/9` = Fort / Citadel / Castle, `11/12/13` = Town Hall /
City Hall / Capitol. The `1ull << i` table at `0x66CD98` is the same one the Tavern check in §4B.10
indexes (entry 5 = `0x66CDC0` = 32).

Note the asymmetry: the fort half only fires while the building is missing, the hall half fires on
any qualifying building. Both are reached through the purchase evaluator, so the effect is that the
AI reprioritises its *build order*, not its army or movement.

### Condition 4 — build the Grail

Inside `hero::AI_scan_objects` (`0x42EDD0`, §4.4), at the point where the Grail object is priced:

```c
if (gpGame->b[0x1F89C] == 4)
    value = 5'000'000;
else
    value = AI_get_value_of_artifact(&{ artId: 2 /*Grail*/, sub: -1 }, hero->owner);
```

So on a Grail map the Grail digging spot outranks every other destination on the map, and on any
other map it is worth only its ordinary artifact value.

### Conditions 0 and 10 — acquire / transport an artifact

Inside `AI_get_value_of_artifact` (`0x4336C0`):

```c
if ((cond == 0 || cond == 10) && gpGame->d[0x1F8A0] == artifactId)
    value = 5'000'000;
```

One test covers both conditions — the AI makes no distinction between "acquire" and "transport to a
location", so on a transport map it will happily pick the artifact up and then treat the job as done.
The artifact traits table indexed alongside is `0x692E18`, stride 16.

### Condition 5 — defeat a hero

Three sites. In `AI_value_of_combat` (`0x427330`) and its sibling `0x427465`:

```c
if (gpGame->b[0x1F89C] == 5 && gpGame->d[0x1F8D0] == hero->d[0x1A])
    ... treat this hero as the priority target
```

and in `0x41E570` the combat-side check walks the active stack list
(`combat->d[0x53CC + combat->d[0x132C0]*4]`) to see whether the target hero is present.

### Condition 6 — capture a town

`hero::AI_town_value` (`0x52AB80`, §4B.1) and `0x525200`:

```c
if (gpGame->b[0x1F89C] == 6) {
    int owner = GetObjectIndexAt(gpGame->d[0x1F8B4],      // 0x4BB870
                                 gpGame->d[0x1F8B8],
                                 gpGame->d[0x1F8BC]);
    if (owner >= 0 && owner == *thisTownOwner) ...        // already ours ⇒ no bonus
}
```

### Condition 7 — defeat a monster

`0x52A140`, a monster-object handler, matches the object's packed coordinate against
`+0x1F8D4/+0x1F8D8/+0x1F8DC` using the standard `(int16)(w << 6) >> 6` unpack.

### Conditions 8 and 9 — flag all dwellings / all mines

Dwelling handler `0x529A30`:

```c
if (gpGame->b[0x1F89C] == 8) {
    if (dwellingOwner >= 0 && curPlayer >= 0
     && gpGame->b[0x1F879 + dwellingOwner] == gpGame->b[0x1F879 + curPlayer])
        ;                                    // same team — no bonus
    else ...
}
```

`gpGame + 0x1F879` is a per-player byte table (244 references image-wide) used here as the team map.

Mine handler `0x52A010`:

```c
int* begin = gpGame->d[0x4E38C];
int* end   = gpGame->d[0x4E390];
value += 5'000'000 / ((end - begin) / 64);   // mine list, 64-byte records
```

**Latent divide-by-zero.** When `begin == 0` — an unallocated mine list — the compiler's constant-
folded path at `0x52A107` is literally `xor ecx, ecx ; mov eax, 0x4C4B40 ; div ecx`, a hardware
`#DE`. Reaching it needs a map with victory condition 9 and no mines at all, which the editor
presumably prevents, but a reimplementation should guard the division rather than reproduce it.

## 4C.4 What this means for a reimplementation

The victory condition is not a strategy layer — it is nine independent constant injections into
valuations that already exist. To reproduce H3's behaviour you need:

1. the `gpGame + 0x1F89C` record parsed from the map,
2. the constant `5 000 000` applied at exactly the nine sites listed above, and
3. the two gaps preserved if you want fidelity: conditions **1 and 2 do nothing**, and condition
   **10 is treated as condition 0**.

Nothing else in the adventure or battle AI consults scenario metadata. In particular the AI never
reads the *loss* condition, never reads the turn limit, and never reads the campaign chain.

# 4D. Engine primitives the AI depends on

These are not AI code — they are shared engine routines the AI calls. They were previously documented
only as deep as a call site needed. This section covers them properly, because a reimplementation
cannot reproduce the AI's decisions without reproducing their outputs exactly.

## 4D.1 The pathfinder — `searchArray::compute` @ `0x56B440`

2 288 bytes, 752 instructions. Callers: `0x4194A0` (human movement), `0x4280E0`, `0x42DE50`
(danger map), `0x42E0B0` (influence map), `0x42F570` (reachability). The AI and the human UI use the
same routine — nothing about it is AI-specific.

### It is not a priority queue

Despite being described as "Dijkstra" throughout this report, the frontier at `sa + 0x3C .. +0x40` is
a **LIFO of 30-byte cell records**, popped from the back (`sa->d[0x40] -= 0x1E` at `0x56BA82`) with
the count taken by the `/30` magic (`0x88888889`, `sar 4`). Combined with the re-push on improvement
described below, it is a **label-correcting flood**, not a label-setting shortest-path algorithm. It
converges to the same answer for non-negative costs, but the *order* cells are settled in differs,
and any reimplementation that swaps in a real priority queue will produce different tie-breaks — which
matters, because the AI's destination chooser reads the resulting order.

### The two-plane cell index — and the `2*z` from §4B.10a

```c
int plane = 2*z + ((cell.flags /*+0x04*/ & 0x400) ? 0 : 1);
int idx   = (plane * MAP_HEIGHT + y) * MAP_WIDTH + x;      // 0x6783CC / 0x6783C8
cell* c   = (cell*)sa->d[0x24] + idx * 0x1E;
```

**The search array holds two planes per map level**, selected by bit `0x400` of the cell flags. That
is the answer to the open question in §4B.10a: the level stride is doubled because a hero standing on
a given tile is a *different search state* depending on that bit, and the two states must not collide.

The dedup test at `0x56BB1C..0x56BB59` makes this explicit — two records are the same cell only when
`x`, `y`, `z` **and** bit `0x400` all match:

```c
if (((a.coord ^ b.coord) & 0x3FF) == 0            // x
 && ((a.hi    ^ b.hi   ) & 0x3FF) == 0            // y
 && ((a.hi    ^ b.hi   ) & 0x3C00) == 0           // z
 && ((a.flags ^ b.flags) & 0x400) == 0)           // same plane
    → same state, relax instead of pushing
```

`0x431BD0` (§4B.10a) computes `y + 2*z*MAP_HEIGHT` with no `+1` term, so it always reads the
**`plane = 2*z`** layer. Whatever bit `0x400` distinguishes, the hero-arrival valuation only ever
looks at one of the two.

### Hero state captured on entry

```c
sa->d[0x08] = movementLimit;                       // arg 7
sa->d[0x0C] = hero::terrain_skill(hero, 0);        // 0x4E4990
sa->d[0x10] = hero::terrain_skill(hero, 1);
sa->d[0x1C] = hero->d[0x112];                      // flight level
sa->d[0x18] = hero->d[0x116];                      // water-walk level
if (hero::HasArtifact(hero, 0x48)) sa->d[0x1C] = 3;   // artifact 72 → expert flight
if (hero::HasArtifact(hero, 0x5A)) sa->d[0x18] = 3;   // artifact 90 → expert water-walk
sa->b[0x04] = sa->b[0x14..0x17] = sa->b[0x20] = 0;
```

Two artifacts hard-override the corresponding spell level to **3 (expert)**. A reimplementation that
models Angel Wings / Boots of Levitation as "grants the spell" rather than "forces level 3" will
produce different reachability whenever the hero also has the spell at a lower level.

### Portal steps

Object types **43, 45 and 103** (`0x2B`, `0x2D`, `0x67` — the monolith family and the subterranean
gate) are handled specially at `0x56B9A7`:

```c
copy the cell record;
record.flags /*+0x04*/ |= 0x200;                   // 0x56B9CF: `or dh, 2`
record.cost  /*+0x1A*/ += 50;                      // 0x56B9D9: `add word [ebp-0x52], 0x32`
push it;
```

So a portal transition costs a flat **50** movement points and is tagged with flag `0x200`.

**Correction to §4B.11/§4B.12.** Bit `0x200` was previously labelled "boat step" on the strength of
`0x42FEE0` testing it and then calling `0x4175E0` (`DemobilizeCurrHero`, a name from the untrusted
`syms.txt`). It is set *here*, and only here, in the monolith/gate branch — so it means **teleport
transition**, and what `0x42FEE0` does on seeing it is lift the hero off the map and re-place it at
the exit. That is consistent with §4B.11's teleport early-exit, which stops the step list at exactly
these transitions.

### Remaining-movement accounting

```c
int remaining = movementLimit - (cell.cost /*+0x18*/ & 0xFFFF);
cell.flags &= ~0x340;                              // clears bits 6, 8 and 9
if (cell.flags & 2) → 0x56AB40(hero, &cell, arg4, mode);
```

Costs are held as **unsigned 16-bit** in the low half of `cell + 0x18`; the mask is explicit in the
code and a 32-bit cost field will silently diverge on long routes.

### Other callees, for the record

| Address | Role |
|---|---|
| `0x4D76E0` | `type_obscuring_object` ctor — visibility context for the walk |
| `0x4E4990` | `hero::terrain_skill(int)` — the two movement skills cached into `sa+0x0C/+0x10` |
| `0x4E4FA0` / `0x4E5240` / `0x4E5080` / `0x4E5CE0` | hero movement-cost helpers |
| `0x4E5550` | boat-state predicate (gates the `hero->b[0x438]` branch) |
| `0x56A850` / `0x56AB40` / `0x56B050` | `searchArray` internal push / relax / record |
| `0x4BB870` | `GetObjectIndexAt(x, y, z)` |
| `0x412BD0` | `map::GetObjectCell` |
| `0x4F79B0` | `cell_is_explored` (with mask `[0x69CCC4]`) |
| `0x44C590` | army slot scan on `hero + 0x91` |

`gpGame + 0x1FC40` is the object-cell array and `gpGame + 0x1FC44` its square dimension; the index is
`((z*D + y)*D + x)` with a **38-byte** stride — a different indexing scheme from the search array's.

## 4D.2 Hex geometry — `army::hex_in_direction` @ `0x523DF0`

`__thiscall(army* this, int hex, int dir)` → hex index or `-1`. 18 call sites; the battle AI's whole
notion of adjacency runs through it.

```c
int h = hex;
if (this->flags /*+0x84*/ & 1) {              // two-hex creature
    if (this->d[0x44] == 0)  { if (dir >= 3)               h = hex - 1; }   // facing left
    else                     { if (dir <= 2 || dir >= 6)   h = hex + 1; }   // facing right
}
if (h < 0 || h >= 187) return -1;

int d = dir;
if (dir == 6) d = (this->d[0x44] == 1) ? 5 : 0;
if (dir == 7) d = (this->d[0x44] == 1) ? 3 : 2;

return (int16)gpCombatManager->w[0x13468 + (h*6 + d)*2];   // precomputed adjacency table
```

* **`gpCombatManager + 0x13468` is an `int16[187][6]` neighbour table**, built once at battle start.
  There is no coordinate arithmetic anywhere in the hex code — every adjacency query is a table read.
* `187` (`0xBB`) is the battlefield hex count and is range-checked on every call.
* Directions **6 and 7 exist only for two-hex creatures** and are folded onto real directions 0/5 and
  2/3 of the *shifted* hex. Together with the ±1 shift, that yields the 8 distinct neighbours a wide
  creature has.

### `army + 0x84` bit 0 is **two-hex**, not shooter

This corrects the struct table in §2 and two comments in §5. Three independent confirmations:

1. `0x523DF0` uses it to gate a ±1 hex shift keyed on `+0x44` (facing) — meaningless for a shooter,
   exactly right for a creature occupying two hexes.
2. `0x448AB0` returns direction mask `0xFF` (8 directions) when it is set and `0x3F` (6) when clear.
3. Directions 6 and 7 only ever appear behind it.

`army + 0x44` is the **facing** (0 = left).

## 4D.3 Attack fan — `army::attack_direction_mask` @ `0x448AB0`

`__thiscall(army* this, int fromHex, army* target, int targetHex)` → direction bitmask, `ret 0xC`.

```c
int all = (this->flags & 1) ? 0xFF : 0x3F;
if (this->d[0x34] != 47) return all;              // only creature type 47 is special-cased

int tail = targetHex;
if (target->flags & 1)                             // target is two-hex
    tail = targetHex + (target->d[0x44] ? 1 : -1);

int dir = -1;
for (int i = 0; i < ((this->flags & 1) ? 8 : 6); i++) {
    int h = army::hex_in_direction(this, fromHex, i);
    if (h == targetHex || h == tail) { dir = i; break; }
}

int mask = 1 << dir;
if (this->flags & 1) {                             // 8-ring: needs the permutation tables
    mask |= 1 << fromRing[(toRing[dir] + 7) % 8];
    mask |= 1 << fromRing[(toRing[dir] + 1) % 8];
} else {                                           // 6-ring: plain modular arithmetic
    mask |= 1 << ((dir + 5) % 6);
    mask |= 1 << ((dir + 1) % 6);
}
return mask;
```

Creature **47** is the Cerberus, and this is its three-headed attack: the direction of the target plus
the two directions either side of it. Every other creature gets the unrestricted mask.

The two permutation tables are static and readable in the image:

| Table | Contents |
|---|---|
| `toRing` @ `0x660878` | `{0, 1, 2, 4, 5, 6, 7, 3}` — direction index → position on the 8-ring |
| `fromRing` @ `0x660898` | `{0, 1, 2, 7, 3, 4, 5, 6}` — the inverse |

They are exact inverses, and they exist because the 8 directions of a two-hex creature are **not**
stored in ring order: direction 3 sits at ring position 4, and direction 7 at ring position 3.
Stepping ±1 around the ring therefore cannot be done with `% 8` on the raw direction index. The same
pair is used at `0x44168D`/`0x4416A4` and `0x4416D2`/`0x4416E6`.

If no direction matches the target, `dir` stays `-1` and `1 << dir` shifts by 31 on x86 — the caller
is assumed to have established adjacency first.

## 4D.4 Hex ring collector — `combatManager::collect_hexes` @ `0x420060`

`__thiscall(army* this, int hex /*edx*/, int lo, int hi, int maxTime, searchArray* sa, vector<int>* out)`,
`ret 0x14`. Used only by `AI_score_hexes_special` (§5).

```c
for (int d = lo; d < hi; d++) {
    int h = army::hex_in_direction(this, hex, d);
    if (h < 0 || h >= 187)                continue;
    cell* c = &sa->cells[h * 0x1E];                    // sa + 0x24
    if (!(c->b[0x04] & 1))                continue;    // not reachable
    if ((uint16)c->w[0x18] > maxTime)     continue;    // costs too much
    out->push_back(h);
}
```

The `lo`/`hi` pair is what lets the caller sweep either the full ring (`0, 6`) or one half
(`0, 3` / `3, 6`) when widening around a shooter.

# 4E. Verification against the game's data files

The `.txt` data files shipped with the game were cross-checked against the binary. This resolves two
of the three inferred labels in §8 and corrects the scope of trap 5 in `METHODOLOGY.md`.

## 4E.1 The creature flags are **static**, not runtime-loaded

Trap 5 said the whole of `TCreatureTypeTraits` (`0x6703B8`, stride 116) is filled from
`crtraits.txt` at startup and reads as zeros in the image. That is true of the **stats and costs** —
the parser at `0x47B480` (reached from `0x47B290`, which opens `"crtraits.txt"` @ `0x675514`) writes
only offsets `0x18` and above, 150 times (`push 0x96` — the creature count, confirmed).

It is **not** true of the flags dword at `traits + 0x10`. That field is baked into the image:
**146 of 150 entries are non-zero before the game ever reads a file.** It can be dumped directly.

Also note: the `Attributes` column of `crtraits.txt` is genuinely reference-only, exactly as its
header claims. None of the token strings (`SHOOTING_ARMY`, `DOUBLE_WIDE`, …) appears anywhere in the
executable, so nothing parses them — and several rows are stale (it lists Cerberus and Devil as
`FLYING_ARMY`, which the binary does not).

## 4E.2 `traits + 0x10` — the flag bits, dumped and correlated

Method: read `[0x6703B8 + i*116 + 0x10]` for `i` in `0..149`, then compare each bit's membership set
against the columns and attribute tokens of `crtraits.txt`.

| Bit | Mask | Members | Meaning | Basis |
|---|---|---|---|---|
| 0 | `0x1` | 54 | **two-hex / double-wide** | code (§4D.2) + 52/54 token overlap |
| 1 | `0x2` | 40 | flying | membership |
| 2 | `0x4` | 32 | **SHOOTING** | **exact match with `Shots > 0`, all 150 creatures** |
| 3 | `0x8` | 8 | dragon family (incl. Firebird / Phoenix) | membership |
| 4 | `0x10` | 110 | living (complement of the no-morale set, minus gargoyles) | membership |
| 5 | `0x20` | 3 | `CATAPULT` | exact token match |
| 6 | `0x40` | 5 | `SIEGE_WEAPON` | exact token match |
| 7 | `0x80` | 16 | `KING_1` | exact token match |
| 8 | `0x100` | 4 | `KING_2` | exact token match |
| 9 | `0x200` | 2 | `KING_3` | exact token match |
| 10 | `0x400` | 37 | mind-spell immunity | membership |
| 12 | `0x1000` | 10 | no melee penalty | membership (Zealot, Mage, Arch Mage, Titan, Beholder, Evil Eye, Medusa ×2, Enchanter …) |
| 14 | `0x4000` | 6 | fire immunity | membership (Efreeti ×2, Fire/Energy Elemental, Firebird, Phoenix) |
| 15 | `0x8000` | 4 | attacks twice | membership (Marksman, Crusader, Grand Elf, Wolf Raider) |
| 17 | `0x20000` | 34 | **undead / mechanical — no morale, no alignment** | membership, full list below |
| 18 | `0x40000` | 15 | undead | membership |
| 19 | `0x80000` | 5 | multi-hex attack | membership (Cerberus, Hydra, Chaos Hydra, Psychic + Magic Elemental) |
| 20 | `0x100000` | 3 | splash ranged attack | membership (Magog, Lich, Power Lich) |

Bits 11, 13, 16 and 21+ are set on small sets that no single column explains; they are left unlabelled
rather than guessed.

### Bit 17 — the alignment-gate flag from §4B.4

`value_of_adding` tests `traits[t].flags & 0x20000` to decide which stacks are "alignment-free" and
therefore excluded from the town-mixing penalty. The membership set is exactly:

> all four Elementals + Energy/Ice/Magma/Magic/Psychic/Storm Elemental, all Golems (Stone, Iron, Gold,
> Diamond), all undead (Skeleton ×2, Walking Dead, Zombie, Wight, Wraith, Vampire ×2, Lich ×2, Mummy,
> Bone/Ghost Dragon, Black Knight, Dread Knight), and all five war machines.

So the flag is **"has no morale and belongs to no town"** — the creatures a hero can mix freely
because they neither suffer nor cause alignment penalties. §4B.4's description ("alignment-free")
was right; this is the verified membership behind it.

## 4E.3 Creature ids confirmed

Parsing `crtraits.txt` with proper quoted-field handling (several rows carry multi-line ability text,
which naive line-splitting corrupts) yields exactly **150** creatures and confirms every id this
report relies on:

| id | Creature | Used by |
|---|---|---|
| 47 | **Cerberus** — ability text: *"3-headed attack."* | §4D.3, the only creature `army::attack_direction_mask` special-cases |
| 78 / 79 | Minotaur / Minotaur King | §4B.4 alignment-free exemption |
| 112–115 | Air / Earth / Fire / Water Elemental | §4B.4 and `0x42C690` elemental override |
| 145–149 | Catapult, Ballista, First Aid Tent, Ammo Cart, Arrow Tower | the war-machine skips; `d[0x34] == 0x95` = Arrow Tower |

The Cerberus result is worth stating plainly: `0x448AB0` compares `this->d[0x34]` against `47` and
gives that one creature a three-direction attack fan. Creature 47 is the Cerberus, and its data-file
ability text is "3-headed attack." The inference in §4D.3 is confirmed.

Note that the engine hard-codes the id rather than testing flag bit 19 (`multi-hex attack`), even
though Cerberus carries that bit — the Hydras and the Psychic/Magic Elementals get their all-around
attack through a different path.

## 4E.4 What was still unverified at the time (now closed)

Only one of the three flagged labels survives: **what bit `0x400` of a *search-array cell* means**
(§4D.1). Its *role* — selecting between the two planes the pathfinder keeps per map level — is proven
by the dedup test, but nothing in the AI or the data files says which two hero states it separates.
It is a pathfinder-internal state bit and no `.txt` file describes it.

`combat + 0x54A4 + side` was the other one; it has since been **proven** from its write site at
`0x463AA8` (§5A.9), so nothing on that list survives.

## 4E.5 The spell-traits array — half compiled in, half loaded

Every section that prices a spell (§4.9b, §5B.4, §5C) reads `g_spellTraits`, and this report
described it only as "array pointer at `[0x687F58]`, stride 136". That was true but incomplete, and
the missing half turns out to matter.

**The array is static, at `0x685450`.** The loader at `0x59E150` addresses record *n* as
`lea ecx, [eax*8 + 0x685450]` with `eax = 17n`, i.e. `0x685450 + 136n`. Seventy records end at
`0x687980`, and the pointer variable at `0x687F58` — which every AI site dereferences — simply holds
`0x685450`. All of it sits inside the **raw** part of `.data`, so it is readable straight out of the
image.

**But only some of its fields are.** Dumping each field across all 70 records:

| field | meaning | non-zero in the image |
|---|---|---|
| `+0x00` | sign: **> 0 beneficial** (cast on your own side), **≤ 0 hostile** | 41 / 70 |
| `+0x0C` | flag word, incl. the `& 0x1F8000` category | **70 / 70** |
| `+0x18` | spell level | 0 / 70 |
| `+0x1C` | school mask | 0 / 70 |
| `+0x30` | amount per point of spell power | 0 / 70 |
| `+0x34 [level]` | base amount by school level | 0 / 70 |
| `+0x68 [level]` | AI value coefficient by school level | 0 / 70 |

So **the classification is compiled into the executable and the numbers are loaded from
`SPTRAITS.TXT`**, in the same record. This refines trap 5: "runtime-filled" is not a property of a
table, it is a property of a *field*. A record can be half one and half the other, and reading the
whole struct statically — or dismissing the whole struct as runtime-filled — is wrong either way.

### The `SPTRAITS.TXT` columns, matched to the offsets

| offset | `SPTRAITS.TXT` column |
|---|---|
| `+0x18` | 3, *Level* |
| `+0x1C` | 4–7, *School* (Earth / Water / Fire / Air) |
| `+0x30` | 12, *Power* |
| `+0x34 [none/basic/adv/expert]` | 13–16, *Effect* |
| `+0x68 [none/basic/adv/expert]` | 26–29, *AI value* |

Spot-check on Magic Arrow (spell 15): `Power` 10, `Effect` 10/10/20/30, `AI value` 10/10/10/10 —
which is exactly H3's Magic Arrow, 10 damage per point of spell power on a 10/10/20/30 base.

### The category field, censused

`§5B.4` names the six categories from `cast_spell`'s dispatch. Reading `+0x0C & 0x1F8000` out of the
image for all 70 spells confirms every one of those names independently:

| category | count | what is actually in it |
|---|---|---|
| `0x008000` | 7 | Magic Arrow, Fireball … — **direct damage** |
| `0x010000` | 2 | incl. Chain Lightning — the **opening-round** damage arm |
| `0x020000` | 3 | **Armageddon, Death Ripple, Destroy Undead** — hits both sides |
| `0x040000` | 32 | Bless, Haste, Cure, Anti-Magic … — **enchantments** |
| `0x080000` | 7 | incl. **Resurrection** — resurrection / healing |
| `0x100000` | 6 | Summon Boat, Fly, Water Walk, Dimension Door, Town Portal, View Air — **adventure spells** |
| `0x000000` | 12 | Scuttle Boat, Visions, Quicksand … — no arm, token value 1 |
| `0x108000` | 1 | **Titan's Lightning Bolt** |

The `0x020000` row is the one worth pausing on. §4.9b derived, purely from the shape of `0x5271C0`,
that this category must be spells whose damage lands on the caster's own army — because the routine
measures how much of that army is *immune*. The census says the category contains exactly Armageddon,
Death Ripple and Destroy Undead. The inference and the data agree without ever having been compared.

### Titan's Lightning Bolt is in two categories at once

Spell 57 carries `0x108000` — both the direct-damage bit and the adventure bit. `value_of_spell`
switches on the masked value and compares against each category *exactly*, so `0x108000` matches
**no** arm and falls through to `default: return 1`. The AI values Titan's Lightning Bolt at the token
value, always.

That also explains a bare address left dangling in §5B.4. The quick-combat `value_of_buff` has one
hard-coded exception:

```c
if (spell == 57) amount = *(int*)(spellTraitsBase + 0x1E7C);
```

`0x1E7C = 136 × 57 + 0x34`, so the read is `g_spellTraits[57].effect[0]` — and `SPTRAITS.TXT` gives
Titan's Lightning Bolt an *Effect* of 600/600/600/600. The exception is therefore "use the spell's
flat base damage of **600** instead of scaling it by spell power", which is exactly right for a spell
whose damage does not scale. It is also unreachable from `value_of_spell`, since 57 never lands in
the `0x008000` or `0x010000` arms — only the quick-combat path can hit it.


# 4F. Adventure-map spellcasting and sea travel

Previously absent from this report entirely. The AI *does* cast adventure spells, but only four of
them, and only from inside the movement driver — there is no "should I cast something?" planner.

## 4F.1 The dispatcher — `hero::cast_adventure_spell` @ `0x41C490`

`__thiscall(map* this, int spellId)`, `ret 4`. Shared with the human UI (`0x41C2F0` ← `0x409A70`).

```c
int hid = curPlayerData->d[0x04];
if (hid == -1) return;
hero* h = &heroArray[hid];                          // gpGame + 0x21620 + hid*1170
int lvl  = hero::school_level(h);                   // 0x4E4FA0
int mag  = hero::spell_effect(h, spellId, lvl);     // 0x4E5080
if (spellId > 9) return;
switch (spellId) { ... }                            // jump table at 0x41C86C
```

| id | Spell | Arm |
|---|---|---|
| 0 | Summon Boat | `0x41C4F3` → `0x41C8A0` |
| 1 | Scuttle Boat | `0x41C504` → `0x41CDF0` |
| 2 | Visions | `0x41C515` |
| 3 | View Earth | `0x41C5CA` |
| 4 | Disguise | `0x41C607` |
| 5 | View Air | `0x41C67B` |
| 6 | Fly | `0x41C6B8` |
| 7 | Water Walk | `0x41C796` |
| 8 | Dimension Door | `0x41C848` → `0x41D090` |
| 9 | Town Portal | `0x41C859` → `0x41D360` |

## 4F.2 The AI casts exactly four of them

`0x41C490` has **three** call sites in the whole image, and only two are AI:

| Call site | In | Spell |
|---|---|---|
| `0x41C460` | `0x41C2F0` | human UI — id from the clicked spell |
| `0x42FD47` | `0x42FC50` | **0 — Summon Boat**, hard-coded |
| `0x430A93` | `0x4309A0` | **6 — Fly** or **7 — Water Walk**, passed in |

Dimension Door is handled separately and never reaches `0x41C490` (§4F.4). So the complete set the
AI will ever cast on the adventure map is:

> **Summon Boat (0), Fly (6), Water Walk (7), Dimension Door (8).**

It **never** casts Town Portal, Scuttle Boat, Visions, View Earth, Disguise or View Air — not because
of a value test, but because no code path exists. For a reimplementation this is the whole
specification: implement these four triggers and nothing else.

## 4F.3 Fly and Water Walk — `hero::AI_advance_to_plane_change` @ `0x4309A0`

`__thiscall(hero* this, step* list /*edx*/, int index, int spellId, bool silent)`, `ret 0xC`.

```c
// scan forward for the first step whose search-plane bit flips
int i = index;
while (list.begin && i < list.size() && !(list[i].flags /*+0x04*/ & 0x400)) i++;

if (i == list.size()) {                       // no transition ahead
    if (index == 0) this->mp /*+0x4D*/ = 0;
    return false;
}

int cost = list[i].cost /*+0x18*/;
if (index > 0) cost -= list[index-1].cost;
if (this->mp < cost) {
    if (index == 0) this->mp = 0;
    return false;
}

if (!silent) {
    0x47F7D0(gpMap, 1);
    hero::cast_adventure_spell(gpMap, spellId);      // 0x41C490
}
return true;
```

Called twice from `AI_move_to_destination` (`0x42FEE0`):

```c
0x4301F8:  bool silent = hero::HasArtifact(h, 0x48);   // artifact 72
0x43021A:  AI_advance_to_plane_change(h, &list, idx, /*spell*/6 /*Fly*/,        silent);

0x430267:  bool silent = hero::HasArtifact(h, 0x5A);   // artifact 90
0x430284:  AI_advance_to_plane_change(h, &list, idx, /*spell*/7 /*Water Walk*/, silent);
```

`silent` is set when the hero owns the artifact that grants the ability — the movement still happens,
but no spell is cast and no mana is spent. This is the behavioural counterpart of §4D.1's finding
that artifacts `0x48` and `0x5A` force `sa->d[0x1C]` / `sa->d[0x18]` to level 3.

### This resolves search-cell flag `0x400`

§4D.1 proved bit `0x400` selects between the pathfinder's two planes per map level but could not say
what the planes *are*. This function scans for the bit to flip and then casts **Fly** or **Water
Walk** at exactly that step. So the second plane is the **airborne / water-walking traversal state**:
cells reachable only while flying or walking on water. Two records for the same tile are distinct
search states because arriving on foot and arriving airborne are different situations.

That also explains §4B.10a: `town::AI_hero_arrival_value` indexes `plane = 2*z` with no `+1` term, so
it values a town by what a hero could reach **on foot** — it never credits flight.

## 4F.4 Dimension Door — `0x430AB0`

`__thiscall(hero* this, step* list /*edx*/, int index)`, `ret 4`. 1 232 bytes, called from
`0x43015D` inside the move driver for every step. Does **not** go through `0x41C490`.

```c
if (!(list[index].flags & 0x800)) return false;      // step is not a DD transition
if (!this->b[0x438])              return false;      // gate (same field the pathfinder tests)
if (0x4E4EC0(this) == 21)         return false;      // unnamed predicate

int lvl  = hero::school_level(this);                             // 0x4E4FA0
int cost = hero::spell_cost(this, /*spell*/8, 0, lvl);           // 0x4E5240
if (cost + 20 > this->w[0x18] /*mana*/) return false;            // keep 20 mana in reserve

int sch  = hero::spell_effect(this, 8, lvl);                     // 0x4E5080
int used = this->b[0x10D];                                       // DDs used today
int cap  = *(int*)([0x687F58] + 0x474 + sch*4);                  // per-school-level daily cap
if (used >= cap) {
    if (!(list[index].flags & 0x80)) return false;
    if (index == 0) { this->mp = 0; return true; }
}
…
```

Three things worth porting exactly:

* **The AI keeps a 20-mana reserve** (`cost + 20 > mana`). It will not spend itself dry on Dimension
  Door even when it can afford the cast.
* `hero + 0x10D` is the **Dimension Doors used today** counter, capped per school level by a table at
  `[0x687F58] + 0x474`, indexed by the school level.
* A **third step flag, `0x800`**, marks a DD transition — §4B.11 documented `0x10` and `0x200` only.

## 4F.5 Sea travel — `0x42FC50`, and buying a boat — `0x430F80`

`0x42FC50` (656 bytes, the only caller is `0x4302D0`) is the arm of the move driver that executes one
step of the step list (§4B.11). It is `__fastcall(hero* /*ecx*/, step* /*edx*/)`, `ret 8`, with two
stack arguments: the move token and a `bool mayCast`.

The step record's flag word is `step->d[4]`; the AI reads four bits of it and the direction:

| field | meaning |
|---|---|
| `(step->d[4] >> 12) & 0xF` | facing / direction of travel |
| `step->d[4] & 0x04` | **this step enters water** |
| `step->d[4] & 0x10` | this step ends on an object |
| `step->d[5] & 0x04` | …and the object should be entered rather than walked past |

```c
int dir = (step->d[4] >> 12) & 0xF;

// 0. turn the hero to face the step, unless an animation is already running
if (gpAdvMgr->d[0x68] == 0 && mapManager::needs_turn(gpMapMgr, hero, dir))   // 0x480000
    { int save = g_animFlag; g_animFlag = 1; advManager::animate(gpAdvMgr); g_animFlag = save; }

mapCell *c = cell_at(step->x, step->y, step->z);

// 1. the water branch
if ((step->d[4] & 4) && !(hero->d[0x105] & 0x40000)) {          // entering water, not already afloat
    bool boatHere = (c->d[0x1E] == 8 /*Boat*/) && (c->b[0x0D] & 0x10);
    if (!boatHere) {
        mapManager::something(gpMapMgr, 1);                      // 0x47F7D0
        if (hero::can_summon_boat(hero)) {                       // 0x4E5550
            if (mayCast)
                hero::cast_adventure_spell(gpMapMgr, 0 /*Summon Boat*/);   // 0x41C490, §4F.1
            return false;                                        // the step is spent either way
        }
        if (c->b[0x0D] & 8)                                      // a shipyard-bearing tile
            hero::AI_try_buy_boat(hero, x, y, z);                // 0x430F80 — below
    }
}
```

**`hero + 0x105` bit 18 (`0x40000`) is "the hero is on a boat"** — the same bit
`searchArray::compute` passes as its `onBoat` argument (§4D.1), which is why the pathfinder and the
move driver agree about when a crossing is possible.

```c
// 2. execute the step, temporarily re-aiming the AI destination at the object we are entering
int sx = hero->d[0x35], sy = hero->d[0x39];  int16 sz = hero->w[0x3D];
bool retarget = (step->d[4] & 0x10) && (step->d[5] & 4) && (c->b[0x0D] & 0x10);
if (retarget) { hero->d[0x35] = x; hero->d[0x39] = y; hero->w[0x3D] = z; }

int r = mapManager::move_hero(gpMapMgr, dir, token, &packed, &outA, 1, &outB, 0);   // 0x4805E0

if (retarget) { hero->d[0x35] = sx; hero->d[0x39] = sy; hero->w[0x3D] = sz; }

// 3. the hero may have changed hands (a fight, a Prison, a Boat that was someone else's)
if (hero->b[0x22] != gpCurPlayerIdx) return false;

if (r == 0) {
    if (outA == 0 && outB == 0) return true;      // nothing happened — the caller may retry
    hero->d[0x4D] = 0;                            // the step failed: burn the movement
    return false;
}

// 4. the step landed on something
mapManager::finish_move(gpMapMgr, r, hero, packed);            // 0x4ACCA0
if (gpCurPlayer->d[4] == -1) return false;                     // the player has been eliminated

if (object_type(r) == 37 /*Hut of the Magi*/ && 0x4FCC80(r))   // 0x4FCBD0
    AI_player[hero->owner].d[0x04] = 0;                        // invalidate the magus-hut value

hero::AI_update_valuations(hero);                              // 0x527760 — §4.9b
return false;
```

That last branch closes a loop from §2 and §4G.3: `type_AI_player + 0x04` really is the **magus-hut
value**, it is produced by `0x429910` during `begin_turn`, and it is invalidated here the moment a
hero actually walks into a Hut of the Magi.

### `hero::AI_try_buy_boat` @ `0x430F80`

The old note in this section called this "boarding" and said no AI decision weight lived in it. That
was wrong on both counts. It is what the AI does when it must cross water and **cannot** summon a
boat: it finds a shipyard it owns at the target tile, **builds the Shipyard building if the town does
not have one**, and buys a boat.

```c
// __fastcall(hero* /*ecx*/, int x /*edx*/) ; stack: int y, int z
if (hero::owner_is_human(hero) && !g_aiPlaysThisTurn /*0x691209*/) return;   // 0x4D9050

playerData *pd = &gpGame->playerData[hero->owner];
town *t = NULL;

// 1. one of OUR TOWNS on that tile?
for (int i = 0; i < pd->townCount /*+0x3E*/; ++i) {
    town *cand = &gpGame->towns[pd->townIds[i]];
    if (cand->b[8] == x && cand->b[9] == y && cand->b[7] == z) { t = cand; break; }
}

if (t) {
    // 1a. it must have a Shipyard — build one if not
    if (!(*(uint64*)&t->d[0x158] & 0x40 /*building 6*/)) {      // mask at 0x66CDC8
        if (!town::build(t, 6 /*Shipyard*/)) return;            // 0x5BF3C0
    }
} else {
    // 2. otherwise, one of our standalone Shipyard objects on that tile?
    int n = (pd->d[0x94] - pd->d[0x90]) / 4;                    // vector of shipyard records
    for (int i = 0; i < n; ++i) {
        shipyard *s = pd->shipyards[i];
        if (s->z != z) continue;
        mapCell *c = cell_at(s->pos);
        if (c->x != x || c->y != y) continue;
        goto buy;
    }
    return;                                                     // nothing of ours here
}

buy:
if (pd->resources[0] /*wood, +0x9C*/ < 10)   return;
if (pd->resources[6] /*gold, +0xB4*/ < 1000) return;
if (map::create_boat(x, y, z, hero->owner, 0, 1) == -1) return; // 0x4BB250
pd->resources[6] -= 1000;
pd->resources[0] -= 10;
```

So the AI's water policy has **three tiers, in strict order**:

1. **Walk onto a boat that is already there** — free, and the pathfinder has already costed it.
2. **Summon Boat** — only if `hero::can_summon_boat` (`0x4E5550`) passes and the caller allowed
   casting. This consumes the step whether or not the cast happens.
3. **Buy one for 1 000 gold + 10 wood**, building a Shipyard first if the town on that tile lacks
   one. This is the only place in the adventure AI where a *building* is constructed outside
   `AI_build_one_building` (§4A.4), and it bypasses the whole value/priority machinery — the AI will
   put up a Shipyard purely because a hero wants to cross.

The price is not weighed against anything: there is no comparison with `resource_value[]`, no check
that the crossing is worth 1 000 gold. If the hero's step list says "cross here" and the treasury can
pay, it pays. That is worth reproducing exactly — it is a large, unconditional spend triggered by a
pathfinding decision made several layers away.


# 5. Battle AI

## 5.1 Entry — `combatManager::AI_take_turn` @ `0x4221F0`

Called from `combatManager::get_next_action` @ `0x477EE0` (`0x4782AF`) whenever the active stack
belongs to a computer side.

```c
this->d[0x132C8] = 0;
this->prepare(1);                                             // 0x477E10
army* cur = &this->armies[side*21 + index];                   // +0x54CC, stride 0x548
cur->target = cur->target2 = -1;

int mode;
if (army_is_special(cur) /*0x4428F0*/)      mode = 1;
else if (cur->type == 146 /*Ballista*/)     mode = 1;
else mode = ((~(cur->flags[0x84] >> 1)) & 1) | 2;             // 2 or 3

this->action = 3;                                             // default = defend/wait
if (mode == 1) {
    if (this->quickCombat) AI_war_machine_quick(cur);          // 0x422060
    else                   AI_war_machine_turn(cur, 0, arg4);  // 0x41F060
} else {
    AI_stack_turn(cur, 0, 0, arg4);                            // 0x421F80
}
```

## 5.2 Dispatcher — `combatManager::AI_stack_turn` @ `0x421F80`

```c
type_AI_combat_parameters est(this, arg4);                     // 0x435EC0
this->AI_update_reachability(0);                               // 0x41F140
this->AI_score_all_hexes(arg4, cur, 1, &est, 0);               // 0x422B20

if (AI_choose_target(cur, evaluateOnly, &result, &est))        // 0x421680  ← core
    return result;

if (!evaluateOnly && AI_creature_ability(cur, &result, &est))  // 0x421280
    return result;

// nothing to do
this->action = (canWait) ? 8 : 3;
return 0;
```

`type_AI_combat_parameters` (`0x28` bytes, ctor `0x435EC0`) caches, for the whole turn:
`lowest_attack`, `lowest_defense`, `kills_only`, `simulated`, `friendly_combat_value`,
`enemy_combat_value`, `awake_friendly_value`, `awake_enemy_value`, `rounds_left`,
`our_group`, `enemy_group`. It is what makes the per-target loop cheap.

## 5.3 The hex-value array

`AI_choose_target` allocates `int hexValue[187]` (`0xBB` dwords, one per battlefield hex, zeroed) and
fills it in up to four passes:

| Pass | Function | Condition | Effect |
|---|---|---|---|
| 1 | `0x4214F0` | always | for each hex holding a hazard (`cell.flags & 0x10`), subtract the expected land-mine/moat/quicksand damage from `hexValue[i]` |
| 2 | `0x421590` | `gpGame->d[0x1F698] >= 2` | extra AI-skill-gated scoring |
| 3 | `0x420260` | difficulty > 0 **or** this side is AI | scores hexes for area-of-effect / positional value |
| 4 | `0x41FB60` | difficulty ≥ 2 **or** this side is AI | further refinement |

Hex-cell geometry: `combatManager + 0x1D4 + hex*0x70`. Row of a hex = `hex / 17`; the per-row first
hex table is at `0x63BD00`.

## 5.4 Target selection — `combatManager::AI_choose_target` @ `0x421680`

2 304 bytes. For every enemy stack:

```c
// --- filters ---
skip if (enemy->flags & (1<<21))            // dead / removed
skip if (enemy->type == 149 /*Arrow Tower*/ && ...)
skip if (!our stack can attack it at all)   // 0x443080
skip if (we are a blocked shooter and cannot reach)   // 0x467460 / 0x4466A0

// --- value ---
int v = 0;
if (we are a shooter && not blocked && the target is worth shooting)
    v = AI_ranged_value(cur, enemy, &est);                       // 0x41F3B0

hexChooser chooser(cur, enemy, hexValue, gpSearchArray, &est);   // 0x4360C0
if (!chooser.find_attack_hex()) continue;                        // 0x436840

if (melee is possible)
    v += AI_melee_exchange_value(cur, enemy, 0, pathCost);       // 0x435C70

// --- the "skip this target" gate (not fleeing — see 5.9 for that) ---
if (v < 0) {
    if (difficulty > 0 || this side is AI) {
        if (v < hexValue[chooser.best_hex]) continue;            // hex is worse than the attack
    }
}

// --- RANDOMISATION ---
int r  = Random(75, 100);                                        // 0x50B230
int vr = (v * r) / 100;

// --- comparison, in priority order ---
//   1. lower attack time (chooser.best_attack_time) wins
//   2. higher  vr / attack_time wins
//   3. higher  enemy->d[0x58] wins
//   4. shorter path wins
if (better than the incumbent) { best = enemy; bestValue = vr; ... }
```

**The AI multiplies every candidate's score by a uniform random 75–100 % before comparing.** This is
the source of the AI's famous inconsistency: two identical battle states can produce different target
choices. It is *not* a tie-breaker — the ±25 % band is wide enough to reorder genuinely different
targets.

Result:

```c
*out = min(value/(100*attackTime), value/attackTime);
if (evaluateOnly) {
    if (best && *out >= 0) { this->action = 6; this->d[0x40] = bestIdx; this->d[0x44] = best->hex; }
    else                     this->action = 3;
    return true;
}
if (best) { this->AI_execute_attack(cur, bestIdx, hexValue, canWait); return true; }   // 0x41F580
// no reachable enemy → walk toward the wall / the nearest row (table 0x63BD00)
```

## 5.5 Creature special abilities — `combatManager::AI_creature_ability` @ `0x421280`

Gated on: not quick-combat, `+0x53C0 != 2`, `+0x53C4 == 0`, and **neither hero carries artifact
`0x7E`** (the anti-magic artifact). Dispatch on `army->type` in `[13, 134]` via the jump table at
`0x42135C` / index table `0x42136C`. Only five creature types are handled:

| Creature | id | Handler |
|---|---|---|
| Archangel (`aagl`) | 13 | `0x421000` — resurrection |
| Pit Lord (`pfoe`) | 51 | `0x421000` — raise demons |
| Master Genie (`calf`) | 37 | `0x420D20` — cast a beneficial spell |
| Ogre Magi (`ogrm`) | 91 | `0x420D20` — cast Bloodlust |
| Faerie Dragon (`faer`) | 134 | `0x420F00` — cast a random spell |

Everything else falls through to "no ability". Note the range check is `type - 13 <= 0x79`, so
**Enchanters (136) and Sharpshooters are outside the table entirely** — their behaviour is handled by
the generic engine, not by the AI dispatcher.

## 5.6 Hero spellcasting — `type_AI_spellcaster`

Constructors write vftable `0x63B7D8` at `0x4369F0`, `0x436B05`, `0x436BF9`, `0x436C35`; the object is
`0x410` bytes and holds three `type_AI_enemy_data[20]` arrays (`melee_enemies` @ +0x50,
`ranged_enemies` @ +0x190, `worst_enemies` @ +0x2D0) plus an embedded
`type_AI_combat_parameters` at +0x20.

Entry chain: `0x477EE0 → 0x41E570 → 0x422A40 → 0x4369C0`, and
`0x4782D0 → 0x422DA0 → 0x4369C0`. `consider_spell` walks the hero's whole spellbook and
scores each spell with the family of evaluators NH3API already names
(`get_damage_value`, `get_area_effect_value`, `get_chain_lightning_value`,
`get_attack_skill_value`, `get_defense_boost_value`, `get_speed_value`,
`get_protection_value`, `get_cancel_value`, `get_traitor_value`,
`consider_enchantment`, …) — all present here, shifted by +0x3B0 from the NH3API addresses.

## 5.7 Siege — `combatManager::AI_should_break_wall` @ `0x4213F0`

Only when `+0x132F4 != 0` (a siege) and the caller's group is the attacker (`est.our_group == 1`):
iterate the five wall-target ids in `0x63ABE0..0x63ABF4`, resolve each to a hex through
`g_wallTargets` (`0x63BE60`, 12-byte entries), and ask `0x469A10` whether anything is reachable
behind it. If every section blocks, `AI_evaluate_sides` (`0x420A80`) decides whether it is worth
bombarding at all.

## 5.8 Side strength — `combatManager::AI_evaluate_sides` @ `0x420A80`

For each of the two sides:

```c
total[side] = Σ over living, non-Arrow-Tower stacks of army::AI_value(a, b);   // 0x442E60
awake[side] = same, restricted to stacks that can still act this round;        // 0x4428F0
if (side has a hero, hero can cast, and neither hero has artifact 0x7E)
    awake[side] += type_spellvalue(hero).get_best_spell_value(0x8000);          // 0x5275B0
if (siege) awake[defender] += towerValue * traits[2].AI_value;                  // 0x5BF4F0
```

`total[]` / `awake[]` feed the side comparison; the actual retreat decision is §5.9.

---


## 5.9 Retreat and surrender — `combatManager::AI_should_flee` @ `0x41E570`

1 360 bytes. Called from `0x422DA0` (which then passes the result to the battle spellcaster as its
`retreating` flag, §5C) and from `0x477EE0`. Returns `true` = get out of this battle.

The decision is a single ratio test at the end, but it is guarded by six hard vetoes first.

### The vetoes — any one of these means "never flee"

```c
int side = cm->d[0x132C0];
hero* h  = cm->d[0x53CC + side*4];
if (!h) return false;                                     // (1) no hero ⇒ cannot flee at all

if (!cm->b[0x54A4 + side]) {                              // our side is not computer-controlled
    int diff = gpGame->b[0x1F6D8];
    if (diff == 0)                       return false;    // (2) difficulty 0: never
    if (diff == 1 && Random(1,100) <= 50) return false;   // (3) difficulty 1: 50 % of the time
}

if (cm->d[0x53CC] && HasArtifact(cm->d[0x53CC], 0x7D)) return false;   // (4) Shackles of War,
if (cm->d[0x53D0] && HasArtifact(cm->d[0x53D0], 0x7D)) return false;   //     either side

if (gpGame->b[0x1F89C] == 5                                            // (5) victory condition
 && gpGame->d[0x1F8D0] == h->d[0x1A]) return false;                    //     "defeat this hero"
                                                                       //     and we are that hero
```

Artifact **125** (`0x7D`) is the Shackles of War, and the check is symmetric — it pins *both* sides.
Veto (5) is the §4C victory-condition system reaching into combat: a hero who is the map's designated
target may not run away.

### Is this hero worth saving?

```c
int worth = 0;
for (s = 0; s < 0x98; s += 8) {                    // 19 equipped slots at hero + 0x12D
    int id = h->d[0x12D + s];  if (id == -1) continue;
    int v    = AI_get_value_of_artifact(&{id, sub}, owner);   // 0x433AA0
    int half = artifactTraits[id].d[0x04] / 2;                // [0x660B68], stride 32, +4 = gold cost
    worth += max(v, half);
}
for (s = 0; s < 0x200; s += 8) { … same over the 64 backpack slots at hero + 0x1D4 … }

if (worth < 1000 && h->d[0x51] /*experience*/ < 2000) return false;   // (6) not worth it
```

Each artifact counts as **the larger of** its AI value and **half its gold cost** — so even an
artifact the AI does not want still pins a floor under the hero's worth. A hero carrying under 1 000
of artifact value *and* under 2 000 experience is abandoned: the AI fights to the death rather than
pay to save it.

### Nothing left to fight with

```c
for (i = stackCount - 1; i >= 0; i--) {
    army* s = &cm->stacks[side][i];
    if (s->flags /*+0x84*/ & (1<<21)) continue;    // dead / removed
    if (s->flags & (1<<6))            continue;
    if (get_total_hits(s, 1) > 0) break;           // 0x443080 — this one can still act
}
if (i < 0) return true;                            // no usable stack ⇒ FLEE unconditionally
```

### Can we afford to surrender?

```c
int cost = 0x477A00(cm);
0x422A40(cm, side, 1);
if (playerData->d[0xB4] /*gold*/ < cost + 2500) return false;
```

The same 2 500 that prices a tavern hero (§4B.10) is held back as a reserve.

### The ratio test

Army value per side, over 20 stack slots:

```c
for (b = 0; b < 2; b++) {
    int v = 0;
    for (slot = 0; slot < 20; slot++) {
        army* a = …;
        if (a->d[-0x18] < 0) continue;
        int n = a->d[0x00];  if (n <= 0) continue;
        int x = n * a->d[0x64];                       // count × per-creature AI value
        if (!(a->d[0x38] & 0x4000000)) x = ftol(x * 1.2);   // [0x63AC20]
        v += x;
    }
    value[b] = v;
    if (cm->d[0x53C8] && b == 1) value[1] = ftol(value[1] * 1.1);   // [0x63AC18]
}
value[side] = ftol(value[side] * 1.1);
```

Threshold, built from the artifact `worth` computed above:

```c
float t = worth > 10000 ? 0.22f
        : worth >  5000 ? 0.21f
        : worth >     0 ? 0.20f
        :                 0.16f;

t -= (4 - difficulty) * 0.015f;                   // [0x63AC10]

float xp = (float)(h->d[0x51] / 200000);          // magic 0x14F8B589, sar 14
if (xp >= 0.03f) xp = 0.03f;                      // [0x63AC08] — clamp
t += xp;

if (side == 0) t -= 0.06f;                        // [0x63AC00], float32 — attacker discount
if (t >= 0.16f) t = 0.16f;                        // [0x63ABF8] — hard ceiling

float ratio = (float)value[side] / (float)(value[0] + value[1]);
return ratio < t;                                 // below the threshold ⇒ FLEE
```

### What this actually means

* The AI flees when its **share of the total army value on the field** drops below the threshold.
* The ceiling clamp at `0.16` fires often: the base is 0.20–0.22 whenever the hero carries any
  artifact at all, so after the clamp the effective threshold is **almost always exactly 0.16**
  unless the difficulty and experience terms pull it below. The 0.20/0.21/0.22 ladder is therefore
  nearly dead code — worth reproducing, but do not expect it to move behaviour.
* **Higher difficulty makes the AI flee *sooner***: `t -= (4 - difficulty) * 0.015`, so difficulty 4
  subtracts nothing and difficulty 0 subtracts 0.06. That is the opposite of the usual direction and
  is easy to get backwards.
* An **experienced hero is more willing to run** (`+xp`, capped at 0.03 — reached at 6 000 000
  experience).
* The **attacker gets a 0.06 discount** (`side == 0`), i.e. attacks longer before giving up.
* A stack **without** flag `0x4000000` is worth **1.2×** in this comparison, and side 1 gets a
  further **1.1×** when `cm->d[0x53C8]` is set — both sides then get 1.1× applied to the side under
  test, so the multipliers do not cancel.

Note the constant at `0x63AC00` is a **float32** (`fsub dword ptr`, opcode `D8 /4`) holding `0.06`,
not a double — reading those eight bytes as a `double` yields a denormal and makes the term look
like zero.

# 5A. Battle-AI reference math

Everything the battle AI decides reduces to four primitives. They are worth reimplementing exactly,
because every scoring function above is expressed in their terms.

## 5A.1 `army::AI_value_per_hit` @ `0x442A50`

`__thiscall(army*, int lowestAttack, int lowestDefense, bool ranged, army* attacker) → double`

This is the value of one hit point of this stack.

```c
int atkTerm = army::get_attack(this, 0, ranged)   // 0x442410
            - traits[type].attack   /*+0x54*/
            - lowestAttack;
int defTerm = army::get_defense(this)             // 0x442590
            - traits[type].defense  /*+0x58*/
            - lowestDefense;

// --- defensive half ---
double m = 1.0;
if (ranged) { if (this->d[0x208]) m = (double)this->f[0x4BC]; }   // ranged luck/morale effect
else        { if (this->d[0x204]) m = (double)this->f[0x4B8]; }
if (this->d[0x2B0]) m *= 0.5;                                     // blinded / paralysed
int side = this->d[0xF4]; if (this->d[0x288]) side = 1 - side;    // hypnotised
if (hero* h = gpCombatManager->hero[side]) m *= hero_AI_multiplier(h);   // 0x4E4310
double A = (defTerm * 0.05 + 1.0) * m;

// --- offensive half ---
// "ranged" is cleared here if the shooter is blocked / out of ammo and has no
// Bow-of-the-Sharpshooter-style artifact (0x89) on its hero
double B = atkTerm * 0.05 + 1.0;
if (!ranged && (traits.flags & 4))            B *= 0.5;      // shooter in melee
if (type == 146 /*Ballista*/) {
    int art = hero->b[0xDD];                                  // Artillery skill
    if (art > 1) B *= 2.0;
    B *= ballistaTable[art];                                  // 0x63B7F0 = {1.0, 1.5, 1.5, 2.0}
}
if (this->d[0x23C] || this->d[0x240]) {                       // wall / tower state
    double t = (this->d[0xD0] + this->d[0xD4]) / 2.0;
    double u = this->d[0x23C] ? (this->d[0x458] + this->d[0xD4])
             : this->d[0x240] ? max(this->d[0xD0] - this->d[0x45C], 1)
             : t;
    B *= u / t;
}
if (ranged && (traits.flags & 0x8000)) B *= 2.0;              // shoots twice

double v = traits[type].fight_value /*+0x3C*/ * sqrt(A * B);

// stacks whose loss barely matters are discounted
if (traits.flags & 0x400040) {
    int mine  = (flags & 0x800000) ? 1 : hp*count - firstHp;
    int total = Σ over same-side stacks (skipping flags & 0x600040) of the same quantity;
    if (mine == 0) return 0.1;
    v = v * total / (mine + total);
}
return v;
```

**The `1 + 0.05·x` terms are the standard H3 ±5 %-per-point damage law**, expressed relative to the
weakest attack / defence on the battlefield (the `lowest_*` fields of `type_AI_combat_parameters`).
`sqrt` of the product means a stack is valued by the *geometric mean* of its offence and defence.

## 5A.2 `army::AI_value_of_hits_lost` @ `0x442FD0`

`__thiscall(army*, int lowestAttack, int lowestDefense, bool ranged, int hitsLost, bool killsOnly) → int`

```c
double perHit = AI_value_per_hit(this, lowestAttack, lowestDefense, ranged, 0);
if (this->flags & 0x800000)                        // stack counted per-unit, not per-hit
    return ftol( this->count * perHit / 5.0 );
if (killsOnly) perHit = 1000.0;
int hp      = this->d[0xC0];
int firstHp = this->d[0x58];                       // remaining hp of the front creature
int carry   = ((hitsLost % hp) + firstHp < hp) ? 0 : firstHp;
return ftol( (double)(carry + hitsLost) * perHit / (double)hp );
```

## 5A.3 `type_AI_combat_parameters` @ `0x435EC0`

```c
ctor(combatManager* cm, int side) {
    our_group   = side;  enemy_group = 1 - side;
    lowest_attack = lowest_defense = 0;
    for (g = 0; g < 2; ++g)
      for (each living stack a of side g, skipping type 149 Arrow Tower) {
          lowest_attack  = min(lowest_attack,  army::get_attack (a, 0, is_shooter(a)));
          lowest_defense = min(lowest_defense, army::get_defense(a));
      }
    friendly_combat_value = cm->get_total_combat_value(our_group,   la, ld, 1);   // 0x41EAC0
    awake_friendly_value  = cm->get_total_combat_value(our_group,   la, ld, 0);
    enemy_combat_value    = cm->get_total_combat_value(enemy_group, la, ld, 1);
    awake_enemy_value     = cm->get_total_combat_value(enemy_group, la, ld, 0);

    // "we are badly outnumbered" flag (field +0x08)
    if (2*friendly_combat_value < enemy_combat_value &&
        (difficulty > 0 || sideIsComputer[our_group]))  b[0x08] = 1;

    // expected number of rounds (field +0x1C)
    int hi = max(awake_friendly, awake_enemy), lo = min(...);
    if (lo == 0 || 5*lo <= hi) rounds_left = 1;
    else {
        double r = (double)hi / (double)lo;
        int i = 0;
        while (i < 6 && r < roundsTable[i]) ++i;      // 0x63B798
        rounds_left = i + 1;
    }
}
```

`roundsTable` @ `0x63B798` = **{2.6, 1.9, 1.5, 1.31, 1.2, 1.13}**. A near-even fight (ratio < 1.13)
is expected to last 7 rounds; a 2.6:1 fight, 1.

Field usage note: **`+0x08` and `+0x09` are swapped relative to NH3API's naming.** `+0x09` is the flag
that switches `get_simple_attack_effect` into "only count damage that actually kills creatures"
mode; `+0x08` is the outnumbered flag that is forwarded as the `killsOnly` argument of
`AI_value_of_hits_lost`.

## 5A.4 `simulate_attack` @ `0x4359B0`

`__thiscall(est, army* cur, int* ourHits, army* enemy, int* enemyHits, bool ranged, int distance)`

```c
if (ranged) ranged = is_shooter(cur);
int dmg = compute_damage(cur → enemy, ranged, ceil(*ourHits / cur->hp), 1, distance);   // 0x442780
if (!ranged) {
    int fs = combatManager::fire_shield_damage(dmg, cur, enemy, *enemyHits);            // 0x422440
    if (fs > 0) *ourHits = max(*ourHits - fs, 0);
}
*enemyHits = max(*enemyHits - dmg, 0);

if (*ourHits && *enemyHits > 0 && !ranged) {
    // retaliation — simulated only when difficulty > 0 or our side is the computer
    if (!(cur->flags & 0x10000)      // attacker ignores retaliation
     && enemy->d[0x2B0] == 0         // enemy not blinded
     && enemy->d[0x454]  > 0         // retaliations left
     && (difficulty > 0 || sideIsComputer[est->our_group])) {
        int d2 = compute_damage(enemy → cur, false, ceil(*enemyHits / enemy->hp), 1, 0);
        int f2 = fire_shield_damage(d2, enemy, cur, *ourHits);
        if (f2 > 0) *enemyHits = max(*enemyHits - f2, 0);
        *ourHits = max(*ourHits - d2, 0);
    }
    if (*ourHits && *enemyHits && (cur->flags & 0x8000)) {    // attacks twice
        int d3 = compute_damage(cur → enemy, false, ceil(*ourHits / cur->hp), 1, 0);
        int f3 = fire_shield_damage(d3, cur, enemy, *enemyHits);
        *ourHits   = max(*ourHits   - f3, 0);
        *enemyHits = max(*enemyHits - d3, 0);
    }
}
```

## 5A.5 Derived scores

| Function | Address | Meaning |
|---|---|---|
| `get_simple_attack_effect(cur, ourHits, enemy, enemyHits, ranged, dist)` | `0x435B90` | run `simulate_attack` on copies; return `value_of_hits_lost(enemy, …, enemyLoss) − value_of_hits_lost(cur, …, ourLoss)` |
| `get_simple_attack_effect(cur, enemy, ranged, dist)` | `0x435C70` | same, filling the hit totals from `get_total_hits` |
| `get_ranged_attack_value(shooter, target)` | `0x435CB0` | `simple_attack_effect(…, ranged=1, dist=0)`, then **÷ 10 if the target is disabled**, **÷ turnsToReachUs**, floored at **÷ 5** |
| `get_exchange_effect(a, b, dist)` | `0x435DC0` | a hits b, then b hits a back next round; net value |
| `AI_get_attack_damage(cur, ourHits, enemy, ranged, dist)` | `0x435980` | raw `compute_damage` with the surviving creature count |

`get_ranged_attack_value` is why AI shooters prefer targets that can actually reach them: a target
that needs 5+ turns to close is worth only a fifth of its damage value.

## 5A.6 Move order — `combatManager::AI_compute_move_order` @ `0x41F140`

Writes a sort key into `army->+0x190` for every stack on both sides, sorts descending, breaks ties in
favour of the side currently acting, then rewrites `+0x190` as the reverse rank.

| Condition | Key |
|---|---|
| type 147 First Aid Tent or 148 Ammo Cart | **−100 000** |
| `d[0x25C] > 1` or `d[0x27C] > 1` (petrified / disabled) | **−10 000** |
| `flags & (1<<26)` (already acted) or `0x41F380(a)` | `speed − 1000` |
| `flags & (1<<25)` (has waited), or the no-wait mode flag `cm->b[0x13DE4]` | `−speed` |
| otherwise | `speed` |

`army->+0x190` is then read by `AI_compute_expected_damage` to decide who strikes first.

## 5A.7 `combatManager::AI_compute_expected_damage` @ `0x422B20`

`__thiscall(cm, int side, army* exclude, bool rangedOnly, est*, searchArray* sa)`

For every living stack on `side` that is not disabled and not `exclude`, it clears the four AI
scratch fields (`+0x538` planned target, `+0x53C` expected damage value, `+0x540`, `+0x544` reachable-
zone mask), computes its reachability, then for every enemy stack records

```c
time = is_shooter(s) ? 1 : sa->cells[e->hex].cost;
dist = (time > get_AI_target_time(s)) ? 0 : time;
v    = est->get_simple_attack_effect(s, e, is_shooter(s), dist);
army::set_AI_expected_damage(s, e, v, time);            // 0x448840
```

This is what makes the AI *focus fire*: `AI_get_ranged_bonus` (§5A.8) adds the value other stacks are
already expected to extract from a target.

## 5A.8 `combatManager::AI_get_ranged_bonus` @ `0x41F3B0`

```c
if (enemy->d[0x2B0]) return 0;                          // blinded — no bonus
if (enemy->d[0x454] != 1) return 0;                     // must have exactly one retaliation left
int surviving = get_total_hits(enemy, killsOnly)
              - AI_get_attack_damage(cur, get_total_hits(cur, killsOnly), enemy, 0, 0);
if (surviving <= 0) return 0;                           // we kill it anyway

int sum = 0, best = 0;
for (each own stack o that can reach the enemy's zone) {
    int d = min(AI_get_attack_damage(o, get_total_hits(o,…), enemy, 0, 0), surviving);
    double perHit = AI_value_per_hit(enemy, la, ld, 0, 0);
    int v = killsOnly ? ftol(((surviving % enemy->hp) + d) / enemy->hp * perHit)
                      : ftol((double)d * perHit / enemy->hp);
    v -= o->d[0x53C];
    if (o->d[0x538] == enemy && v > 0) sum += v; else best = max(best, v);
}
return sum + best;
```

## 5A.9 Hex-value passes in full

### Pass 1 — hazards, `0x4214F0`

For every one of the 187 hexes whose cell flag bit `0x10` is set (mine / quicksand / fire wall),
subtract the expected damage of that hazard from `hexValue[i]`.

### Pass 2 — moat, `0x421590`

Runs only when `cm->b[0x53A8]` / `cm->b[0x53A9]` are set (a defended town with a moat).

```
moatRow1 @0x63BCE8 = { 11, 28, 44, 61, 77, 95,111,129,146,164,181 }
moatRow2 @0x63BCF4 = { 10, 27, 43, 60, 76, 94,110,128,145,163,180 }
moatDamage @0x63BD18 (per town type) =
   Castle 70, Rampart 70, Tower 150, Inferno 90, Necropolis 70,
   Dungeon 90, Stronghold 70, Fortress 90, Conflux 70
```

For each of the 11 hexes in the row (skipping hex 0x5F / 0x5E unless the town type is 3):

```c
hexValue[h] -= AI_value_of_hits_lost(cur, est->lowest_attack, est->lowest_attack,
                                     0, moatDamage[townType], est->b[8]);
```

Note the quirk: **`est->lowest_attack` is passed for both the attack and the defence argument** —
almost certainly a copy/paste slip in the original source, but it must be reproduced to match.

### Pass 3 — enemy reach, `0x420260`

```c
ourFullValue = -AI_value_of_hits_lost(cur, la, ld, is_shooter(cur),
                                      get_total_hits(cur, killsOnly), 0);   // negative
*zoneMask = 0;
for (each enemy stack e, skipping disabled / dead / types 147,148,149) {
    if (is_shooter(e)) { *zoneMask |= 1 << e->d[0xF8]; continue; }
    compute e's reachability with range get_AI_target_time(e)+1;
    bool reachesUs = any own stack whose hex is passable and within e's reach;
    if (reachesUs) {
        *zoneMask |= 1 << e->d[0xF8];
        if ((e->flags & (1<<19)) && difficulty >= 2 && !sideIsComputer[our_group])
            AI_score_hexes_special(cur, e, hexValue, ourFullValue, sa, est);   // 0x41FD60
    } else {
        int neg = -est->get_simple_attack_effect(e, cur, 0, 0);
        if (neg < 0)
            for (each hex e can reach)
                hexValue[hex] = max(hexValue[hex] + neg, ourFullValue);
        // two-hex creatures also poison their rear hex
    }
}
```

#### `AI_score_hexes_special` @ `0x41FD60`

`__thiscall(combatManager*, army* cur, army* e, int* hexValue, int ourFullValue, searchArray* sa, est*)`,
768 bytes, one call site. Reached only when **all three** hold at `0x420465`:

```c
gpGame->b[0x1F6D8] >= 2                    // difficulty (0..4) at least Normal
&& combat->b[0x54A4 + ourGroup] == 0       // our side is NOT computer-controlled
&& (e->flags & (1 << 19))
```

so it runs exclusively while the AI is resolving **quick combat on behalf of a human player**. An
all-AI battle never executes a single instruction of it.

What it does: for every *other* stack on our side, it marks the hexes from which the enemy `e` could
strike that stack, and pushes those hexes down by the same attack-effect term Pass 3 uses. Where Pass
3 asks "which hexes can `e` reach", this asks "which hexes let `e` hit one of my **other** stacks" —
it is a protect-the-rest-of-the-army term, and it only exists for the human's auto-resolve.

```c
int threat = -est->get_simple_attack_effect(e, cur, 0, 0);     // 0x435C70, negative
uint8 boostedOnce[187] = {0};                                  // whole call
army* s = &combat->stacks[est->d[0x20]][0];                    // our side, stride 0x548

for (int k = combat->d[0x54BC + side*4]; k > 0; k--, s++) {
    if (s->flags /*+0x84*/ & (1 << 21)) continue;              // dead / removed
    if (s == cur)                       continue;              // not the stack we're placing
    if (s->d[0x34] == 149)              continue;              // war machine
    if (est->b[0x09] && get_total_hits(s, 1) == 0) continue;   // 0x443080
    if (combat->hex[s->hex].b[0x20E] == 0) continue;           // hex flag, stride 0x70
    if (sa->cells[s->hex].cost /*+0x18*/ > get_AI_target_time(e)) continue;   // e can't get there

    uint8 touched[187] = {0};                                  // per own-stack
    vector<int> ring; ring.clear();                            // 0x54CDB0

    int rs = (s->flags & 1 /*two-hex*/) ? 8 : 6;      // neighbour count, not shooting
    collect_hexes(s, s->hex, 0, rs, get_AI_target_time(e), sa, &ring);        // 0x420060

    if (e->flags & 1) {                                        // e occupies two hexes: widen
        int h = s->hex - (e->d[0x44] ? 1 : -1);
        if (e->d[0x44]) collect_hexes(s, h, 3, 6, get_AI_target_time(e), sa, &ring);
        else            collect_hexes(s, h, 0, 3, get_AI_target_time(e), sa, &ring);
    }

    int re = (e->flags & 1) ? 8 : 6;                            // 8 neighbours if two-hex
    for (int i = ring.size() - 1; i >= 0; i--) {
        int h        = ring[i];
        int dirMask  = 0x448AB0(e, h, s, s->hex);              // which directions are usable
        for (int d = re - 1; d >= 0; d--) {
            if (!(dirMask & (1 << d))) continue;
            int h2 = army::hex_in_direction(e, h, d);          // 0x523DF0
            if (h2 < 0 || h2 >= 187) continue;                 // 0xBB = battlefield hex count
            if (touched[h2]) continue;                         // once per own stack

            if (boostedOnce[h2]) hexValue[h2] -= 1;            // already threatened: token nudge
            else                 hexValue[h2] += threat;       // full threat, first time only
            if (hexValue[h2] < ourFullValue) hexValue[h2] = ourFullValue;   // same clamp as Pass 3

            boostedOnce[h2] = touched[h2] = 1;
        }
    }
}
free(ring.begin);
```

Points that matter for a faithful port:

* **The full threat is applied once per hex per call.** A hex threatened on behalf of three different
  friendly stacks gets `threat` once and then `-1` twice — not `3 × threat`. The two 187-byte
  bitmaps are the whole mechanism: `boostedOnce` spans the call, `touched` is reset per own stack.
* **187 (`0xBB`) is a hard bound** on the hex index, checked after every `hex_in_direction`.
* The clamp to `ourFullValue` is identical to Pass 3's, so a hex can never be driven below the value
  of losing `cur` outright.
* `get_AI_target_time` (`0x448CD0`) is `__thiscall` taking **no stack arguments** — the two pushes
  that precede its call at `0x41FEC0`/`0x41FEC4` belong to the *following* `collect_hexes` call
  (trap 11 in `METHODOLOGY.md`). Getting this wrong makes `0x420060` look like a 3-argument function.
* `combat + 0x54A4 + side` is **proven** to be "this side is computer-controlled": it is written at
  `0x463AA8` as `combat->b[0x54A4 + side] = is_computer(gpGame, player)` (`0x4CE940`), and set to 0
  at `0x463AED` when the side has no player. `is_computer` itself is
  `gpGame->b[0x20BB2 + player*360] != 0`, i.e. **`playerData + 0xE2`**.

### Pass 4 — adjacency threat, `0x41FB60`

```c
selfLoss  = AI_get_expected_incoming_damage_value(enemy_group, cur, zoneMask, est);  // 0x41F920
floorVal  = -AI_value_of_hits_lost(cur, la, ld, is_shooter(cur),
                                   get_total_hits(cur, killsOnly), 0);
bool seen[187] = {0};
for (each enemy stack e, alive, != cur, not an Arrow Tower) {
    rangedLoss = (e->flags & 8) ? AI_value_of_hits_lost(cur, …,
                     compute_damage(e → cur, 0, e->count, 1, 0), 0) : 0;
    if (!selfLoss && !rangedLoss) continue;
    int dirs = (e->flags & 1) ? 8 : 6;
    for (int d = dirs-1; d >= 0; --d) {
        int h = adjacent_hex(e, e->hex, d);
        if (0 <= h < 187 && selfLoss) {
            hexValue[h] -= seen[h] ? 1 : selfLoss;   seen[h] = 1;
            hexValue[h]  = max(hexValue[h], floorVal);
        }
        if (e->flags & 8) {                          // breath / second hex
            int h2 = second_hex(e, h, d);
            if (0 <= h2 < 187) hexValue[h2] = max(hexValue[h2] - rangedLoss, floorVal);
        }
    }
}
```

`AI_get_expected_incoming_damage_value` (`0x41F920`) sums the damage every enemy in the masked zones
would do to `cur`, plus the best mass-damage spell the enemy hero could afford (spells 20–23 only:
Frost Ring, Fireball, Inferno, Meteor Shower), and converts it to value.

## 5A.10 `type_AI_attack_hex_chooser` in full

Constructor `0x4360C0(attacker, enemy, hexValue, searchArray, est)`:

| off | value |
|---|---|
| +0x00 | `attacker` |
| +0x04 | `speed = get_AI_target_time(attacker)` |
| +0x08 | `enemy` |
| +0x0C | `searchArray` |
| +0x10 | `hexValue[187]` |
| +0x14 | `retaliation_strength` = enemy creatures surviving our first strike |
| +0x18 | `our_strength` = our creature count |
| +0x1C / +0x20 / +0x24 | `best_value` / `best_hex` (−1) / `best_attack_time` |
| +0x28 | `est` |

`check_adjacent_hexes(enemyHex, from, to)` @ `0x436300` — for each neighbour direction in range:

```c
hex = neighbourTable[enemyHex*6 + dir];             // cm + 0x13468, 6 int16 per hex
if (!(sa->cells[hex].flags & 1)) continue;          // unreachable
if (sa->cells[hex].flags & 0xFC000000) continue;

int t = (speed == 0) ? (cost > 0 ? 100 : 1)
                     : max(ceil(cost / speed) + (extraTurn ? 1 : 0), 1);
if (best_hex >= 0 && best_attack_time < t) continue;

uint32 checked = 0;
int v = get_hex_attack_value(hex, &checked);                        // 0x436180
if (difficulty > 0 || sideIsComputer[our_group]) {
    if (attacker->flags & (1<<19)) v += splash_bonus(hex, our_strength, enemy);   // 0x436620
    if (attacker->flags & (1<<3))  v += breath_bonus(hex, our_strength, enemy);   // 0x436760
    if (!(attacker->flags & (1<<16)) && enemy->d[0x2B0]==0 && enemy->d[0x454]>0) {
        if (enemy->flags & (1<<19)) v -= splash_bonus_from(enemy, retaliation_strength, attacker);
        if (enemy->flags & (1<<3))  v -= breath_bonus_from(enemy, retaliation_strength, attacker);
    }
}
int hv = hexValue[hex];
if (attacker->flags & 1) {                          // two-hex attacker: also its rear hex
    int rear = hex + (attacker->d[0x44] ? 1 : -1);
    v  += get_hex_attack_value(rear, &c2);
    hv  = min(hv, hexValue[rear]);
}
v += hv;

// selection: lower attack time wins; then higher v; then path length —
// EXCEPT for Cavalier (10) / Champion (11) on difficulty > 0, where the AI
// prefers the LONGER path to maximise the jousting bonus.
```

`get_hex_attack_value(hex, &checkedZones)` @ `0x436180` measures how much extra threat you take on by
standing next to *other* enemies:

```c
if (difficulty == 0 && !sideIsComputer[our_group]) return 0;
int total = 0;
for (each of the 6 neighbours h of hex) {
    army* e = stack_at_hex(h);
    if (!e || e is ours || e == attacker) continue;
    if (checkedZones & (1 << e->d[0xF8])) continue;   checkedZones |= …;
    if (!army_can_retaliate(e, attacker)) continue;
    int hits = get_total_hits(e, killsOnly); if (!hits) continue;
    double withAtk = AI_value_per_hit(e, la, ld, 1, attacker);
    double without = AI_value_per_hit(e, la, ld, 0, 0);
    total += max(1, ftol(hits * (withAtk - without) / e->hp));
}
return total;
```

`find_attack_hex` @ `0x436840` calls `check_adjacent_hexes(enemy->hex, 0, 6)`, repeats for the enemy's
second hex if it is a wide creature, then adds the front/back approach hexes for a wide attacker, and
returns `0 <= best_hex < 187`.

## 5A.11 Creature-ability handlers

| Handler | Creatures | Behaviour |
|---|---|---|
| `0x421000` | 13 Archangel, 51 Pit Lord | Resurrect / raise Demons (creature 48). Requires `cur->d[0xDC] > 0` casts left. Builds a temporary `army` (`0x43D250` / `0x43D730`) for the raised stack and scores every friendly stack. |
| `0x420D20` | 37 Master Genie, 91 Ogre Magi | Cast one specific beneficial spell — builds a `type_AI_spellcaster` (`0x4369C0`) and runs `0x43C330` / `0x43C4A0`. |
| `0x420F00` | 134 Faerie Dragon | Cast a random spell — `type_AI_spellcaster` + `0x43C620`. |

Gate for all three (`0x421280`): not quick combat, `cm->d[0x53C0] != 2`, `cm->b[0x53C4] == 0`, and
**neither hero carries artifact `0x7E`** (the anti-magic relic). Dispatch range is `type − 13 ≤ 0x79`,
so **creature 136 (Enchanter) and above are outside the table entirely**.

## 5A.12 The battle spell AI

### Spell traits record

Array at **`0x685450`**, stride **136 (0x88)**, pointed at by `[0x687F58]` — see §4E.5 for which
fields are static and which come from `SPTRAITS.TXT`:

| off | meaning |
|---|---|
| +0x00 | target sign — `< 0` cast on enemies, `> 0` cast on friends, `0` either |
| +0x0C | flag byte — bit 0 = usable in combat; bits 4–6 = targets a stack; bit 9 = usable while retreating |
| +0x18 | spell level (1–5) |
| +0x30 | damage per point of spell power |
| +0x34 + 4·mastery | base damage at that mastery (4 entries) |

**Spell damage** = `traits[spell].powerFactor · heroSpellPower + traits[spell].levelDamage[mastery]`.

### `consider_spell` @ `0x43BB20`

Dispatch on `spell − 14`, table `0x43BEBC` / index bytes `0x43BEE4`:

| Handler | Spells |
|---|---|
| `0x43ACA0` | 38 Resurrection, 39 Animate Dead |
| `0x437310` | 19 Chain Lightning |
| mass-damage loop | 24 Death Ripple, 25 Destroy Undead, 26 Armageddon |
| `0x43A910` ×2 or ×3 | 35 Dispel |
| `0x43B8F0` | 14 Earthquake |
| area loop | 20 Frost Ring, 21 Fireball, 22 Inferno, 23 Meteor Shower |
| `0x43B1E0` | 40 Sacrifice |
| summon | 66–69 Fire / Earth / Water / Air Elemental |
| `0x43AA60` | 63 Teleport |
| generic | everything else (all single-target damage and all enchantments) |

* **Generic path** — if `traits[spell].d[0x0C] & 0x70`: call `consider_enchantment(choice, our_group)`
  when the target sign is `>= 0`, and `consider_enchantment(choice, enemy_group)` when it is `<= 0`.
* **Mass damage** — sum `get_damage_value` over every enemy stack, subtract it over every friendly
  stack, then `get_mass_damage_effect`.
* **Area** — try every hex `0..186` that is not in column 0 or 16 (`hex % 17`), score with
  `get_area_effect_value(spell, damage, mastery, hex)` (`0x437040`), keep the best.
* **Summon elemental** — `value = elementalCount × traits[elemental].AI_value`; if the caster is
  retreating the value becomes `damage × 1000` instead.

### `cast_spell(bool retreating)` @ `0x43C800`

```c
bool antimagic = hero_has_artifact(hero, 0x53) ||
                 (enemyHero && hero_has_artifact(enemyHero, 0x53));
bool allHealthy = every own living stack (type != 149) has
                  a->d[0x500] + a->d[0x24] >= a->d[0x8C];
int power = combatMgr->d[0x53D4 + our_group*4]
          + (hero ? hero_spell_power_bonus(hero) : 0);
if (retreating) this->b[0x28] = 1;

best = 0;  bestChoice.spell = -1;
for (spell = 0; spell < 70; ++spell) {
    if (!hero->b[0x430 + spell])                        continue;   // not known
    if (!(traits[spell].b[0x0C] & 1))                   continue;   // not a combat spell
    if (retreating && !(traits[spell].b[0x0D] & 2))     continue;
    if (combatMgr->d[0x53C0] == 2 && traits[spell].level > 1) continue;
    if (antimagic && traits[spell].level > 2)           continue;
    mastery = hero_spell_mastery(hero, spell, …);       // 0x4E5080
    cost    = hero_spell_cost(hero, spell, …);          // 0x4E5240
    if (cost > hero->mana) continue;
    if (allHealthy && (spell == 38 || spell == 39)) continue;   // no point resurrecting

    type_spell_choice c = { spell, mastery, power, duration, …, value = 0, cast_now = 1 };
    consider_spell(&c);
    if (c.value <= 0) continue;

    int v = (hero->mana >= 7*cost) ? (c.value * 5) / 2
                                   : ftol( sqrt((double)(hero->mana / cost)) * (double)c.value );
    v = v * Random(75, 100) / 100;                       // ±25 %, same as target choice
    if (v > best) { best = v; bestChoice = c; }
}
if (bestChoice.spell != -1 && (bestChoice.cast_now || retreating)) {
    combatMgr->action  = 1;                  // ACTION_CAST_SPELL
    combatMgr->d[0x40] = bestChoice.spell;
    combatMgr->d[0x44] = bestChoice.target_hex;
    combatMgr->d[0x48] = bestChoice.second_target_hex;
    return true;
}
return false;
```

The `×2.5 when mana ≥ 7 × cost, else × sqrt(castsRemaining)` normalisation is the AI's mana budget:
with plenty of mana it over-values casting; when mana is tight the value shrinks with the square root
of how many casts are left.

### `combatManager` action codes (`+0x3C`)

| value | action |
|---|---|
| 1 | cast spell (`+0x40` spell, `+0x44` target hex, `+0x48` second hex) |
| 2 | move (`+0x44` destination hex) |
| 3 | defend / skip |
| 4 | (set by `get_next_action` on the surrender/retreat path) |
| 6 | attack (`+0x40` target stack index, `+0x44` target hex) |
| 8 | wait |

---

# 5B. The quick-combat simulator in full

`type_AI_combat_data` is used both for the adventure-map "should I attack this?" question and for
the actual quick-combat resolution. Constructor `0x423EE0`, body `0x424120`.

## 5B.1 Setup

```c
tactics_advantage = ourHero ? ourHero->b[0xDC] : 0;      // Tactics secondary skill
if (enemyHero) tactics_advantage = max(tactics_advantage - enemyHero->b[0xDC], 0);

// hero attack/defence, netted against the opposing hero
int atk = clamp(ourHero->b[0x476], 0, 99);
int def = clamp(ourHero->b[0x477], 0, 99);
if (enemyHero) {
    atk -= min(atk, clamp(enemyHero->b[0x477], 0, 99));
    def -= min(def, clamp(enemyHero->b[0x476], 0, 99));
}
double modifier = sqrt(def*0.05 + 1.0) * sqrt(atk*0.05 + 1.0) * baseModifier;

double manaFactor  = ourHero ? hero_spell_power(ourHero) / 5.0 : 0.2;
int    speedBonus  = ourHero ? 0x4E5AA0(ourHero) : 0;
```

## 5B.2 Per-stack `type_monster_data` (size 0x48)

```c
double hp = traits[ct].hp /*+0x4C*/;
if (ourHero) hp += hero_creature_hp_bonus(ourHero, ct);        // 0x4E5B80
int speed = traits[ct].speed /*+0x50*/ + speedBonus;

md.index                 = slot;
md.type                  = ct;
md.number = md.original_number = count;
md.speed                 = speed;
md.value                 = ftol( sqrt(hp / traits[ct].hp) * traits[ct].fight_value * modifier );
md.total_value           = md.value * count;
md.combat_value_per_hit  = (double)md.value / hp;

// speed category
if (traits[ct].flags & 4) md.category = 0;                     // shooter
else {
    md.category = (14 - 2*tactics_advantage + speed) / speed;
    if (md.category > 4) md.category = 4;
    if (wall_speed_limit > md.category && !(traits[ct].flags & 2 /*flying*/))
        md.category = wall_speed_limit;
}

// modifiers
md.melee_modifier = 0.2;  md.final_melee_modifier = 1.0;
if (md.category == 0 && !(traits[ct].flags & 0x1000))          // shooter without melee immunity
    { md.melee_modifier = 0.1;  md.final_melee_modifier = 0.7; }
md.ranged_modifier = manaFactor;
if (traits[ct].flags & 0x8000)                  md.ranged_modifier *= 2;   // shoots twice
if (wall_archery_penalty && ct != 35 /*Arch Mage*/) md.ranged_modifier /= 2;   // 0x63AC40 = 2.0

this->total_combat_value += md.total_value;
```

The vector is then sorted by `value` (insertion sort ≤ 16 elements, median-of-3 quicksort above).

### Where the two wall parameters come from — `0x424790`

Both are fields of `type_AI_combat_data`, set once by a helper the constructor calls at `0x423F26`:

```c
// type_AI_combat_data::set_wall_parameters(town *t)
this->b[0x30] = 0;                                    // wall_archery_penalty
this->w[0x32] = 0;                                    // wall_speed_limit
if (!t) return;                                       // a field battle - both stay 0
uint64 built = *(uint64*)&t->d[0x150];
if (built & g_fortMask    /*0x66CDD0*/) { this->b[0x30] = 1; this->w[0x32] = 4; }
if (built & g_citadelMask /*0x66CDD8*/) { this->b[0x30] = 1; this->w[0x32] = 5; }
if (built & g_castleMask  /*0x66CDE0*/) { this->b[0x30] = 1; this->w[0x32] = 6; }
if (this->hero /*+0x24*/ && this->hero->b[0x43E]) this->b[0x30] = 0;   // the penalty is waived
```

So **`wall_speed_limit` is 0 / 4 / 5 / 6** for no fortification / Fort / Citadel / Castle, and
`wall_archery_penalty` is simply "the town has any of the three". The tests read `town + 0x150`, a
**different** 64-bit mask from the `+0x158` built-buildings mask the building planner uses (§4G.5).

Two consequences. Against an unfortified town `wall_speed_limit == 0`, so the `md.category` clamp is
a no-op and the simulation is a plain field battle. And every adventure-AI call site of the
constructor (`0x4270C0`, `0x427210`, `0x427330`, `0x427465`) passes **no town**, so for the adventure
AI both values are always 0; only the real battle path (`0x428410`) supplies one.

## 5B.3 The round loop — `simulate_combat` @ `0x426BC0`

```c
for (round = 1; ; ++round) {
    if (this->total_combat_value <= 0 || def.total_combat_value <= 0) break;
    bool aMelee = this->choose_melee(def,  round);      // 0x4267C0
    bool bMelee = def.choose_melee(*this, round);
    // the side with the faster surviving stack casts first
    if (myFastestSpeed < def.get_fastest_speed()) { def.cast_spell(*this, round); this->cast_spell(def, round); }
    else                                          { this->cast_spell(def, round); def.cast_spell(*this, round); }
    // exchange, driven by (aMelee, bMelee)
    //   both melee:      each side takes get_attack(4, true) from the other
    //   one side ranged: X.get_attack(...) is the damage X DEALS, subtracted from the other side
    //     (proved at 0x426CC7 - this->get_attack(limit,0) then other->get_attack(4,1), and it is
    //      the OTHER side's result that is subtracted from this->total_combat_value)
    //   after every hit, if total_combat_value <= 0 → kill() and stop
}
do_aftermath(def, enemyTown);                          // 0x426EE0 — necromancy, towers, cleanup
```

### `get_attack(speedLimit, shootersBlocked)` @ `0x426390`

```c
int total = 0;
for (i = n-1; i >= 0; --i) {
    if (creatures[i].category > speedLimit) continue;
    double m = (!shootersBlocked && creatures[i].category == 0)
             ? creatures[i].ranged_modifier
             : creatures[i].melee_modifier;
    total = ftol( (double)(creatures[i].number * creatures[i].value) * m + total );
}
return total;
```

### `inflict_damage(damage, blockerSpeed)` @ `0x426300`

```c
this->total_combat_value -= damage;
if (this->total_combat_value <= 0) { kill(); return; }          // army wiped out
int left = inflict_melee_damage(damage, 1, blockerSpeed);
if (left) inflict_melee_damage(left, 0, 4);                     // spill onto everything
```

### `inflict_melee_damage(damage, minCat, maxCat)` @ `0x426170`

Damage is in **combat-value units**, distributed proportionally to `total_value` among stacks whose
category is in `[minCat, maxCat]`:

```c
int pool = Σ total_value of eligible stacks;
for (each eligible stack md with total_value > 0) {
    int share = ftol( (double)md.total_value * damage / pool );
    pool -= md.total_value;
    int destroyed;
    if (md.total_value < share) { destroyed = md.total_value; md.total_value = 0; md.number = 0; }
    else { destroyed = share; md.total_value -= share;
           md.number = (md.total_value + md.value - 1) / md.value; }
    damage -= destroyed;
    if (damage <= 0 || pool <= 0) break;
}
return damage;    // unabsorbed remainder
```

`get_final_melee_value` @ `0x426450` returns `Σ total_value × final_melee_modifier` — the value used
for the "who wins the last exchange" comparison.

---


## 5B.4 The quick-combat spell AI — `type_AI_combat_data::cast_spell` @ `0x425BD0`

§5B.3 lists `cast_spells(defender, round)` as step 1 of the round loop. It is 1 440 bytes with seven
helpers, and it is **not** the same code as the real battle's spell AI (§5C) — quick combat has its
own, simpler evaluator set. Since quick combat resolves most AI-vs-AI battles, a reimplementation
that reuses the §5C evaluators here will produce different outcomes.

`__thiscall(type_AI_combat_data *this, type_AI_combat_data *other, int round)`.

### The shared vocabulary

Three structures and two tables recur throughout. Getting them right is most of the work.

```c
struct spellctx {              // 0x24 bytes, built on the stack by 0x436980
    int spell;                 // +0x00
    int level;                 // +0x04  effective school level, 0…3
    int power;                 // +0x08  caster's spell power, clamped 1…99
    int duration;              // +0x0C  power + hero::spell_duration_bonus  (0x4E4DB0)
    int target;                // +0x14  chosen stack index on the "natural" side; −1 = the other side
    int enemyTarget;           // +0x18  chosen stack index on the enemy side (Dispel only)
    int value;                 // +0x1C  accumulated / best value
};
```

`g_spellTraits[spell]` — the **static** array at `0x685450`, stride `0x88`, which `[0x687F58]`
points at (§4E.5 shows which of its fields are compiled in and which are loaded) — supplies:

| field | meaning |
|---|---|
| `+0x00` | sign: **> 0 = beneficial** (cast on your own side), **≤ 0 = hostile** (cast on theirs) |
| `+0x0C` | flag word; bits 15–20 (`& 0x1F8000`) are the **category selector** |
| `+0x18` | spell level (1…5) |
| `+0x30` | amount **per point of spell power** |
| `+0x34 [level]` | base amount by school level (4 entries) — read by `get_base_effect` @ `0x436930` |
| `+0x68 [level]` | AI value coefficient by school level (4 entries) |

```c
// 0x436930  get_base_effect(ctx) — the whole function
return g_spellTraits[ctx->spell].d[0x34 + ctx->level * 4];
```

and the recurring magnitude, computed identically in five places:

```c
int amount = get_base_effect(ctx) + g_spellTraits[ctx->spell].d[0x30] * ctx->power;
```

`type_monster_data` (§5B.2) is read at `+0x04` type, `+0x08` number, `+0x0C` original number,
`+0x30` `combat_value_per_hit` (double), **`+0x3C` per-creature combat value**, `+0x40` total value.
The `/72` element count is the `0x38E38E39` / `sar 4` magic, factored out as
`type_AI_combat_data::stack_count` @ `0x427750`.

**`armyGroup::spell_effect_fraction` @ `0x44A4D0` returns an *effectiveness* multiplier**, not a
resistance: `1.0` = fully affected, `0.0` = immune. Every consumer either multiplies by it or tests
it against `0.0`. `0x44B4B0` is the companion per-creature immunity switch — a dense switch on
`creature − 32` (index table `0x44B5B0`, jump table `0x44B58C`) that returns its `ecx` argument
unchanged by default.

### The damage pipeline — `type_monster_data::get_spell_damage` @ `0x423DE0`

Every damage number in this subsystem goes through the same four steps, and the two "value" helpers
inline them verbatim:

```c
int get_spell_damage(int spell, hero *caster, hero *defender, int amount)
{
    if (this->total_value == 0) return 0;
    double f = armyGroup::spell_effect_fraction(spell, this->type, caster, defender);  // 0x44A4D0
    int a = ftol(f * (float)amount);                       if (!a) return 0;
    a = apply_creature_immunity(a, spell, this->type);     if (!a) return 0;   // 0x44B4B0
    a = hero::spell_effect_amount(defender, spell, a, 0);  if (!a) return 0;   // 0x4E5760
    return min(this->total_value, ftol((double)a * this->combat_value_per_hit));
}
```

The result is in **combat-value units**, capped at the stack's remaining total value — so a spell can
never "overkill" for scoring purposes.

### The main loop

```c
if (!this->d[0x1C] || !this->d[0x14] || !this->b[0x18]) return;   // no army / no mana / no caster

hero *h  = this->d[0x24];                       // our hero
hero *eh = this->d[0x2C];                       // enemy hero
bool  blocked = HasArtifact(h, 83) || (eh && HasArtifact(eh, 83));   // Recanter's Cloak

int power    = clamp(h->b[0x478], 1, 99);
int duration = hero::spell_duration_bonus(h) + power;          // 0x4E4DB0

spellctx best; best.spell = -1;  int bestValue = 0, bestCost = 0;

for (int spell = 10; spell < 70; ++spell) {
    if (!h->b[0x430 + spell]) continue;                        // not available to this hero
    int lvl = g_spellTraits[spell].d[0x18];
    if (lvl > 1 && this->d[0x10] == 2) continue;               // some combat mode caps at level 1
    if (lvl > 2 && blocked)            continue;               // Recanter's Cloak

    int sch  = hero::get_spell_school_level(h, spell, this->d[0x10]);   // 0x4E5080
    int cost = hero::get_spell_cost(h, spell, other->d[0x28], this->d[0x10]); // 0x4E5240
    if (cost > this->d[0x14] /*mana*/) continue;

    spellctx ctx;  spellctx_init(&ctx, spell, sch, power, duration);   // 0x436980
    int v = 0;

    switch (g_spellTraits[spell].d[0x0C] & 0x1F8000) {
      case 0x008000:
          v = this->get_damage_spell_value(&ctx, other);               // 0x424D20
          break;
      case 0x010000:
          if (round == 1) v = this->get_damage_spell_value(&ctx, other);
          break;                                                       // opening round only
      case 0x020000: {                                                 // hits BOTH sides
          int amount = get_base_effect(&ctx) + traits[spell].d[0x30] * ctx.power;
          int mine = 0;
          for (int i = this->stack_count() - 1; i >= 0; --i)
              mine += this->stacks[i].get_spell_damage(spell, h, h, amount);
          int theirs = other->get_mass_damage_value(&ctx, h);           // 0x4253E0
          if (mine >= this->d[0x1C]) break;                            // it would gut us
          if (mine >= theirs)        break;                            // net loss
          v = theirs - mine;
          break;
      }
      case 0x040000:
          this->get_enchantment_value(&ctx, other);                     // 0x425510
          v = ctx.value;
          break;
      case 0x080000:                                                    // resurrection
          if (spell < 38 || spell > 39) break;                          // Resurrection, Animate Dead
          for (int i = this->stack_count() - 1; i >= 0; --i) {
              int r = this->stacks[i].get_resurrection_value(&ctx, h);   // 0x423D00
              if (r > v) { v = r; bestStack = i; }
          }
          break;
      // 0x100000 has no arm — those spells are never cast in quick combat
    }

    if (v > bestValue) { best = ctx; bestValue = v; bestCost = cost; }
}
if (best.spell == -1) return;

this->d[0x14] -= bestCost;                       // pay for it

// the Familiar's mana channel: 20 % of what we spent goes to the enemy hero
if (other->d[0x24])
    for (int i = other->stack_count() - 1; i >= 0; --i)
        if (other->stacks[i].type == 43 /*Familiar*/ && other->stacks[i].number > 0)
            { other->d[0x14] += bestCost / 5; break; }

… commit, by the same category switch …
```

**Creature 43 is the Familiar**, and `cost / 5` is exactly its 20 % mana channel — confirmed against
`CRTRAITS.TXT`. It is the only creature ability the quick-combat spell AI models.

### The commit arms

```c
switch (category) {
  case 0x008000: case 0x010000:
      this->cast_damage_spell(&best, other);                    // 0x425260
      break;
  case 0x020000: {                                              // hits both sides
      int amount = get_base_effect(&best) + traits[s].d[0x30] * best.power;
      int acc = 0;
      for (int i = this->stack_count() - 1; i >= 0; --i) {      // our own losses first
          acc += this->stacks[i].get_spell_damage(s, h, h, amount);
          apply(this->stacks[i], acc);                          // inlined 0x423EA0
          this->d[0x1C] -= acc;
      }
      acc = 0;
      for (int i = other->stack_count() - 1; i >= 0; --i) {     // then theirs
          acc += other->stacks[i].get_spell_damage(s, other->hero, h, amount);
          acc  = other->stacks[i].apply_damage(acc);            // 0x423EA0
          other->d[0x1C] -= acc;
      }
      break;
  }
  case 0x040000: this->cast_enchantment_entry(&best, other); break;   // 0x425B10
  case 0x080000:                                                     // resurrection
      if (s >= 38 && s <= 39) {
          type_monster_data *md = &this->stacks[best.savedStack];
          int raised = md->get_resurrection_value(&best, h) / md->value /*+0x3C*/;
          md->number      += raised;
          md->total_value += raised * md->value;
      }
      break;
}
```

`type_monster_data::apply_damage(int dmg)` @ `0x423EA0` is the shared write-back:

```c
if (this->total_value < dmg) { this->total_value = 0; this->number = 0; return this->total_value; }
this->total_value -= dmg;
this->number = (this->total_value + this->value - 1) / this->value;    // ceiling
return dmg;
```

The same `(total + value − 1) / value` ceiling appears in `inflict_melee_damage` (§5B.3), so a stack's
creature count is always derived from its remaining combat value, never tracked independently.

### `get_damage_spell_value` @ `0x424D20`

```c
int get_damage_spell_value(spellctx *ctx, type_AI_combat_data *target)
{
    int amount = get_base_effect(ctx) + g_spellTraits[ctx->spell].d[0x30] * ctx->power;

    // 1. pick the best single target, walking the stack list BACKWARDS
    for (int i = target->stack_count() - 1; i >= 0; --i) {
        int v = target->stacks[i].get_spell_damage(ctx->spell, target->hero, this->hero, amount);
        if (v > ctx->value) { ctx->value = v; ctx->target = i; }
    }
    if (ctx->value <= 0) return ctx->value;

    // 2. area spells add neighbours
    switch (ctx->spell) {
      case 19: {                                   // Chain Lightning — three extra bounces
          uint32 visited = 1u << ctx->target;
          int idx = ctx->target, dmg = amount;
          for (int b = 0; b < 3; ++b) {
              dmg /= 2;
              idx = this->get_next_chain_lightning_target(visited, target, idx, dmg);  // 0x4249D0
              if (idx < 0) break;
              ctx->value += target->stacks[idx].get_spell_damage(19, target->hero, this->hero, dmg);
              visited |= 1u << idx;
          }
          break;
      }
      case 20: case 21: case 23:                   // Frost Ring, Fireball, Meteor Shower
          this->get_area_value(ctx, target, amount, 1); break;      // 0x424BF0
      case 22:                                     // Inferno
          this->get_area_value(ctx, target, amount, 2); break;
    }
    return ctx->value;
}
```

`get_next_chain_lightning_target` @ `0x4249D0` scans downward from the previous index, skipping
anything already in the `visited` bitmask, running the same pipeline with the spell hard-coded to
`0x13` (19), and returns the best remaining index or `−1`.

### `get_area_value` @ `0x424BF0` — the AoE adjacency model

```c
int get_area_value(spellctx *ctx, type_AI_combat_data *target, int amount, int maxExtra)
{
    int centre = target->stacks[ctx->target].index /*+0x00*/;
    for (int i = 0; i < target->stack_count() && maxExtra > 0; ++i) {
        if (abs(centre - target->stacks[i].index) != 1) continue;     // ← adjacency test
        int v = target->stacks[i].get_spell_damage(ctx->spell, target->hero, this->hero, amount);
        if (v > 0) { ctx->value += v; --maxExtra; }
    }
    return ctx->value;
}
```

**Quick combat has no hex grid, so "adjacent" means "neighbouring army slot"** — `|index − index| == 1`
on `type_monster_data + 0x00`, the original slot number. Fireball catches one extra stack, Inferno
two, and which stacks those are depends entirely on how the player arranged their army. That is the
single largest abstraction in the simulator and it is worth reproducing exactly: an army laid out
1-3-5-7 takes noticeably less area damage than one laid out 1-2-3-4.

### `get_mass_damage_value` @ `0x4253E0`

The same magnitude as `get_damage_spell_value`, but summed over **every** stack, with no best-target
tracking and no area extension:

```c
int amount = get_base_effect(ctx) + g_spellTraits[ctx->spell].d[0x30] * ctx->power;
int sum = 0;
for (int i = stack_count() - 1; i >= 0; --i)
    sum += stacks[i].get_spell_damage(ctx->spell, this->hero, casterHero, amount);
return sum;
```

### `cast_damage_spell` @ `0x425260`

```c
int amount = get_base_effect(ctx) + g_spellTraits[ctx->spell].d[0x30] * ctx->power;
type_monster_data *md = &target->stacks[ctx->target];
int dmg = md->get_spell_damage(ctx->spell, target->hero, this->hero, amount);   // inlined
md->apply_damage(dmg);
target->d[0x1C] -= dmg;
switch (ctx->spell) {
  case 19: this->cast_chain_lightning(ctx, target, amount); break;   // 0x424FB0
  case 20: case 21: case 23: this->cast_area(ctx, target, amount, 1); break;
  case 22:                   this->cast_area(ctx, target, amount, 2); break;
}
```

The valuation and the commit walk exactly the same target-selection logic, so what the AI *priced* is
what it *gets* — there is no re-selection between the two.

### `type_monster_data::get_enchantment_value` @ `0x423C80`

```c
int get_enchantment_value(spellctx *ctx, hero *a, hero *b)
{
    if (this->total_value == 0) return 0;
    int dur  = min(ctx->duration, 5);                                  // capped at five rounds
    int coef = g_spellTraits[ctx->spell].d[0x68 + ctx->level * 4];
    double f = armyGroup::spell_effect_fraction(ctx->spell, this->type, a, b);
    return ftol((double)(this->total_value * coef * dur) * f / 500.0);  // 0x63AC30 = 500.0
}
```

**An enchantment is worth `totalValue × coef × min(duration,5) × effectiveness / 500`.** The `500`
is the only tuning constant in the group, and the `min(…, 5)` is why Expert-level duration bonuses
stop paying off past five rounds.

### `get_enchantment_value` (the driver) @ `0x425510`

```c
void get_enchantment_value(spellctx *ctx, type_AI_combat_data *enemy)
{
    // 1. the protective spells are worthless against a non-caster
    if (!enemy->b[0x18])
        for (int s = 30; s <= 37; ++s)                 // Protection from Air…Earth, Anti-Magic,
            if (ctx->spell == s) return;               // Dispel, Magic Mirror, Cure

    if (ctx->spell == 35 /*Dispel*/) {
        // 2a. value it on OUR OWN stacks (stripping enemy curses)
        bool mass = spell_affects_all_at_level(35, ctx->level);         // 0x59E060
        for (int i = this->stack_count() - 1; i >= 0; --i) {
            int v = <the 0x423C80 body, inlined>;
            if (mass) ctx->value += v;
            else if (v > ctx->value) { ctx->value = v; ctx->target = i; }
        }
        // 2b. from Advanced upward, also value it on THEIR stacks (stripping their buffs)
        if (ctx->level < 2) return;
        spellctx probe = *ctx;                                          // 9 dwords
        int before = probe.value;
        bool mass2 = spell_affects_all_at_level(35, probe.level);
        for (int i = enemy->stack_count() - 1; i >= 0; --i) {
            int v = enemy->stacks[i].get_enchantment_value(ctx, this->hero, enemy->hero);
            if (mass2) ctx->value += v;
            else if (v > ctx->value) { ctx->value = v; ctx->target = i; }
        }
        if (ctx->level == 2 && before < ctx->value) {
            ctx->enemyTarget = ctx->target;
            ctx->target      = -1;                     // the marker "cast it at THEM"
        }
        return;
    }

    // 3. everything else: the sign of spellTraits[s].d[0x00] picks the side
    type_AI_combat_data *side = (g_spellTraits[ctx->spell].d[0x00] > 0) ? this : enemy;
    bool mass = spell_affects_all_at_level(ctx->spell, ctx->level);
    for (int i = side->stack_count() - 1; i >= 0; --i) {
        int v = side->stacks[i].get_enchantment_value(ctx, this->hero, enemy->hero);
        if (mass) ctx->value += v;
        else if (v > ctx->value) { ctx->value = v; ctx->target = i; }
    }
}
```

Two things here are worth calling out.

* **Dispel is the only two-sided spell in the model**, and `ctx->target == -1` is the sentinel that
  says "the winning use is on the enemy, look in `ctx->enemyTarget` instead". `cast_enchantment`'s
  entry point is the only reader of that sentinel.
* The commit test at `0x425723` is `if (ctx->level == 2)`, not `>= 2`. At **Expert** Dispel the
  enemy-side probe still runs and still updates `ctx->value` and `ctx->target`, but the sentinel is
  never set — so the value the AI computed includes the enemy-side option while the cast that follows
  goes to whichever side the ordinary path picks. Reproduce it as written; it is a shipped
  off-by-one, and "fixing" it changes which spell Expert casters choose.

### `cast_enchantment` — entry `0x425B10`, body `0x4258A0`

```c
// 0x425B10 — pick the side(s)
void cast_enchantment_entry(spellctx *ctx, type_AI_combat_data *enemy)
{
    if (ctx->spell == 35 /*Dispel*/) {
        if (ctx->level < 3) {
            if (ctx->target < 0) {                       // the enemy-side sentinel
                if (ctx->enemyTarget < 0) return;
                ctx->target = ctx->enemyTarget;
                enemy->cast_enchantment(ctx, this->hero, /*beneficial=*/false);
            } else {
                this->cast_enchantment(ctx, this->hero, /*beneficial=*/true);
            }
        } else {                                         // EXPERT Dispel hits both sides
            this ->cast_enchantment(ctx, this->hero, true);
            enemy->cast_enchantment(ctx, this->hero, false);
        }
        return;
    }
    if (g_spellTraits[ctx->spell].d[0x00] > 0) this ->cast_enchantment(ctx, this->hero, true);
    else                                       enemy->cast_enchantment(ctx, this->hero, false);
}

// 0x4258A0 — apply it
void cast_enchantment(spellctx *ctx, hero *other, bool beneficial)
{
    bool mass = spell_affects_all_at_level(ctx->spell, ctx->level);
    for (each affected stack md) {                     // one stack if !mass, all of them if mass
        int delta = <the 0x423C80 body, inlined>;
        double perHit = (double)md->total_value * md->combat_value_per_hit;
        int64  scaled = (int64)md->total_value * md->value;         // 0x617AD0 = __allmul
        if (beneficial) { md->total_value += delta;  md->value += share(scaled, delta); }
        else            { md->total_value -= delta;  md->value -= share(scaled, delta); }
        md->combat_value_per_hit = (double)md->total_value / perHit;
    }
}
```

**An enchantment is modelled purely as a shift in combat value.** `total_value` and the per-creature
`value` move together and `combat_value_per_hit` is rescaled, so the stack's *creature count* is
untouched — a Bless does not add creatures, it makes the ones you have worth more, and every
downstream formula (`get_attack`, `inflict_melee_damage`, `get_final_melee_value`) picks that up for
free.

### `type_monster_data::get_resurrection_value` @ `0x423D00`

```c
int get_resurrection_value(spellctx *ctx, hero *h)
{
    if (this->original_number <= this->number) return 0;          // nothing has been lost
    double f = armyGroup::spell_effect_fraction(ctx->spell, this->type, h, h);
    if (f == 0.0) return 0;                                       // immune

    int amount = get_base_effect(ctx) + g_spellTraits[ctx->spell].d[0x30] * ctx->power;
    if (h) amount += hero::resurrection_bonus(h, traits[this->type].level /*+0x04*/);   // 0x4E5FF0

    int lost     = this->original_number - this->number;
    int raisable = ftol((double)amount * this->combat_value_per_hit) / this->value;
    return min(lost, raisable) * this->value;                     // in combat-value units
}
```

Note the `original_number <= number` guard: **a stack that has lost nothing is worth nothing to
resurrect**, so the AI will never open with Resurrection, and the `f == 0.0` test is what stops it
trying to raise siege engines and war machines.

### The helper set, reconciled

| Address | NH3API name | Verdict |
|---|---|---|
| `0x436930` | `get_base_effect` | confirmed — 25 bytes, `spellTraits[s].d[0x34 + level*4]` |
| `0x424D20` | `get_damage_spell_value` | confirmed |
| `0x424BF0` | `get_area_value` | confirmed — slot-adjacency AoE |
| `0x4249D0` | `get_next_chain_lightning_target` | confirmed — spell hard-coded to 19 |
| `0x425260` | `cast_damage_spell` | confirmed |
| `0x4253E0` | `get_mass_damage_value` | confirmed |
| `0x425510` | `get_enchantment_value` | confirmed |
| `0x4258A0` | `cast_enchantment` | confirmed |
| `0x425B10` | `cast_enchantment` (entry) | confirmed — the side selector |
| `0x423C80` | `type_monster_data::get_enchantment_value` | confirmed |
| `0x423D00` | `type_monster_data::get_resurrection_value` | confirmed |
| `0x423DE0` | `type_monster_data::get_spell_damage` | confirmed |
| `0x423EA0` | `type_monster_data::apply_damage` | confirmed |
| `0x427750` | `type_AI_combat_data::stack_count` | confirmed |
| `0x44A4D0` | `armyGroup::spell_effect_fraction` | confirmed — returns **effectiveness**, not resistance |
| `0x44B4B0` | per-creature spell immunity switch | confirmed |

This is the one cluster where the `syms.txt` names are **corroborated rather than assumed**: the call
graph, sizes and argument shapes all match, unlike (say) `0x52C0B0`, which NH3API calls
`AI_choose_secondary_skill` but which is 16 bytes with zero callers in this build.

### What the categories actually contain

`cast_spell`'s dispatch is explicit about which helper each category gets, so the category field can
finally be named rather than guessed:

| `d[0x0C] & 0x1F8000` | quick combat does | contains |
|---|---|---|
| `0x008000` | `get_damage_spell_value` / `cast_damage_spell` | direct-damage spells |
| `0x010000` | the same, **round 1 only** | opening-round damage |
| `0x020000` | scores our own losses against their mass damage | spells that hit **both** sides (Armageddon, Death Ripple, Destroy Undead) |
| `0x040000` | `get_enchantment_value` / `cast_enchantment` | enchantments and curses |
| `0x080000` | `get_resurrection_value`, spells 38–39 only | resurrection and healing |
| `0x100000` | **no arm** | never cast in quick combat |

The adventure-side `type_spellvalue` (§4.9b) switches on the same field but assigns its own evaluator
to each category; the two are independent readings of one classification, and neither implies the
other.


# 5D. The combat-estimate leaves

The routines §4.11 and §5B name but never open.

## 5D.1 `hero + 0x47A` — the per-hero combat-strength modifier

Assigned once, when the hero record is initialised (`hero::init` @ `0x4D8720`, stores at `0x4D8967`):

```c
int   r1 = rand_range(75, 100);                                  // 0x50B230(ecx=0x4B, edx=0x64)
float base = (float)r1 * g_heroClass[hero->d[0x30]].f[0x08];     // 0x67DCEC, 64 bytes per class
int   r2 = rand_range(100, 125);                                 // 0x50B230(ecx=0x64, edx=0x7D)
hero->f[0x47A] = base / (float)r2;
```

So it is **a random draw per hero**, roughly `classFactor × U[0.6, 1.0]`, fixed for the life of the
hero. `AI_value_of_combat` widens it to a double as its base multiplier at `0x427353`. Two heroes of
the same class will value the same fight slightly differently, permanently — that is by design, and a
reimplementation that uses `1.0` will make every AI hero rate identical fights identically.

## 5D.2 The victory-condition term in `AI_value_of_combat` (`0x427330`)

```c
double modifier = (double)hero->f[0x47A];
double second   = 1.25;                                    // 0x3FF40000 high dword
if (gpGame->b[0x1F89C] == 5                                // "defeat a specific hero"
 && gpGame->d[0x1F8D0] == hero->d[0x1A])                   // …and it is THIS hero
    modifier *= 0.5;                                       // 0x63AC70
```

This settles the apparent contradiction between §4.11 and §4C.4: the term is keyed on the **victory**
condition record (`gpGame + 0x1F89C`), the same one §4C documents, and the value 5 is "defeat a
specific hero". **There is no loss-condition term** — §4C.4 is right, and any earlier wording in
§4.11 suggesting otherwise should be read as this. A hero that is itself the enemy's victory target
halves its own appetite for fights.

## 5D.3 The three hero bonus accessors

```c
// 0x4E4840  hero::xp_reward_factor(hero) → float
float f = g_learning_factor[hero->b[0xDE] /*Learning = 0xC9 + 21*/];   // 0x63EA58
if (hero->b[0xDE] > 0 && g_heroSpecialty[hero->d[0x1A]].type == 0
                      && g_heroSpecialty[hero->d[0x1A]].param == 21)
    f *= (hero->level * 0.05f + 1.0f);                                 // 0x63EAE4, 0x63B6E0
return f + 1.0f;
```

`g_learning_factor` = **{0.00, 0.05, 0.10, 0.15}** — so the multiplier is 1.00 / 1.05 / 1.10 / 1.15,
and this is the float the Sirens and `do_aftermath` XP terms scale by.

```c
// 0x4E5AA0  hero::creature_speed_bonus(hero) → int
int b = 0;
if (has_artifact_or_its_set(hero,  97 /*Necklace of Swiftness*/)) b += 1;
if (has_artifact_or_its_set(hero,  69 /*Ring of the Wayfarer*/))  b += 1;
if (has_artifact_or_its_set(hero,  99 /*Cape of Velocity*/))      b += 2;
if (g_heroSpecialty[hero->d[0x1A]].type == 5)                     b += 2;   // a speed specialist
return b;

// 0x4E5B80  hero::creature_hp_bonus(hero, int creature) → int
int b = 0;
if (has_artifact_or_its_set(hero,  94 /*Ring of Vitality*/))  b += 1;
if (has_artifact_or_its_set(hero,  95 /*Ring of Life*/))      b += 1;
if (has_artifact_or_its_set(hero,  96 /*Vial of Lifeblood*/)) b += 2;
if ((traits[creature].flags & 0x10) /*living*/
    && has_artifact_or_its_set(hero, 131 /*Elixir of Life*/))
    b += traits[creature].hp /*+0x4C*/ / 4;
return b;
```

`has_artifact_or_its_set(h, a)` is the pattern repeated in each: scan the 19 equipped slots
(`hero + 0x12D`, stride 8) for artifact `a`; if it is not worn, look up
`g_artifactTraits[a].d[0x18]` (the combination-set id, `−1` when the artifact belongs to no set) and
test whether the hero wears the assembled set artifact instead.

**Creature traits flag `0x10` is "living"** — the same bit the Elixir of Life effect class filters on
in §4.9a, and the same bit that decides whether the Elixir's `hp/4` applies here.

### Hero specialties — `0x679C80`

Several of these accessors read `g_heroSpecialty[hero->d[0x1A]]`, a **40-byte record per hero id** at
`0x679C80`, of which the AI only uses the first two dwords:

| `+0x00` (type) | `+0x04` (param) | meaning |
|---|---|---|
| 0 | secondary-skill id | specialist in that skill — 12 = Necromancy (`0x4E3CD0`), 21 = Learning (`0x4E4840`), 27 = First Aid (`0x4E4920`) |
| 5 | — | creature-speed specialist: `+2` in `0x4E5AA0` |

`hero + 0x1A` is the hero's **id**, which is what indexes this table; `hero + 0x22` is the owning
player colour. They are different fields and both appear in AI code.

## 5D.4 `choose_melee` @ `0x4267C0` — a five-way lookahead, not a heuristic

This is the single biggest surprise in the quick-combat simulator. It does not apply a rule; it
**simulates the rest of the battle once per candidate speed category and keeps the best outcome.**

```c
bool type_AI_combat_data::choose_melee(type_AI_combat_data &other, int round)
{
    int n = (last - first) / 0x48;                       // 0x38E38E39, sar 4 → /72
    int cat = n - 1;
    while (cat >= 0 && !(creatures[cat].category <= round
                      && creatures[cat].category != 0
                      && creatures[cat].number > 0)) --cat;
    if (cat < 0) return false;                           // nothing of ours can act

    int best = INT_MIN, bestLimit = 4;
    for (int limit = 4; limit >= round; --limit) {       // candidates, worst→best order
        type_AI_combat_data A = *this, B = other;        // stack copies (ctor 0x4276C0)

        // (a) every round before `limit` is a pure ranged exchange
        for (int r = round; r < limit && A.alive() && B.alive(); ++r) {
            cast_in_speed_order(A, B, r);
            int da = A.get_attack(0, false), db = B.get_attack(0, false);
            A.inflict_damage(db, 0);  B.inflict_damage(da, 0);
        }
        // (b) from `limit` onward we are in melee
        for (int r = limit; r < 4 && A.alive() && B.alive(); ++r) {
            cast_in_speed_order(A, B, r);
            int da = A.get_attack(r, false);             // our stacks capped at category r
            int db = B.get_attack(4, true);              // theirs unrestricted, shooters blocked
            A.inflict_damage(db, r);  B.inflict_damage(da, 0);
        }
        // (c) resolve the standoff
        double va = A.get_final_melee_value(), vb = B.get_final_melee_value();
        if (va != 0.0 && vb != 0.0) {
            if (va > vb) { B.kill(); A.inflict_damage(ftol((vb/va + 0.05) * vb), 0); }
            else         { A.kill(); B.inflict_damage(ftol((va/vb + 0.05) * va), 0); }
        }
        int score = A.total_combat_value ? A.total_combat_value : -B.total_combat_value;
        if (limit == 4 || score > best) { best = score; bestLimit = limit; }
    }
    return bestLimit == round;                           // "close to melee THIS round?"
}
```

`cast_in_speed_order(A, B, r)` is the same "faster side casts first" rule as `simulate_combat`:
compare `A.get_fastest_speed()` (`0x424960`) with `B.get_fastest_speed()` and call `cast_spell`
(`0x425BD0`) on the faster one first.

Three details that matter for fidelity:

* the candidate loop runs **downwards** from category 4, and ties are broken toward the *higher*
  limit (i.e. toward staying at range) because a later candidate must beat `best` strictly;
* the standoff resolution at (c) is asymmetric: the winner is not just declared, it also takes
  `(loserValue / winnerValue + 0.05) × loserValue` in damage, so a narrow win still costs;
* `0.05` is `0x63AC58`; `0.0` is `0x63AC38`.

Because both sides run this independently every round, the "both melee / one ranged" branch table in
§5B.3 is fed by two independent lookaheads, and a reimplementation that substitutes a simple
"shoot while you can" rule will diverge on any battle involving mixed shooter/walker armies.

## 5D.5 `do_general_melee` @ `0x4264D0`

The plain both-sides-melee exchange used when neither side chooses to stay at range:

```c
int total = 0;
for (int i = n - 1; i >= 0; --i)                          // walk the stack list backwards
    total = ftol((double)creatures[i].number * creatures[i].melee_modifier + (double)total);
// …the same for the other side, then a mutual inflict_damage
```

The `0x38E38E39 / sar 4` element count (`/72`) and the backwards walk are shared with
`get_attack` and `choose_melee`; the stack list order therefore matters in all three, and it is the
order `type_AI_combat_data`'s constructor produced.

## 5D.6 `do_aftermath` @ `0x426EE0`

```c
void do_aftermath(type_AI_combat_data *loser, void *townFlag)
{
    hero *win = this->d[0x24], *los = loser->d[0x24];
    if (win) win->w[0x18] = this->w[0x14];                 // mana carried out of the battle
    if (los) los->w[0x18] = loser->w[0x14];

    if (this->d[0x1C] > 0 && win) {                        // we won and we have a hero
        // 1. experience — with a 60 % chance the loser is treated as having fled,
        //    which drops the hero's own contribution from the XP award
        bool fled = los && (rand_range(0, 100) < 60);      // 0x50B230
        int xp = combat_experience(gpGame, loserArmy, fled ? NULL : los);   // 0x4CA3B0
        xp = ftol(hero::xp_reward_factor(win) * (float)xp);                 // 0x4E4840
        hero::give_experience(win, xp, 1, 1);                                // 0x4E33B0
        // 2. the loser
        if (los)         hero::set_defeated(los, 2);                        // 0x4E2DD0
        if (los && !fled) hero::take_artifacts(los, win);                    // 0x4E23D0
        // 3. the town changes hands
        if (townFlag) map::set_owner(gpGame, *(int8*)townFlag, win->owner, 0, 1);  // 0x4C61E0
    } else if (los) hero::set_defeated(los, 2);

    this->adjust_army(1);                                  // 0x424880 — write survivors back
    loser->adjust_army(1);

    if (this->d[0x1C] > 0 && win) {
        do_necromancy(this, loserArmy, loserCount);        // 0x426DF0
        // Eagle Eye
        if (los && win->b[0xD4] /*Eagle Eye = 0xC9 + 11*/ > 0
                && hero::has_artifact(win, 0 /*Spell Book*/)) {
            for (int s = 0; s < 70; ++s) {
                if (!los->b[0x430 + s]) continue;                 // the loser must have used it
                if ( win->b[0x430 + s]) continue;                 // we must not already have it
                if (spellTraits[s].level /*+0x18*/ > win->b[0xD4] + 1) continue;
                if (!(spellTraits[s].d[0x0C] & 1)) continue;       // not learnable
                if (spellTraits[s].level > win->b[0xD0] /*Wisdom*/) continue;
                … chance roll, then learn the spell …
            }
        }
    }
}
```

Two things a reimplementation must not skip: **necromancy** (`0x426DF0`, which is what makes a
Necropolis AI's quick-combat outcomes compound) and the **town flag transfer**, which is the only
place quick combat changes map ownership.


# 5C. The battle spell AI, spell by spell

§5A.12 covered the dispatch. This section is the layer beneath it: the ~40 leaf functions that answer
"what is this spell worth on this stack". They are only reachable through a function-pointer table, so
they have no call sites and standard boundary detection misses them — every address below was
recovered by recursive descent from the table.

## 5C.1 Construction — `type_AI_spellcaster::ctor` @ `0x4369C0`

`__thiscall(combatManager* cm, int side, bool creatureSpell)`

```c
new (&this->estimate /*+0x20*/) type_AI_combat_parameters(cm, side);
this->vftable           = 0x63B7D8;
this->is_creature_spell = creatureSpell;      // +0x1D
this->our_group         = side;               // +0x0C
this->enemy_group       = 1 - side;           // +0x10
this->current_hero      = cm->hero[side];     // +0x04
this->enemy_hero        = cm->hero[1 - side]; // +0x08

cm->AI_compute_move_order(false);             // 0x41F140

// win_likely: can we wipe the enemy out? true unless some living enemy stack
// with hits left lacks flag bit 6
this->win_likely = true;                      // +0x1C
for (each enemy stack e)
    if (!(e->flags & (1<<21)) && get_total_hits(e, 1) > 0 && !(e->flags & (1<<6)))
        { this->win_likely = false; break; }

for (g = 0; g < 2; ++g) cm->AI_compute_expected_damage(g, 0, 0, &this->estimate, 0);
this->find_enemy_attacks();                   // 0x43C040

// mirror model of the OPPONENT's spellcaster, built identically with side = enemy_group
this->enemy_caster = (type_AI_spellcaster*)malloc(0x410);   // +0x48
enemy_caster->enemy_caster      = this;
enemy_caster->owns_enemy_caster = false;      // +0x4C
enemy_caster->find_enemy_attacks();
```

**The spellcaster always builds an opponent model.** Dispel needs it (§5C.7) — to price removing an
enemy buff you must evaluate that buff the way the *enemy* would.

`win_likely` is the master off-switch: **fifteen of the leaf evaluators return 0 immediately when it is
set**, so an AI that is already certain to win stops buffing and debuffing entirely.

## 5C.2 Threat bookkeeping

### `set_melee_enemies` @ `0x43BF20`

Fills `melee_enemies[20]` (at `+0x50`, 16 bytes each) — one slot per **own** stack:

```c
for (i = 0; i < cm->stackCount[our_group]; ++i) {
    army* s = &cm->armies[our_group][i];
    if (s disabled / dead / tent / cart / hypnotised) continue;
    army* target = s->d[0x538];                       // the stack it plans to attack
    if (!target || is_shooter(s)) continue;
    if (army::turns_to(s, get_AI_target_time(s)) > 1) continue;   // can't reach it this turn
    melee_enemies[i].enemy        = target;
    melee_enemies[i].damage       =
    melee_enemies[i].total_damage = compute_damage(target → s, 0, target->count, 1, 0);
    melee_enemies[i].count        = 1;
}
```

### `find_enemy_attacks` @ `0x43C040`

```c
this->enemy_can_attack = 0;    // +0x14, bitmask of enemy zone ids that can hit us
this->can_be_attacked  = 0;    // +0x18, bitmask of enemy stack indices that can reach us
clear ranged_enemies[20] (+0x190) and worst_enemies[20] (+0x2D0);
set_melee_enemies();

for (i = own stacks) {
    if (mine dead or an Arrow Tower) continue;
    for (j = enemy stacks) {
        if (e disabled / dead / tent / cart / hypnotised / Arrow Tower) continue;
        if (get_total_hits(e, 1) == 0) continue;
        if (is_shooter(e)) {
            d = compute_damage(e → mine, ranged, e->count, 1, 0);
            ranged_enemies[i].count++;  ranged_enemies[i].total_damage += d;
            if (d > ranged_enemies[i].damage) { ranged_enemies[i].damage = d; ranged_enemies[i].enemy = e; }
        } else {
            if (e == melee_enemies[i].enemy) continue;      // already counted
            if (!(e->d[0x544] & (1 << i))) continue;        // e cannot reach our stack i
            this->can_be_attacked |= 1 << j;
            d = compute_damage(e → mine, 0, e->count, 1, 0);
            melee_enemies[i].count++;  melee_enemies[i].total_damage += d;
            if (d > melee_enemies[i].damage) { melee_enemies[i].damage = d; melee_enemies[i].enemy = e; }
        }
        this->enemy_can_attack |= 1 << e->d[0xF8];
    }
}
for (i = own stacks)
    worst_enemies[i] = (ranged_enemies[i].total_damage > melee_enemies[i].total_damage)
                     ? ranged_enemies[i] : melee_enemies[i];
```

`enemy_can_attack` is what Blind/Paralyze check before bothering; `can_be_attacked` gates the
defensive buffs.

## 5C.3 The two enchantment drivers

Both take the `type_spell_choice` and a side, and both look the effect up in a
**spell → evaluator function-pointer table**, `get_enchantment_effect_fn` @ `0x43B690`
(index bytes `0x43B8A4`, jump table `0x43B808`, spell range 15–75).

Every evaluator has the same signature:

```c
int __thiscall Effect(type_AI_spellcaster* this,
                      army* target,             // [ebp+8]
                      SpellID spell,            // [ebp+0x0C]
                      TSkillMastery mastery,    // [ebp+0x10]
                      int power,                // [ebp+0x14]
                      int duration,             // [ebp+0x18]
                      bool check_resistance);   // [ebp+0x1C]   → ret 0x18
```

### `consider_enchantment(choice, group)` @ `0x43A910` — the mass version

```c
if (spell_is_single_target(choice->spell, choice->mastery)) return consider_single_enchantment(...);
EffectFn fn = get_enchantment_effect_fn(choice->spell);
int total = 0;
for (each stack a in group) {
    if (cm->d[0x575C + …]) continue;                     // per-stack slot already used
    if (a->d[0x2B0] || a->d[0x2C0]) continue;            // blinded / petrified
    if (a->flags & (1<<21)) continue;                    // dead
    if (a->type == 147 || a->type == 148) continue;      // tent / cart
    if (a->spellDuration[spell] /*a + 0x198 + spell*4*/) continue;   // already under it
    if (!cm->spell_can_affect(spell, our_group, a, 1, is_creature_spell)) continue;
    total += fn(this, a, *choice);
}
choice->value = total;  choice->cast_now = true;
```

### `consider_single_enchantment(choice, group)` @ `0x43A670`

Same filters, but keeps the **best single target** and applies a magic-resistance discount:

```c
int v = fn(this, a, *choice);
if (a->d[0x228] /*has resistance*/ && group != our_group && v > 0 && spell != 35 /*Dispel*/)
    v = v * (50 - army::get_magic_resistance(a)) / 50;   // 0 at 50 % resistance
if (v > choice->value) { choice->value = v; choice->target_hex = a->hex; best = a; }
```

Afterwards it sets `choice->cast_now`: **false** when the chosen target is one of our own stacks that
some *other* still-to-act friendly stack precedes in the move order — i.e. the AI defers a buff until
just before the stack that will use it acts. It is forced **true** when the spell's trait byte `+0x0D`
has bit 6 set.

## 5C.4 The three master formulas

Almost every enchantment evaluator is a thin wrapper that computes one number and hands it to one of
these.

### A. Offensive buff — `get_attack_skill_value(our, enemy, duration, bonus)` @ `0x437800`

```c
if (this->win_likely) return 0;
army copy = *our;  copy.d[0x4AC] += bonus;              // apply the bonus to a scratch copy
bool ranged  = is_shooter(our);
double ratio = army::get_attack_rating(&copy, enemy, 100, ranged, 0)      // 0x443E30
             / army::get_attack_rating(our,   enemy, 100, ranged, 0);
int dmg    = compute_damage(our → enemy, ranged, our->count, 1, 0);
int newDmg = ftol(dmg * ratio);
int hits   = get_total_hits(enemy, 0);
if (newDmg > hits) { newDmg = hits; ratio = (double)hits / dmg; }   // cap at overkill
if (newDmg <= dmg) return 0;

int rounds = this->estimate.rounds_left;
double f = (duration >= rounds) ? 1.0 : (double)duration / rounds;
if (our->flags & (1<<26)) { f -= 1.0 / rounds; if (f <= 0) f = 0; }   // already acted

int full = army::AI_value(our, estimate.lowest_attack, estimate.lowest_defense);
return ftol( (sqrt(ratio) - 1.0) * full * f );
```

**`value = (√damageMultiplier − 1) × stackValue × durationFraction`** is the single most reused
expression in the battle spell AI.

### B. Defensive buff — `get_defense_skill_value` @ `0x438910` → `0x4387C0`

`0x438910` builds a scratch copy with the defence bonus applied, recomputes the attack rating of the
*worst* attacker against it, and passes the resulting damage divisor to `0x4387C0`, which repeats the
overkill cap and the same `(√x − 1) × value × durationFraction` shape with the ratio inverted.

### C. Speed — `get_speed_value(our, increase, duration)` @ `0x439550`

```c
if (!our->d[0x538] || this->win_likely) return 0;
int d = duration - ((our->flags & (1<<26)) ? 1 : 0);
if (d == 0) return 0;
int oldTurns = army::turns_to(our, get_AI_target_time(our));
int newTurns = army::turns_to(our, our->speed + increase);
if (newTurns > estimate.rounds_left) return 0;

int bonus = 0;
if (newTurns == 1) {                                    // the buff lets us strike this round
    army* victim = our->d[0x538];
    if (get_AI_target_time(victim) >= get_AI_target_time(our)
     && get_AI_target_time(victim) <  our->speed + increase) {
        bonus  = estimate.get_exchange_effect(our, victim, 0);      // we now strike first
        bonus += estimate.get_exchange_effect(victim, our, 0);
        if (bonus < 0) bonus = 0;
    }
}
if (newTurns < oldTurns) {
    int r = estimate.rounds_left;
    if (oldTurns > r + 1) oldTurns = r + 1;
    int val = army::AI_value(our, la, ld);
    bonus += (r - newTurns + 1) * val / r - (r - oldTurns + 1) * val / r;
}
return bonus;
```

Haste and Slow are therefore valued as **the fraction of the remaining rounds the stack gains or
loses**, plus the whole two-way exchange when the speed change flips who strikes first.

## 5C.5 Complete evaluator table

`0x43B680` = "no evaluator" (those spells are handled directly in `consider_spell`).

| Spell | Evaluator | What it computes |
|---|---|---|
| 15 Magic Arrow, 16 Ice Bolt, 17 Lightning Bolt, 18 Implosion, 57 Titan's Bolt | `0x436F60` | `get_damage_spell_value` → `damage = powerFactor·power + levelData[mastery]`, then `get_damage_value` |
| 41 Bless | `0x437430` | multiplier `= (levelData[mastery] + minDamage) / averageDamage`, then formula A |
| 43 Bloodlust | `0x438100` | `get_attack_skill_value(target, target->plannedTarget, duration, levelData[mastery])` |
| 44 Precision | `0x438B50` | same, ranged variant |
| 45 Weakness | `0x438ED0` | same with a negative bonus, applied to the **enemy** |
| 48 Prayer | `0x438AC0` | attack **and** defence **and** speed — calls all three master formulas and sums |
| 55 Slayer | `0x438D00` | formula A with the anti-dragon bonus from `levelData[mastery]` |
| 56 Frenzy | `0x4375D0` | runs `simulate_attack` twice (with and without the defence loss) and diffs the values |
| 27 Shield | `0x438C60` | halves melee damage → `0x4387C0` with divisor from `levelData[mastery]` |
| 28 Air Shield | `0x438BC0` | same for ranged damage |
| 46 Stone Skin | `0x438D90` | formula B with `levelData[mastery]` defence bonus |
| 47 Disrupting Ray | `0x438DC0` | defence **reduction** on an enemy: `(√ratio − 1) × value` directly |
| 53 Haste | `0x4396B0` | formula C with `+levelData[mastery]` |
| 54 Slow | `0x439270` | formula C with a negative delta, plus `get_exchange_effect` when it removes the enemy's strike |
| 49 Mirth | `0x438170` | `AI_value_of_morale(target->morale /*+0x4E8*/, levelData[mastery]) × army::AI_value(target)` |
| 50 Sorrow | `0x4382C0` | same with a negated delta, times the spell-resistance multiplier |
| 51 Fortune | `0x438490` | luck version: recomputes damage with the extra lucky-strike chance (`+0x4EC`) |
| 52 Misfortune | `0x438F60` | negative luck version |
| 62 Blind, 74 Paralyze | `0x439100` | 0 unless `enemy_can_attack` includes the target's zone; then `(0.5 − √(levelData[mastery]/400)) × army::AI_value(target)`, or a rounds-based figure when the stack is worth less than `awake_enemy_value` |
| 71 Poison | `0x439500` | `AI_value_of_hits_lost(target, la, ld, ranged, hpLostToPoison, killsOnly)` |
| 73 Disease | `0x438A10` | attack **and** defence loss: `(1 − 0.9) × value` scaled by the resistance multiplier |
| 75 Age | `0x4373F0` | 0 if `win_likely`, else `army::AI_value(target, la, ld) / 3` |
| 30/31/32/33 Protection from Air/Fire/Water/Earth | `0x4399A0` / `0x4399D0` / `0x439A40` / `0x439A00` | all → `get_protection_value` (§5C.6) |
| 34 Anti-Magic | `0x439D40` | `get_protection_value` over **all** schools up to `levelData[mastery]`, plus `get_cancel_value` on a scratch copy |
| 36 Magic Mirror | `0x439DE0` | `get_protection_value` weighted by the reflect chance |
| 35 Dispel | `0x439BC0` | `get_cancel_value(copy, false)` (§5C.7) |
| 37 Cure | `0x439C30` | `get_cancel_value(copy, true)` **plus** healed hits × `AI_value_per_hit` |
| 29 Fire Shield | `0x43A110` | expected reflected damage over the remaining rounds, `(√x − 1)`-shaped |
| 58 Counterstrike | `0x439E80` | value of the extra retaliations: recomputes `compute_damage` per extra strike |
| 42 Curse | `0x43B370` | damage **reduction** on an enemy: multiplier from `averageDamage` vs `levelData[mastery]`, times resistance |
| 59 Berserk | `0x43A400` | `get_traitor_value` summed over every stack the berserked unit could reach (`0x445490`) |
| 60 Hypnotize | `0x43A500` | `get_traitor_value` on the best reachable victim, using combat reachability (`0x4B2DA0`) |
| 61 Forgetfulness | `0x43B500` | shooter's lost damage × `AI_value_per_hit`, times resistance |
| 65 Clone | `0x43B2E0` | `AI_value_of_hits_lost` of the damage the clone would inflict before dying |
| 19 Chain Lightning | `0x437190` | §5C.8 |
| 38 Resurrection, 39 Animate Dead | `0x43ACA0` | resurrected hits × `AI_value_per_hit`, capped by the original stack size |
| 40 Sacrifice | `0x43B1E0` → `0x43AF50` | value gained on the resurrected stack minus `army::AI_value` of the sacrificed one |
| 63 Teleport | `0x43AA60` | re-runs `AI_choose_target` (`0x421F80`) from every candidate hex and takes the best delta |
| 64 Remove Obstacle, 70 Stone Gaze, 72 Bind, 66–69 Elementals, 20–26 area/mass | `0x43B680` (none) | handled inline in `consider_spell` |

### `get_traitor_value(attacker, target)` @ `0x43A340`

Used by Berserk and Hypnotize:

```c
if (target->side == this->our_group) return 0;
int aHits = get_total_hits(attacker, 0), bHits = get_total_hits(target, 0);
int a2 = aHits, b2 = bHits;
estimate.simulate_attack(attacker, &a2, target, &b2, is_shooter(attacker), 0);
return AI_value_of_hits_lost(attacker, la, ld, ranged, aHits - a2, killsOnly)
     + AI_value_of_hits_lost(target,   la, ld, ranged, bHits - b2, killsOnly);
```

**Both sides' casualties count as gains** — which is why the AI loves Berserk on a big melee stack
standing in a crowd.

## 5C.6 `get_protection_value(target, schoolMask, maxLevel, duration, amount)` @ `0x4396E0`

```c
if (!cm->enemy_hero_can_cast(enemy_group, 1)) return 0;    // 0x41F890
if (this->win_likely) return 0;
if (target->flags & (1<<23)) return 0;                      // already immune
int enemyPower = cm->d[0x53D4 + enemy_group*4];
int total = 0;
for (spell = 0; spell < 70; ++spell) {
    if (!(schoolMask & spellTraits[spell].schoolMask /*+0x1C*/)) continue;
    if (!(spellTraits[spell].b[0x0D] & 2)) continue;        // must be a damage spell
    if (!(spellTraits[spell].b[0x0C] & 1)) continue;        // must be usable in combat
    if (spellTraits[spell].level > maxLevel) continue;
    if (!enemyHero->b[0x430 + spell]) continue;             // the enemy must actually know it
    if (!cm->spell_can_affect(spell, enemy_group, target, 1, 0)) continue;
    mastery = hero_spell_mastery(enemyHero, spell, cm->d[0x53C0]);
    if (hero_spell_cost(enemyHero, spell, …) > enemyHero->mana) continue;   // and afford it
    int dmg = spellTraits[spell].powerFactor * enemyPower + spellTraits[spell].levelData[mastery];
    total += value of the fraction `amount` of that damage which the protection absorbs;
}
return ftol(total × durationFraction);
```

This is the sharpest piece of reasoning in the whole spell AI: **the protection spells are priced by
enumerating the specific damage spells the opposing hero knows, is high enough level for, and has the
mana to cast.** Protection from Fire against a hero with no fire magic scores exactly 0.

## 5C.7 `get_cancel_value(target, badSpellsOnly)` @ `0x439A80`

```c
int total = 0;
for (spell = 10; spell < 70; ++spell) {
    if (target->spellDuration[spell] == 0) continue;        // target + 0x198 + spell*4
    if (badSpellsOnly) { if (spellTraits[spell].targetSign >= 0) continue; }   // Cure: only harmful
    else               { if (spell == 35) continue; }                          // Dispel: not itself
    EffectFn fn = get_enchantment_effect_fn(spell);
    if (!fn) continue;
    type_enchant_data d;  army::get_active_spell_data(target, spell, &d);      // 0x444510
    bool ours    = (target->side == this->our_group);
    bool harmful = (spellTraits[spell].targetSign < 0);
    if (harmful == ours) total += fn(this->enemy_caster, target, d);   // priced through the OPPONENT's model
    else                 total -= fn(this,               target, d);
}
return total;
```

Dispel is the sum of every active enchantment's value, signed. The `enemy_caster` branch is the reason
the constructor builds a mirror spellcaster: an enemy's Bless has to be valued with the enemy's
`estimate`, `win_likely` and threat tables, not ours.

## 5C.8 Chain Lightning — `0x437310` / `0x437190`

```c
int hops = chainHops[mastery];                  // 0x63B7C8 = {4, 4, 5, 5}
cm->clear_chain_marks();                        // 0x5A66B0
int damage = spellTraits[19].powerFactor * power + spellTraits[19].levelData[mastery];
int friendlyVal = 0, enemyVal = 0;
army* t = target;
for (h = hops; h > 0; --h) {
    if (t->side == our_group) friendlyVal += get_damage_value(19, damage, current_hero, t);
    else                      enemyVal    += get_damage_value(19, damage, enemy_hero,  t);
    cm->b[0x547C + t->side*20 + t->d[0xF8]] = 1;
    int next = cm->find_next_chain_target(t, 0);            // 0x5A61F0
    if (next < 0 || next >= 187) break;
    t = stack_at_hex(next);
    damage /= 2;                                            // halves each hop
}
// then the standard proportional test of §5A.12
```

`0x437310` is the wrapper that tries every legal first target and keeps the best.

## 5C.9 `get_damage_value` @ `0x436E30` — the one non-obvious detail

```c
if (target dead or an Arrow Tower) return 0;
int raw = cm->compute_spell_damage(spell, baseDamage, current_hero, targetHero, target, 0);
int dmg = ftol(cm->get_spell_damage_multiplier(spell, our_group, target, 0, 1, is_creature_spell) * raw);
if (dmg <= 0) return 0;
dmg = min(dmg, get_total_hits(target, 0));
int v = AI_value_of_hits_lost(target, la, ld, is_shooter(target), dmg, this->b[0x28] /*retreating*/);

// PENALTY, not a bonus: if the target is already blinded / paralysed / petrified / dead,
// or is a First Aid Tent or Ammo Cart, the value is pulled back toward zero
if (target is disabled, dead, or a tent/cart) {
    int full = army::AI_value(target, la, ld);
    if (full > v) v = 2*v - full;                 // negative when the stack is worth more than the damage
}
return v;
```

That branch is easy to misread as a finishing-blow bonus. It is the opposite: it stops the AI wasting
damage spells on stacks that are already harmless.

## 5C.10 The mass/area gate

`get_mass_damage_effect(enemyDamage, friendlyDamage)` @ `0x436FB0` and the tail of
`get_area_effect_value` @ `0x437040` share one rule:

```c
if (enemyDamage <= 0) return 0;
if (friendlyDamage < 0) friendlyDamage = 0;
if (enemyDamage <= friendlyDamage) return 0;
if (friendlyDamage / friendly_combat_value >= enemyDamage / enemy_combat_value) return 0;
if (friendlyDamage >= friendly_combat_value) return 0;      // don't wipe ourselves out
return enemyDamage - friendlyDamage;
```

Armageddon is cast when it costs the enemy a **larger fraction** of their army than it costs us of
ours — not merely more absolute damage. That is why an AI with a few Black Dragons will Armageddon a
much larger army.

## 5C.11 Constants used by this section

| Address | Value | Used by |
|---|---|---|
| `0x63B7C8` | `{4, 4, 5, 5}` | Chain Lightning hops by mastery |
| `0x63B7E0` | `0.9` | Disease attack/defence factor |
| `0x63B7E8` | `400.0` | Blind / Paralyze denominator |
| `0x63AC50` | `1.0` | the `−1` in `(√x − 1)` |
| `0x63AC58` | `0.05` | the ±5 %-per-point damage law |
| `0x63AC70` | `0.5` | Blind base chance, blinded-stack discount |
| `0x63AC38` | `0.0` | comparison zero |
| `0x63B780` / `0x63B788` | `0.0173` / `−0.0833` | Mirth / Sorrow (morale) |
| `0x63B790` / `0x63B794` | `0.0173` / `−0.0122` | Fortune / Misfortune (luck) |
| `0x63B7F0` | `{1.0, 1.5, 1.5, 2.0}` | Artillery skill multiplier |
| `0x63B798` | `{2.6, 1.9, 1.5, 1.31, 1.2, 1.13}` | `rounds_left` estimate |

## 5C.12 `army` fields the spell AI reads

| off | meaning |
|---|---|
| +0x198 + spell·4 | remaining duration of that spell on this stack |
| +0x228 | magic resistance present |
| +0x4AC | attack rating (the field the buffs modify on the scratch copy) |
| +0x4E8 / +0x4EC | morale / luck |
| +0x538 / +0x53C | planned target / expected damage value (filled by `AI_compute_expected_damage`) |
| +0x544 | bitmask of own stacks this stack can reach |
| +0x64 | speed |
| +0xF8 | zone id (used for every `1 << zone` mask) |

---

# 6. Things worth knowing that fall out of this

1. **Difficulty changes the AI's *risk model*, not its *skill*.** `0x6604D0` scales the simulated enemy
   by 0.5 on Easy/Normal and 1.25 on Expert/Impossible. Three further behaviours are gated on
   `difficulty > 0`: retaliation is simulated at all (`simulate_attack`), the splash/breath and
   retaliation terms in the hex chooser, and the jousting-distance preference for Cavaliers.
   A fourth, `AI_score_hexes_special` (`0x41FD60`), needs `difficulty >= 2`.
2. **Two independent ±25 % randomisations.** Battle target choice (`0x421AF8`) and battle spell choice
   (`0x43CA60`) both multiply the final score by `Random(75,100)/100`. The band is wide enough to
   reorder genuinely different candidates, so no deterministic reimplementation reproduces vanilla.
3. **The adventure AI is a pure influence-map greedy planner.** No lookahead, no multi-turn plan beyond
   "keep walking to the destination I picked", no cooperation between heroes except through the shared
   `value_map`, and the decay `300/(300+cost)` is the only spatial reasoning it has.
4. **The AI cannot plan through portals.** Monoliths, subterranean gates and whirlpools all evaluate to
   0 and are never chosen as destinations. Neither are Taverns, Trading Posts or Cartographers.
5. **Negative morale is valued 4.8× more harshly than positive morale** (0.0833 vs 0.0173), and negative
   luck only 1.22 %. That asymmetry drives a lot of AI army composition.
6. **The `−1e9` hard block** in the danger map means a hero that would certainly lose to an enemy hero
   will refuse to enter *any* cell that hero can reach — including cells containing objectives.
7. **`hero+0x109` (value of one XP)** is the exchange rate between the combat/level economy and every
   other reward: `(2500 + armyValue) / (40 × expForNextLevel)`. It rises with army strength and falls
   as the hero levels, which is why strong high-level AI heroes stop detouring for experience.
8. **Resource values are the AI's real economy.** A bottleneck resource is floored at
   `baseValue / tradeRate` — up to 10× its market value with a single marketplace — while a surplus
   resource collapses to its trade rate. Everything the AI buys or walks toward is priced through
   `AI_resource_cost`, so this one table drives most kingdom behaviour.
9. **Building value flows backwards through prerequisites.** Forts, Mage Guilds and Halls score 0 on
   their own; they get their value from the closure step in `AI_build_one_building`.
10. **The battle AI never re-plans mid-turn.** `AI_take_turn` produces exactly one action code in
    `combatManager+0x3C`; everything above it is scoring.
11. **Only five creature abilities are AI-driven** (Archangel, Pit Lord, Master Genie, Ogre Magi,
    Faerie Dragon). Enchanters, Sharpshooters and everything with an id ≥ 136 fall outside the
    dispatch range entirely.
12. **The spellcaster models its opponent.** Its constructor allocates a second
    `type_AI_spellcaster` for the enemy side, because Dispel has to price an enemy's buff using the
    enemy's own `estimate`, `win_likely` and threat tables.
13. **`win_likely` is a master off-switch.** When the AI can already wipe the enemy out, fifteen of the
    spell evaluators return 0 immediately — it stops buffing and debuffing and just attacks.
14. **Protection spells are priced against the actual opposing spellbook.** `get_protection_value`
    enumerates every damage spell the enemy hero knows, is high enough level for, and can afford;
    Protection from Fire against a might hero scores exactly 0.
15. **AI armies flow one way only.** `take_best_stack` scores every candidate as
    `skillDiff × valueOfAdding / 40` where `skillDiff` is the receiving hero's primary-skill total
    minus the giver's, clamped at 0. An equal-or-weaker hero scores 0 and receives nothing, so troops
    accumulate in one main hero and never trickle back.
16. **Towns are worth 5 000 000.** The first town captured is worth a flat five million; every
    fortified town thereafter is worth `5'000'000 / numTowns`, both on capture and on every revisit.
    Nothing else on the map comes close, which is why the AI rushes towns and keeps going home.
17. **Allied AIs bail each other out, but only in a crisis.** Surplus resources are gifted to level
    the two players out, keeping 10 000 gold / 20 units in reserve, with a minimum gift of
    1000 gold / 5 units and only when the gift is at least 5× what the ally already holds.
18. **Slow creatures are priced in lost movement.** `value_of_adding` subtracts
    `armyValue × (1 − movementForSpeed[newSpeed] / movementForSpeed[slowestExisting])` whenever the
    newcomer would become the army's slowest stack. That term is frequently large enough to go
    negative, which is why a fast AI hero walks past creatures it could afford.
19. **The AI stops hiring heroes when the humans get ahead.** Two difficulty tables cap it: the AI
    will never own more than 2/3/4/5/6 heroes, and once the human players *collectively* own
    8/11/14/17/20 heroes it stops buying altogether. A tavern hero is priced by the artifacts it
    carries, not by its stats.
20. **There is almost no inter-hero coordination.** `AI_build_reachability` re-runs the pathfinder
    from every other friendly hero and strikes out the cells it already covers. That, plus the shared
    value map, is the entire mechanism — no assignment, no negotiation, no re-planning.
21. **Two reproducible quirks worth keeping.** `AI_score_hexes_moat` passes `lowest_attack` for both the
    attack and defence argument of `AI_value_of_hits_lost`; and `type_AI_combat_parameters` fields
    `+0x08` / `+0x09` are used in the opposite sense to NH3API's `kills_only` / `simulated` names.

# 6A. Community-documented behaviour, checked against the binary

The HoMM fandom wiki's *AI Behavior* page makes a number of specific claims, most of them derived
from black-box experiment. They are useful as **hypotheses to test**, and testing them turned up one
routine this report had never opened and one correction to a conclusion it had asserted twice.

Nothing in this section is taken from the wiki: every entry below is either confirmed from the
disassembly, refined by it, or shown not to exist in the code.

## 6A.1 The three AI personalities — **confirmed, and narrower than described**

The claim: *"AI randomly gets one of three behaviors: Builder, Warrior or Explorer… As a Builder, AI
will act more in defense, focusing on the towns development, limiting the scope of exploring. A
Warrior will seek to fight more… An Explorer will focus on hiring more heroes and moderate
aggression."*

**The field exists.** `playerData + 0x34` is a 4-valued enum. Its names are loaded from
`ARRAYTXT.TXT` by the initialiser at `0x5B9E3E`:

```c
edx = 0x6A7794;
do { *edx++ = lines[ecx++]; } while (edx < 0x6A77A4);      // exactly 4 entries
```

The loader walks `ARRAYTXT.TXT` sequentially, and the block immediately before this one is the
ten `cRumourTerrainDescriptions` lines (`0x6A5D24…0x6A5D4C`, 10 entries), followed by one `inc ecx`
that steps over the `cPersonality` section marker. The four lines that follow it in the shipped file
are **Warrior, Builder, Explorer, Human** — so

| `playerData + 0x34` | name |
|---|---|
| 0 | Warrior |
| 1 | Builder |
| 2 | **Explorer** |
| 3 | Human |

### The field's complete lifecycle

The strongest evidence is not a search but a **closed trace**: where the value is born, everywhere it
travels, and where it dies. All five sites are accounted for.

| # | site | what it does |
|---|---|---|
| **write** | `0x4BA3F6`, inside `playerData::deserialise` (`0x4BA260`) | reads **one byte** from the map/save stream and stores it to `+0x34`. Each field in that routine takes its own `stream->vft[1](&buf, 1)` call, so this byte is not shared with any neighbour: `+0x30`, `+0x34`, `+0x38`, `+0x3D`, `+0x3E`, `+0x3F` and `+0x00` are read one after another as separate bytes |
| write | `0x4B9EAD`, inside `playerData::reset` (`0x4B9E20`) | zeroes it, along with the Grail guess at `+0x39/+0x3B` and the rest of the record |
| copy | `0x58F7A9` (`playerData` copy-assign) and `0x4EF32B` (the save serialiser) | field-by-field struct copies; neither derives anything from the value |
| **read** | `0x5DE793` | `g_aiPersonalityName[value]` — display only |
| **read** | `0x4E4B9A` | the `== 2` test that grants +50 movement |

The neighbouring bytes from the same map record were checked too, in case the personality fed a
second field: `+0x38` has exactly one consumer (`0x4BAFE7`, the bonus puzzle-piece count of §4.14)
and `+0x30` and `+0x3F` have **none at all** in this build.

So the value is loaded, displayed, and used once. It is not expanded at load time into per-player
weights, multipliers or flags — there is no such derivation anywhere between `0x4BA3F6` and the two
reads.

### How thoroughly the negative was checked

The claim "the personality has one consumer" is a **negative**, and negatives are easy to get wrong.
An absolute-address xref search alone is *not* sufficient evidence: it only matches the fused form
`[reg*8 + 0x20B04]`, and misses the far more common idiom where a `playerData*` is materialised once
and every field then read as `[reg + 0x34]`. Three independent sweeps were used.

**Sweep 1 — the fused displacement.** Exhaustive over instruction operands: `0x20B04` appears at
exactly **three** sites — `0x4C0193` (a base-pointer computation in the save/load walker),
`0x5DE793` (the name lookup above), and `0x4E4B9A` (the movement grant).

**Sweep 2 — the pointer-plus-offset idiom.** 145 functions in the image materialise a `playerData*`
(via `0x20AD0`, `0x20B04`, or `gpCurPlayer` at `0x69CCB0`). Every `+ 0x34` access anywhere inside
those 145 functions was enumerated — 40 sites — and each was classified by what its base register
actually holds:

| sites | base register really holds | how it was identified |
|---|---|---|
| 7 | a vftable | `call dword ptr [reg + 0x34]` — dispatch, not a data read |
| 12 | `army + 0x34` = **creature type** (§2) | all in `0x45xxxx`/`0x46xxxx`, the combat manager |
| 1 (`0x4B8CC2`) | `town + 0x34` | base is `gpGame + 0x20BD8` stepping by `0x168`; the same block reads `+0x158/+0x15C`, the town built-buildings mask (§4G.5) |
| 3 (`0x4087BB`, `0x4E368B`, `0x55398E`) | `gpMapMgr + 0x34` | base loaded from `[0x699268]` |
| 2 (`0x4CDAD9`, `0x4CDB1D`) | `gpGame + 0x34` | the enclosing function opens with the calendar read at `gpGame + 0x1F642` |
| 1 (`0x520711`) | a `std::vector` `_First` | `+0x34` and `+0x38` compared to each other, then iterated |
| 1 (`0x4076EA`) | `advManager + 0x34` | the literal `"advManager"` is loaded two instructions later |
| 1 (`0x4DA296`) | not an enum | compares against `0x9E` |
| 3 | the serialiser (`0x4EE3E0`), the save/load walker (`0x4C00DF`), the name lookup (`0x5DDA10`) | field-by-field struct copy / display |
| **1** | **`playerData + 0x34`** | `0x4E4B9A` — the movement grant |

**`town` is also `0x168` bytes**, so the `+= 0x168` stride does not distinguish the two structures —
that is exactly the trap the `0x4B8CC2` row fell into on the first pass.

**Sweep 3 — from the value side.** A 4-valued enum can only be used behaviourally by comparing it
against a literal or by indexing a table with it. Every `cmp [reg + 0x34], 0…3` in the image (22
sites) and every `movzx`/`movsx` widening of a `+0x34` byte (2 sites) was enumerated and classified;
all resolve to the structures above. The only table indexed by the field is the name table at
`0x5DE79F`.

### The finding

**The personality has exactly one gameplay consumer in the whole image**: the movement grant at
`0x4E4B9A`. There is no personality test in `AI_build_one_building` (§4A.4), `compute_wants`
(§4A.1), `AI_hire_hero` (§4B.9), `AI_value_of_combat` (§4.11), the object valuations (§4.8), the
army planner (§4B.4) or the turn driver (§4.1).

So "Builders develop towns, Warriors fight more, Explorers hire more heroes" is **not implemented in
`h3.exe`** — at least not through this field. The only mechanical difference between the three is
movement points.

A read could in principle still hide behind a `playerData*` passed as an *argument* into a helper
that never materialises `0x20AD0` itself, which sweep 2 would not see. Two things close that: sweep 3
(such a helper would still have to compare or index the value, and every comparison and widening in
the image is classified above), and the lifecycle trace (the only sites that *copy* the field are the
two struct copies, so it is never staged anywhere a helper could read it from).

### So why does the description ring true?

The wiki's characterisations are plausible **observations**, and one of them has a mechanism after
all — just not the one implied. An Explorer AI hero starts each day with **+125** movement on Expert
and Impossible against every other AI's +75, and covers visibly more ground per turn. "Explorers
explore" is real; it is a movement grant, not a different decision policy.

The other two have no counterpart in this binary. What varies between AI players instead is
everything the report already documents: town faction (which changes the whole build tree of §4A.4
and the faction-special table of §4G.5), starting resources feeding `compute_wants` (§4A.1), the
per-hero random combat modifier `hero + 0x47A` (§5D.1), and the five difficulty tables (§6A.3). Two
AI players with the same personality and different towns will play very differently; two with
different personalities and the same town will not, beyond movement.

One caveat on scope: this is **Heroes3.exe, Complete 4.0** (§0). Nothing here speaks to HotA, the HD
mod, or any later community patch, several of which do rework AI behaviour.

## 6A.2 "Explorer AI heroes get 50 more movement points" — **confirmed, with two additions**

The tail of `hero::compute_max_movement` (`0x4E4990`, land path) is:

```c
// 0x4E4B58
int owner = (int8)hero->b[0x22];
if (owner >= 0 && owner < 6                          // ← players 6 and 7 are excluded
    && !is_human(owner)                              // 0x4CE940 — AI players only
    && gpGame->b[0x1F6D8] > 2) {                     // ← Expert and Impossible only
    mp += 75;                                        // EVERY AI hero
    if (playerData[owner].d[0x34] == 2 /*Explorer*/)
        mp += 50;                                    // this personality only
}
return mp;
```

Three things the black-box testing could not see:

* The `+50` is **conditional on difficulty**. It fires only when `gpGame->b[0x1F6D8] > 2`, i.e. on
  Expert and Impossible. On Easy, Normal and Hard an Explorer AI moves exactly like any other.
* There is a **larger, unconditional `+75` for every AI hero** under the same difficulty condition,
  regardless of personality. An AI hero on Impossible therefore starts the day with **+75** movement,
  or **+125** if its player is an Explorer.
* `owner < 6` means **the seventh and eighth player colours get neither bonus**. Eight colours exist
  and are handled everywhere else; this looks like a shipped off-by-one, not a design choice.

## 6A.3 The difficulty word — this is what settles it

A flat movement grant to AI heroes can only be a hard-mode cheat, and it fires at indices 3 and 4.
That fixes the direction of `gpGame + 0x1F6D8` as **0 = Easy … 4 = Impossible**, matching the menu.

**Two earlier sections of this report asserted the opposite** and have been corrected (§4G.1, §4.14,
§5D.2). The mistake came from reading `g_attack_computer_bonus` as a *cheat* — "the AI gets +1.00 at
index 0, so index 0 must be hardest" — when it is in fact a *handicap*: the number inflates how the
AI rates its own armies, so a large value makes it attack into fights it loses. Every one of the five
difficulty-indexed tables now reads consistently in the same direction:

| table | on Easy (0) | on Impossible (4) |
|---|---|---|
| `0x6604D0` combat modifier vs a human | 0.50 — treats a human army as half strength | 1.25 — full weight |
| `0x6604F8` / `0x6604FC` attack bonuses | overrates itself, underrates the human | underrates itself, overrates the human |
| `0x6822C8` Grail threshold | 1.10 — never hunts the Grail | 0.00 — hunts from turn one |
| §4G.6 supply tests 2 & 3 | run (refuses level-7 creatures, caps ambition) | skipped |
| `0x4E4B58` AI hero movement | none | **+75**, or +125 for an Explorer |

The lesson is in `METHODOLOGY.md`: a single difficulty-indexed table cannot tell you which end is
which, because a bonus and a handicap look identical in isolation. Five tables that must all point
the same way can.

## 6A.4 The object-priority list — **there is no priority list**

The wiki gives an ordering derived by repeatedly offering the AI two objects:

> Obelisk > Creature Dwelling > Windmill > Wagon > Learning Stone > {Observatory, Grave, Gold} >
> {Scholar, Witch Hut} > Corpse > {Garden of Revelation, Star Axis} > Hut of the Magi

**No such table exists in the binary.** `AI_object_value` (§4.8) computes a number per object from
player and hero state, and `AI_choose_destination` (§4.5a) scores each candidate as

```c
score = (cost > 100) ? value * 100 / cost : value;   // floored at 1
```

so the ranking is a value-per-movement-point rate, not an ordering of object types. Most of the
values in that list are **state-dependent**, which is why a fixed ordering emerges only for one
particular hero on one particular map:

| object | value (§4.8 / §4.8a) | depends on |
|---|---|---|
| Obelisk | `AI_value_of_artifact(Grail, player) / obeliskCount` | the Grail's worth to this player — normally the largest number in the list by orders of magnitude |
| Creature dwelling | `type_AI_army_planner::value_of_adding` | the hero's army and the treasury |
| Windmill | `playerData->d[0x160] * 9 / 2` | the average non-gold resource value |
| Wagon | `artifactValue * 0.4 + playerData->d[0x160] * 7 / 4` | resource values **and** the average artifact value |
| Learning Stone / Scholar | `expForNextLevel(level) * hero->xpValue` | hero level and army size (§4.9) |
| Redwood Observatory | newly revealed tiles + per-object bonuses | how much fog is left nearby |
| Corpse | `artifactValue / 5` | the average artifact value |
| Garden of Revelation | `hero->d[0x486]` (+1 knowledge) | the hero's spellbook — **floored at 10** |
| Star Axis | `hero->d[0x47E]` (+1 spell power) | the hero's spellbook — **floored at 10** |
| Hut of the Magi | `AI_player[owner].d[0x04]` | the number of Eyes of the Magi on the map (§4G.3) |

Two of the reported orderings do fall straight out of the formulas and are worth stating, because
they hold for essentially any early-game hero:

* **Corpse beats Garden of Revelation and Star Axis** whenever the hero has no spell book. Both of
  those return `hero->d[0x47E]` / `hero->d[0x486]`, which `AI_update_valuations` floors at **10** when
  `type_spellvalue` bails for a bookless hero (§4.9b); the Corpse returns `artifactValue / 5`, and
  `artifactValue` is the mean over ~130 artifacts (§4G.1) — hundreds at least.
* **Hut of the Magi comes last on a map with no Eyes of the Magi**, because `0x429910` sums over Eye
  objects and returns 0 when there are none. On a map that has them it is not last at all.

The rest of the list is a property of the test map, not of the AI.

## 6A.5 The left/right tie-break — **the mechanism exists; the direction is a consequence, not a rule**

The claim: *"the AI always visits the object that's on its left side rather than the object that's on
its right side"*, with a noted exception where the choice looks random.

There is no left/right test anywhere in the chooser. What exists is a **strict** comparison:

```c
// 0x42EAF3
cmp edi, [ebp-0x28]      ; score vs best
jg  accept               ; strictly greater
```

so when two candidates score equally the **first one in the scan list wins**, permanently. The scan
list is built by `AI_scan_objects` walking the pathfinder's reachable-cell vector (`sa + 0x5C/0x60`)
in order, and that vector is filled by the **LIFO label-correcting flood** of §4D.1 — which pops the
most recently pushed cell first, so it visits neighbours in the *reverse* of the order they were
pushed.

The chooser's own neighbour table lives at **`0x678150`**, eight 4-byte records `{int8 dx, int8 dy,
0x10, 0x00}`:

| # | dx, dy | direction |
|---|---|---|
| 0 | 0, −1 | N |
| 1 | +1, −1 | NE |
| 2 | +1, 0 | **E** |
| 3 | +1, +1 | SE |
| 4 | 0, +1 | S |
| 5 | −1, +1 | SW |
| 6 | −1, 0 | **W** |
| 7 | −1, −1 | NW |

— a clockwise sweep from North, in which **east precedes west**. Pushed in that order and popped
LIFO, west is reached first, which is consistent with a systematic bias toward the left-hand object
on equal scores. This report stops short of calling that *proven*: the table above is the
destination chooser's, and `searchArray::compute` has its own expansion order that has not been
traced cell-by-cell. What **is** proven is the part that matters for a reimplementation — that ties
are broken by scan-list order and never re-examined, so **any change to the flood order changes which
of two equal objects the AI walks to**. §4D.1 already warned that substituting a priority queue for
the LIFO would do exactly this; the wiki's observation is the visible symptom.

The reported "seems to choose randomly" exception is what you would expect when the two objects are
*not* actually equal — a pile of gold is `amount x resource_value[6]` and a Grave is a combat plus
loot roll, so their relative order changes with the treasury.


# 7. Suggested NH3API additions

Nothing in NH3API currently covers the *drivers* — only the leaf value classes. The following are
un-named in the headers and are the pieces a mod actually needs to hook:

| Purpose | This build | Suggested name |
|---|---|---|
| Adventure AI turn driver | `0x525E80` | `advManager::AI_take_turn(int player)` |
| Pick next special hero | `0x526A90` | `AI_pick_special_hero` |
| Per-hero wrapper | `0x5261F0` | `AI_hero_turn` |
| Hero turn core | `0x5267B0` | `hero::AI_take_turn` |
| Destination chooser / influence map | `0x42E0B0` | `hero::AI_choose_destination` |
| Object scan | `0x42EDD0` | `hero::AI_scan_objects` |
| Danger map | `0x42DE50` | `hero::AI_add_enemy_threats` |
| Move execution | `0x42FEE0` | `hero::AI_move_to_destination` |
| **Object value (the big switch)** | `0x528040` | `hero::AI_object_value` |
| Paid-object helper | `0x529810` | `hero::AI_pay_for_object` |
| Hero valuation refresh | `0x527770` | `hero::AI_update_valuations` |
| Kingdom management | `0x428DD0` | `type_AI_player::manage_kingdom` |
| Resource value recompute | `0x429D50` | `type_AI_player::compute_resource_values` |
| Greedy purchase step | `0x42AE00` | `type_AI_player::do_one_purchase` |
| Step-list builder | `0x430610` | `hero::AI_build_step_list` |
| Victory cond. — fort/dwellings | `0x42B670` | `AI_town_dwelling_value` |
| Victory cond. — hall/buildings | `0x42B8B0` | `AI_town_building_income_value` |
| Victory-condition checker (engine) | `0x5139E0` | `game::CheckSpecialVictory` |
| Object index at coord | `0x4BB870` | `GetObjectIndexAt(x,y,z)` |
| Engine path search | `0x56A0D0` | `searchArray::build_path` |
| Hero hiring — choose | `0x431360` | `type_AI_player::AI_hire_hero` |
| Hero hiring — execute | `0x431800` | `type_AI_player::AI_buy_hero` |
| Hero hiring — per-town term | `0x431BD0` | `town::AI_hero_arrival_value` |
| Army planner from a town | `0x42D1B0` | `type_AI_army_planner::init_from_town` |
| Hero has artifact | `0x4D91F0` | `hero::HasArtifact` |
| Any hero has artifact | `0x4BACB0` | `playerData::AnyHeroHasArtifact` |
| Town garrison accessor | `0x5C1460` | `town::GetGarrison` |
| Hire a tavern hero | `0x5C12E0` | `town::HireHero` |
| Battle AI entry | `0x4221F0` | `combatManager::AI_take_turn(int side)` |
| Battle AI dispatcher | `0x421F80` | `combatManager::AI_stack_turn` |
| **Battle target selection** | `0x421680` | `combatManager::AI_choose_target` |
| Hex scoring passes | `0x4214F0`, `0x421590`, `0x420260`, `0x41FB60` | `AI_score_hexes_*` |
| Slot displacement chooser | `0x42C690` | `AI_army_planner::pick_slot_to_displace` |
| Hex ring collector | `0x420060` | `combatManager::collect_hexes` |
| Hex in direction | `0x523DF0` | `army::hex_in_direction` |
| Creature ability dispatch | `0x421280` | `combatManager::AI_creature_ability` |
| War-machine turn | `0x41F060` | `combatManager::AI_war_machine_turn` |
| Side strength | `0x420A80` | `combatManager::AI_evaluate_sides` |
| Siege wall decision | `0x4213F0` | `combatManager::AI_should_break_wall` |
| `Random(lo,hi)` | `0x50B230` | `random_range` |
| Artifact count (equipped / backpack) | `0x4D90C0` | `hero::AI_artifact_count(bool backpack)` |
| Same-team test | `0x5296D0` | `NewGame::same_team(a, b)` |
| `hero::get_player_data()` | `0x4E56B0` | `&gpGame->playerData[owner]` |
| `hero::AI_priority()` | `0x4E5960` | Σ of `hero+0x476..0x479` |
| `experience_for_level` | `0x4DA420` | already partly known |
| `cell_visibility(x,y,z)` | `0x4F79B0` | `NewfullMap::get_visibility_mask` |
| `army::AI_value_per_hit` | `0x442A50` | core unit valuation |
| `army::AI_value_of_hits_lost` | `0x442FD0` | hits → value |
| `combatManager::AI_compute_move_order` | `0x41F140` | writes `army+0x190` |
| `combatManager::AI_compute_expected_damage` | `0x422B20` | fills `army+0x538/0x53C/0x540/0x544` |
| `combatManager::AI_get_ranged_bonus` | `0x41F3B0` | focus-fire term |
| `combatManager::AI_get_incoming_damage_value` | `0x41F920` | includes enemy mass spells |
| `combatManager::AI_execute_attack` | `0x41F580` | issues the move+attack |
| `combatManager::AI_should_use_catapult` | `0x4213F0` | siege gate |
| `type_AI_player::compute_wants` | `0x428740` | supply/demand/resource values |
| `type_AI_player::get_total_value` | `0x42A150` | `base × 1000 / cost` |
| `type_AI_player::AI_plan_trades` / `AI_do_trades` | `0x42A2B0` / `0x42A580` | marketplace planner |
| `type_AI_player::reserve_funds` | `0x42A470` | |
| `type_AI_player::AI_build_one_building` | `0x42AE00` | town build AI |
| dwelling / upgrade / horde / hall evaluators | `0x42B520` / `0x42B5B0` / `0x42B790` / `0x42B800` / `0x42B8B0` | |
| `AI_artifact_purchase_value` | `0x529750` | |
| `AI_get_spell_value_for_object` | `0x5298D0` | shrine / Pandora spells |
| `hero::get_primary_skill_sum` | `0x4E5960` | hero ordering + luck/morale scale |
| `hero::get_player_data` | `0x4E56B0` | |
| `type_AI_spellcaster::get_enchantment_effect_fn` | `0x43B690` | spell → evaluator table |
| `get_attack_skill_value` | `0x437800` | offensive-buff master formula |
| `get_defense_skill_value` / helper | `0x438910` / `0x4387C0` | defensive-buff master formula |
| `get_speed_value` | `0x439550` | |
| `get_protection_value` | `0x4396E0` | enumerates the enemy hero's damage spells |
| `get_cancel_value` | `0x439A80` | Dispel / Cure, uses `enemy_caster` |
| `get_traitor_value` | `0x43A340` | Berserk / Hypnotize |
| `get_damage_value` | `0x436E30` | |
| `get_mass_damage_effect` / `get_area_effect_value` | `0x436FB0` / `0x437040` | proportional gate |
| `get_chain_lightning_value` | `0x437190` (wrapper `0x437310`) | |
| `set_melee_enemies` / `find_enemy_attacks` | `0x43BF20` / `0x43C040` | threat tables |
| per-spell evaluators (36) | see §5C.5 | reachable only via `0x43B690` |

---

# 8. Confidence and gaps

**Fully traced, formulas read directly off the instruction stream:** both turn drivers; the influence
map and its decay law; the danger map; `AI_value_of_combat` and the difficulty table; the complete
object dispatch table with the formulas for every handler in §4.8; luck / morale / XP / resource /
artifact valuation; the resource-value economy with its trade-rate and base-value tables; the
town-building AI including the prerequisite closure and every per-building evaluator; the town value,
capture and visit evaluators; the `type_AI_army_planner` troop-exchange and recruitment logic; ally
resource gifting; the battle-AI target loop and both randomisations; all four hex-scoring passes with
the moat tables; the attack-hex chooser; the move-order keys; `AI_value_per_hit` and
`AI_value_of_hits_lost`; `simulate_attack` and all five derived scores; `type_AI_combat_parameters`
including the rounds table; the creature-ability dispatch; the spell dispatch table, the spell-damage
formula, `cast_spell`'s mana normalisation, and every per-spell evaluator in §5C including the three
master formulas, the protection and dispel routines and the mass/area gate; the quick-combat
simulator's `type_monster_data` construction, speed-category formula, modifiers, round loop and damage
distribution; the catapult and shooter-targeting gates; the army planner in full — `init_from_town`, `recruit`, and `writeback`'s **shooter slot-layout rule**; `begin_turn` (§4.16) and the two leaf valuations (§4.17), closing the report's internal call closure; the level-up secondary-skill chooser and all 28 skill arms (§4.12); the town-visit troop exchange driver `0x42BA60` (§4.13); the AI's Grail-location cache (§4.14); the negative result that no tactics-phase AI exists (§4.15); the retreat/surrender decision `0x41E570` (§5.9) with its six vetoes and the full threshold ladder; the complete adventure-spell behaviour (§4F) — the AI casts exactly four spells, with the call-site proof that no path exists for the other six; the Dimension Door daily cap and 20-mana reserve; the quick-combat spell driver's gates and category dispatch (§5B.4); the three engine primitives the AI leans on — the pathfinder `0x56B440` (including the two-plane cell index that explains the doubled level stride, the artifact flight/water-walk overrides and the flat 50-point portal cost), `army::hex_in_direction` `0x523DF0` with the precomputed `int16[187][6]` adjacency table, and `army::attack_direction_mask` `0x448AB0` with the Cerberus fan and its two ring-permutation tables; the destination-slot tail of `value_of_adding` and the displacement chooser `0x42C690`; `AI_score_hexes_special` (`0x41FD60`) including its two 187-byte visit bitmaps; the special-victory record at `gpGame + 0x1F89C` and all nine AI sites that read it (§4C); the step-list builder `0x430610` with its teleport early-exit and the two independent "no route ⇒ lose the turn" paths; the complete hero-hiring execution path — `AI_buy_hero` (`0x431800`) with its five failure exits and the Tavern-first rule, and `town::AI_hero_arrival_value` (`0x431BD0`) with its snapshot/rollback set, the simulated garrison exchange and recruitment, the branch-crediting fold and the crowding divisor; all struct offsets and globals listed above.

**Verified against the shipped data files (§4E):** creature-traits flag bit 2 = **SHOOTING**, an
exact match with `Shots > 0` across all 150 creatures — the §4B.4 inference is confirmed; creature
**47 = Cerberus**, ability text *"3-headed attack."*, confirming §4D.3; creature ids 78/79, 112–115
and 145–149; the full `traits + 0x10` bit map; and the membership of the `0x20000` alignment-free
flag. Also corrects trap 5's scope — the flags dword is **static in the image**, not runtime-loaded.

**Resolved in §4F:** search-cell bit `0x400` is the **airborne / water-walking traversal plane** —
`0x4309A0` scans for the bit to flip and casts Fly or Water Walk at exactly that step.

**Nothing is left inferred.** The last hedged label — `combat + 0x54A4 + side` — is now proven from
its write site at `0x463AA8`: `combat->b[0x54A4 + side] = is_computer(gpGame, player)`. Every
struct field, constant and table this report relies on is either read from the instruction stream,
dumped from the image, or cross-checked against the shipped data files (§4E).

**Remaining unexpanded, by choice:** the difficulty-gated tail of `0x42BA60`; `0x524D20` and
`0x524DD0` from §4.12. Neither carries an AI decision weight — they are bookkeeping and leaf
arithmetic feeding numbers into loops that are already fully specified. (The two large items this
paragraph used to list have since been opened: the quick-combat spell helpers in §5B.4 and the
Grail-area reduction in §4.14 — see the audit note below.)

**Corrected in this pass:** `army + 0x84` bit 0 is **two-hex / double-wide**, not shooter (§4D.2 —
three independent confirmations); search-cell flag bit `0x200` is a **teleport** transition, not a
boat step (§4D.1 — it is set only in the monolith/gate branch of the pathfinder).


**Resolved as non-existent:** campaign-specific AI overrides. §4C records the search and its negative result, plus the thing that does exist — scenario **victory-condition** awareness at nine valuation sites, all injecting the same constant 5 000 000.

Nothing in the "structure understood" or "not examined" groups changes any of the decision rules
documented above — they are leaf valuations and bookkeeping that feed numbers into the loops already
specified.

Tooling, the xref database and the raw dumps are in `/home/abc/h3ai/`, with
`METHODOLOGY.md` in the same directory describing how to drive them and what traps this binary sets.
`./s <addr>` prints a one-screen summary of any function; `./sr <addr>` does the same over a
recursive-descent extent (needed for anything reached only through a function-pointer table);
`./d` / `./dr` print annotated disassembly; `./x` examines data; `./jt` decodes MSVC switch tables.

## 8.1 Named but not specified — the implementation checklist

The section-by-section audit above answers "is this behaviour documented?". A second, stricter audit
answers "**can every formula in this report actually be evaluated from this report?**" — i.e. does
any pseudo-code block call something whose body is never given?

Method: take every `0x…` address in this document, subtract those that head a section or carry an
`@ 0x…` definition, and inspect what remains. Most survivors are engine primitives that are fully
specified by name and role (`map::GetObjectCell`, `town::GetGarrison`, `cell_is_explored`), which a
reimplementation maps onto its own engine. The list below is the residue: **AI logic that this report
tells you to call but does not define.**

Every entry on the original list has since been written up: `init_from_town` and `recruit` in
§4B.4, the five remaining planner primitives in §4B.4, `begin_turn` in §4.16, and
`AI_get_spell_value` / `AI_melee_exchange_value` in §4.17.

**The closure is now complete.** Every address this report tells you to call is either specified
here, or is an engine primitive fully identified by name and role (`map::GetObjectCell`,
`town::GetGarrison`, `cell_is_explored`, …) that a reimplementation maps onto its own engine.

Everything on the original list is now expanded, including `writeback` (`0x42D8E0`) — which turned
out **not** to be bookkeeping at all but the army slot-layout rule (§4B.4), and the six quick-combat
spell helpers (§5B.4). No routine this report references is left described only by role.

### Second closure pass

A later sweep, driven by an independent list of every place a downstream implementation had to guess,
found a further 30-odd routines that this report named but did not open. They are all written up now:

| what was missing | now in |
|---|---|
| `AI_get_value_of_artifact` and all 24 `type_artifact_effect` classes, plus the artifact → effect binding table | §4.9a |
| `type_spellvalue` — construction, `value_of_spell`, `get_best_spell_value`, `AI_get_spell_value`, and the probes that produce `hero + 0x47E … +0x48E` | §4.9b |
| Magic Well, Magic Spring, Oasis, Watering Hole, Rally Flag, Stables, Obelisk, Redwood Observatory, Seer Hut, Hill Fort, Water Wheel, Windmill, Wagon, School of War, Warrior's Tomb, Witch Hut, Spell Scroll, Sirens, Refugee Camp, Campfire, Fountain of Fortune, Idol of Fortune | §4.8a |
| what `*limit` is, where the "critical" flag comes from, the destination score, the friendly-hero suppression, `searchArray + 0x20` | §4.5a |
| `advManager::AI_prepare`, `playerData + 0x164`, `AI_pick_special_hero`, `begin_turn`, the two kingdom-goal passes, the faction-special building table, the third threat test, the trade commit, `get_total_value` | §4G |
| `count_alignments`, `normalise`, `plan.armyValueAfter`, artifact `0x81`, `town::get_buildable_mask` | §4B.4a / §4A.4a |
| the Wisdom / Artillery / First Aid arms, the magic-school counterfactual, the skill-ranking rule, what `useArmy` means | §4.12 |
| `hero + 0x47A`, `xp_reward_factor`, the creature speed/HP bonuses, hero specialties, `choose_melee`, `do_general_melee`, `do_aftermath`, the victory-condition term | §5D |

Several of those passes also **corrected** earlier text rather than only extending it — the list is
in §8.2.

**Everything on that list has since been opened.** §5B.4 (all sixteen quick-combat spell helpers,
both the valuation and the commit arms, and the category field named from `cast_spell`'s own
dispatch); §4.14 (the Grail-area reduction — template matching, not geometry); §4F.5 (the sea-travel
arm and `0x430F80`, which turned out to be *buying a boat*, not boarding one, and to spend
1 000 gold + 10 wood unconditionally).

The only residue left in the whole report is two tables in §4.12 (`0x681860` Pathfinding,
`0x681878` Navigation) that read as uniform `1.0` in the image and are almost certainly
runtime-filled — everything else that section indexes is static and resolved.

### Fifth pass — the residue of the original list

A line-by-line re-audit of the original 101-item list found seven entries that had been *cited* in
the report without ever being *specified*. All are now closed:

| item | where |
|---|---|
| `hero::get_morale` (`0x4E39B0`) and `hero::get_luck` (`0x4E36C0`) — called by a dozen valuations, never opened | §4.9 |
| `wall_speed_limit` / `wall_archery_penalty` — where they come from | §5B.2 |
| `0x4B8AF0` and `0x429AD0`, two of `begin_turn`'s four calls | §4G.3 |
| §5B.3's "which side owns the `get_attack` call" | §5B.3 |
| `computeHeroValuations` at level 0 — the supposed division by zero | §4.9 |
| §4B.1's hero-swap arm, elided with "…" | §4B.1 |
| the per-town AI build flags | below |

Three of those turned out to be **negatives**, and they are worth stating as findings:

* **There is no division by zero at level 0.** `experience_for_level` (`0x4DA420`) computes
  `n = level + 1` and indexes from there, so it returns the experience needed for the *next* level
  and is positive at level 0. No guard is needed.
* **There are no per-town AI build flags.** `type_AI_player::manage_kingdom` (`0x428DD0`) writes **no
  struct field at all** — it is pure orchestration. The build decision is recomputed from scratch on
  every call by `AI_build_one_building` (§4A.4), which picks the single best (town, building) pair
  across the whole kingdom. Nothing is cached on the town, so there is no flag to reproduce.
* **For the adventure AI the two wall parameters are always zero.** Every adventure-side caller of
  the `type_AI_combat_data` constructor passes no town (§5B.2).

Two questions in the original list turned out to have no answer to give, because the behaviour does
not exist:

* **AI answers to blocking dialogs and object-select dialogs.** There is no AI code for either. The
  engine auto-answers on the AI's behalf outside the ranges surveyed here; nothing in `0x4257C0…`
  or `0x525E80…` reads or writes a dialog result.
* **A loss-condition term in `AI_value_of_combat`.** There is none — see §5D.2. The term that looked
  like one is keyed on victory condition 5.

## 8.2 Corrections made by the second pass

| was | is | where |
|---|---|---|
| `hero + 0x482` = "value of +1 knowledge" | value of **+1 spell duration**; `+0x486` is knowledge | §2, §4.9b |
| `gpGame + 0x1F63E` = "scenario loss-condition / mode word" | the **day of the week** (1…7); `+0x1F640` is the week, `+0x1F642` the month | §2, §4.8a |
| Warrior's Tomb = "artifact minus morale penalty" | `playerData.artifactValue` only — there is no morale term | §4.8 table, §4.8a |
| §4.4: "if the cell is already explored **and** the player owns a town → skip" | the exploration reward fires when the cell is **un**explored and the player owns a town | §4.5a |
| §4B.7: "cost at `+0x18`, turn count at `+0x1A`" | `+0x1A` is the raw movement cost (the teleport `+50` lands there); `+0x18` is the turn-adjusted cost | §4.5a |
| §4.11 implies a loss-condition term | victory condition 5 ("defeat a specific hero"), halving the modifier | §5D.2 |
| the radius-widening loop in `AI_take_turn` runs up to 5 times | it runs **once** — `searchArray + 0x20` is cleared by `compute` and set nowhere | §4.5a |
| §4.9b: category `0x020000` is "resurrection-type" | spells that hit **both sides** (Armageddon family); the loop measures the caster's own **immune** fraction | §4.9b, §5B.4 |
| `0x44A4D0` glossed as a resistance | it returns **effectiveness** — `1.0` = fully affected, `0.0` = immune | §5B.4 |
| §5B.2's `md + 0x00` used only as a sort key | it is the original army **slot index**, and quick-combat AoE adjacency is `|slot − slot| == 1` | §5B.4 |
| §4.8a: `gpGame + 0x4E3E9` indexed by player, bit per obelisk | indexed **by obelisk**, bit per **player**; `+0x4E3E8` is the obelisk count | §4.8a, §4.14 |
| §4F.5: `0x430F80` is "boarding", pure mechanics | it is `AI_try_buy_boat` — it **builds a Shipyard and spends 1 000 gold + 10 wood**, outside the build planner entirely | §4F.5 |
| §4.14: the reduction is "map-geometry bookkeeping" | it is **template matching** of the revealed puzzle terrain against explored map tiles | §4.14 |
| `g_spellTraits` described only as "array pointer at `[0x687F58]`" | the array is **static at `0x685450`**; its flag word is compiled in while every numeric field is loaded from `SPTRAITS.TXT` | §4E.5 |
| `gpGame + 0x1F6D8` runs 0 = hardest … 4 = easiest | **0 = Easy … 4 = Impossible**, the menu order — settled by the AI movement grant at `0x4E4B58` | §6A.3, §4G.1, §4.14, §5D.2 |
| `0x429910` = "the magus-hut / scouting value" | it sums `scouting_value(radius 10)` over every **Eye of the Magi** (object 27) on the map; the **Hut** (object 37) is what consumes it | §4G.3 |
| §4.5: neighbour delta table at `0x678151…0x678171` | it is at **`0x678150`**, 8 records of 4 bytes, clockwise from North | §6A.5 |

### Why the earlier audit missed this

The coverage sweep in `METHODOLOGY.md` enumerates function *starts* in the AI ranges and diffs them
against the report text — so a function that the report **mentions once as a callee** counts as
covered. It is a test for "did we look at this?", not for "did we write down what it does?". Both
audits are needed; they fail in different directions.

# 9. NH3API symbol reconciliation

NH3API ships ~1 200 symbol names for Heroes III, but its addresses target **SoD 3.2** while this
binary is **Complete 4.0** (§0). This appendix reconciles the two: which names land on a real
function here, which do not, and what that implies for anyone using NH3API as a starting point.

## 9.1 Hit rate

| Class | Count | Verdict |
|---|---|---|
| Code symbols | 922 | 120 land on a function start with at least one caller or vftable slot — **13 %** |
| … of which already used in this report | 33 | independently corroborated |
| … valid but not yet used | 87 | listed in §9.3 |
| Code symbols that do **not** resolve | 802 | wrong for this build — do not trust |
| Data symbols | 262 | see §9.4 |

The test is mechanical: the address must be a primary function start in this image *and* have
at least one call site or code-pointer reference. It is necessary, not sufficient — see §9.5.

## 9.2 Names confirmed and already used here

These 33 appear in this report at the same address NH3API gives, and the semantics were derived
independently from the instruction stream before the names were consulted.

| Address | NH3API name |
|---|---|
| `0x004175E0` | `DemobilizeCurrHero` |
| `0x00423C80` | `type_monster_data::get_enchantment_value` |
| `0x00423D00` | `type_monster_data::get_resurrection_value` |
| `0x00423DE0` | `type_monster_data::get_spell_damage` |
| `0x00423EE0` | `type_AI_combat_data::type_AI_combat_data` |
| `0x00424D20` | `type_AI_combat_data::get_damage_spell_value` |
| `0x00425260` | `type_AI_combat_data::cast_damage_spell` |
| `0x004253E0` | `type_AI_combat_data::get_mass_damage_value` |
| `0x00425510` | `type_AI_combat_data::get_enchantment_value` |
| `0x004258A0` | `type_AI_combat_data::cast_enchantment` |
| `0x00425B10` | `type_AI_combat_data::cast_enchantment` |
| `0x00425BD0` | `type_AI_combat_data::cast_spell` |
| `0x00426170` | `type_AI_combat_data::inflict_melee_damage` |
| `0x00426300` | `type_AI_combat_data::inflict_damage` |
| `0x00426390` | `type_AI_combat_data::get_attack` |
| `0x00426450` | `type_AI_combat_data::get_final_melee_value` |
| `0x004264D0` | `type_AI_combat_data::do_general_melee` |
| `0x004267C0` | `type_AI_combat_data::choose_melee` |
| `0x00426BC0` | `type_AI_combat_data::simulate_combat` |
| `0x00426EE0` | `type_AI_combat_data::do_aftermath` |
| `0x004270C0` | `type_monster_data::AI_quick_combat` |
| `0x00427210` | `type_monster_data::AI_auto_combat` |
| `0x00427330` | `NewmapCell::AI_value_of_combat` |
| `0x00427750` | `type_AI_combat_data::get_total` |
| `0x00428710` | `type_AI_player::get_attack_bonus` |
| `0x00429AB0` | `type_AI_player::reset_magus_hut_value` |
| `0x0042A150` | `type_AI_player::get_total_value` |
| `0x004336C0` | `NewmapCell::AI_get_value_of_artifact` |
| `0x004339E0` | `NewmapCell::total_artifact_value` |
| `0x00433AA0` | `NewmapCell::AI_get_value_of_artifact` |
| `0x00442410` | `army::get_average_damage` |
| `0x0047B480` | `CSequence::AddFrame` |
| `0x004D76E0` | `type_obscuring_object::type_obscuring_object` |

## 9.3 Names valid here that this report has not used

Filtered to the AI-relevant subset. Several of these name routines this report left
deliberately unexpanded — in particular the quick-combat spell helpers of §5B.4.

| Address | Size | NH3API name | Relevance |
|---|---|---|---|
| `0x00423EA0` | 64 | `type_monster_data::take_damage` | quick-combat damage application (§5B) |
| `0x00424880` | 224 | `type_AI_combat_data::adjust_army` | quick-combat army adjust (§5B) |
| `0x00424960` | 112 | `type_AI_combat_data::get_fastest_speed` | quick-combat speed order (§5B.2) |
| `0x004249D0` | 544 | `type_AI_combat_data::get_next_chain_lightning_target` | **Chain Lightning targeting** — §5B.4 helper |
| `0x00424BF0` | 94 | `type_AI_combat_data::get_area_value` | **area-spell value** — §5B.4 helper |
| `0x00424FB0` | 336 | `type_AI_combat_data::cast_chain_lightning` | **cast Chain Lightning** — §5B.4 helper |
| `0x00425100` | 326 | `type_AI_combat_data::cast_area_effect` | **cast area effect** — §5B.4 helper |
| `0x004262B0` | 80 | `type_AI_combat_data::kill` | quick-combat stack removal (§5B.3) |
| `0x00427650` | 64 | `NewmapCell::AI_approximate_strength` | army strength approximation |
| `0x00427690` | 48 | `NewmapCell::AI_approximate_strength` | army strength approximation (overload) |
| `0x004276C0` | 144 | `type_AI_combat_data::type_AI_combat_data` | `type_AI_combat_data` ctor (§5B.1) |
| `0x00428410` | 352 | `NewmapCell::can_take_town` | can this hero take that town (§4B.2) |
| `0x0042B9E0` | 80 | `TRumour::is_human_ally` | ally test used by the turn driver (§4.2) |
| `0x0042BA30` | 48 | `TRumour::GetTown` | town accessor |
| `0x0042ECC0` | 112 | `searchArray::get_cell` | **`searchArray::get_cell`** — §4D.1 cell lookup |
| `0x0042ED30` | 80 | `searchArray::get_danger_value` | **`searchArray::get_danger_value`** — §4.6 danger map |
| `0x0042ED80` | 80 | `TRumour::get_cell` | map cell accessor |
| `0x004305A0` | 112 | `town::HasBuilding` | `town::HasBuilding` — §4A.4 / §4B.10 |
| `0x004317D0` | 48 | `TRumour::GetHero` | hero accessor (§4B.10) |
| `0x004B4420` | 96 | `army::ValidFlight` | flight validity — §4D.1 / §4F.3 |
| `0x004BAA40` | 256 | `playerData::IsLocalHuman` | human/computer test; cf. `is_computer` `0x4CE940` and `playerData + 0xE2` (§5A.9) |
| `0x004D9330` | 32 | `hero::get_number_in_backpack` | backpack count (§4B.10 step 1) |
| `0x004E3F40` | 304 | `hero::GetNecromancyFactor` | **`hero::GetNecromancyFactor`** |
| `0x004E4580` | 352 | `hero::GetDefenseFactor` | **`hero::GetDefenseFactor`** |
| `0x00523EC0` | 32 | `army::FindPath` | `army::FindPath` (battle) |
| `0x00523F60` | 96 | `army::ValidPath` | `army::ValidPath` (battle) |
| `0x005F2600` | 288 | `VictoryConditionStruct::IsTownCaptureTarget` | **`VictoryConditionStruct::IsTownCaptureTarget`** — §4C.3 condition 6 |
| `0x005F31E0` | 96 | `LossConditionStruct::CheckForDefeatedTownLoss` | **`LossConditionStruct::CheckForDefeatedTownLoss`** — loss conditions, §4C |

The remaining 59 valid-but-unused symbols are window, bitmap, input and LOD-file routines with no
bearing on the AI.

## 9.4 Data symbols travel better than code symbols

262 of NH3API's symbols point into `.rdata` / `.data`. Those cannot be validated the same way, but
there is a useful signal: of the globals this report derived **independently** from the instruction
stream, every one that NH3API also names matched at the same address.

| Address | Derived here as | NH3API name |
|---|---|---|
| `0x6703B8` | creature traits, stride 116 (§4E) | `g_akCreatureTypeTraits` |
| `0x6747B4` | dwelling → creature type table (§4B.4) | `g_gDwellingType` |
| `0x6783C8` | `MAP_WIDTH` (§4D.1) | `g_MAP_WIDTH` |
| `0x6783CC` | `MAP_HEIGHT` (§4D.1) | `g_MAP_HEIGHT` |

4 of 4. That is a small sample, but it fits the mechanism: data layout is far more stable across
builds than code addresses, because inserting a function shifts everything after it in `.text` while
leaving `.data` offsets alone. **Treat NH3API's data symbols as probably-right and its code symbols
as probably-wrong**, and verify either way.

## 9.5 Validity is not correctness

`0x47B480` passes the mechanical test — it is a real function with a real caller — and NH3API names
it `CSequence::AddFrame`. It is not: §4E establishes it as the **`crtraits.txt` parser**, called 150
times from `0x47B290` which opens that file. The address survived the shift; the name belongs to a
different function that used to live there.

So a symbol that resolves is a *hypothesis*, not a fact. Everything in §9.2 was derived from the
instruction stream first and matched to a name afterwards, which is the only ordering that is safe.
Every name in §9.3 is offered as a lead, not a finding.
