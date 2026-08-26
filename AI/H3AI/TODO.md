# H3AI — open TODOs

Every `TODO` marker in `AI/H3AI/`, with what is missing, why, and what it costs in
behaviour. `SS <n>` refers to a section of the reverse-engineering report
(`h3-ai-report.html`) that this AI is transcribed from.

**99 open items.** None of them is a bug in the transcription: each one marks a place
where the original's behaviour could not be reproduced, and the code says so rather
than inventing a number.

## Why a TODO exists

| Cause | Meaning |
| --- | --- |
| **R-gap** | The report *names* a routine, field or table but never gives its body or contents. Nothing to transcribe. |
| **R-amb** | The report gives prose or pseudo-code open to more than one reading. A reading was chosen and stated. |
| **VCMI** | VCMI's data model or callback layer does not expose what the original read directly out of game memory. |
| **Scope** | Battle AI (SS 5), deliberately out of scope for this adventure-map reimplementation. |

## Impact summary

| Priority | Count | What is affected |
| --- | --- | --- |
| **High** | 6 | Object valuation is systematically wrong: artifacts, spells and the `+1 stat` values are all zero, so whole classes of map object are invisible to the AI. |
| **Medium** | 24 | A documented behaviour is absent — adventure spells, the hero-arrival simulation, ally gifting, siege modelling, individual object rewards. |
| **Low** | 69 | Leaf arithmetic, a single object handler, or a chosen reading that is probably right. |

---

## The six that matter most

These are load-bearing: they feed dozens of other valuations, so fixing them changes
the AI's behaviour far more than anything else in this list.

| # | Where | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 1 | `H3ObjectValue.cpp:144` `artifactValue` | `AI_get_value_of_artifact` (`0x433AA0` / `0x4336C0`). SS 4.9 identifies the `type_artifact_effect` vftables at `0x63B6B0…0x63B778` but gives none of the arithmetic. | R-gap | **Every artifact is worth 0.** Artifact objects, Black Market, Wagon, Warrior's Tomb, War Machine Factory, Sea Chest and the whole tavern-hero pricing (SS 4B.9: "priced almost entirely by what its artifacts would be worth") collapse to the floor of 10 or to zero. |
| 2 | `H3Valuations.cpp:155` `computeHeroValuations` | `type_spellvalue::get_best_spell_value` — the counterfactual that prices `+1 spell power` / `+1 knowledge`. | R-gap | All three of `hero+0x47E/0x482/0x486` collapse to the documented floor of 10. Star Axis, Garden of Revelation, Library of Enlightenment, School of Magic, Tower/Inferno town specials and four SS 4.12 skill arms are all mispriced. |
| 3 | `H3ObjectValue.cpp:169` `spellValue` | The real spell valuer `0x527640` and its trio `0x526D40` / `0x5273D0` / `0x5275B0`. SS 4.17 gives the gates, not the value. | R-gap | Every spell is worth 0 once it passes the Wisdom gate. Shrines, Pandora spells and the Mage Guild half of `townVisitValue` (SS 4B.3) contribute nothing. |
| 4 | `H3Player.cpp:245` `computeWants` | `playerData + 0x164`, the average artifact value. Named in SS 2, its producer never given. | R-gap | Left at 0, so Pandora's Box, creature banks, Corpse, Sea Chest and Treasure Chest all lose their artifact term. |
| 5 | `H3ObjectValue.cpp:259` Pandora's Box | SS 4.8's most complete evaluator needs the box contents; VCMI keeps them as a `CRewardableObject` reward list not exposed to an AI. | VCMI | Only the guard-combat term survives. The AI cannot tell a rich box from an empty one. |
| 6 | `H3Kingdom.cpp:421` `heroArrivalValue` | SS 4B.10a performs the hire on live game state, measures the map, then rolls back. An AI cannot mutate VCMI's state. | VCMI | The candidate is measured with the army it already carries, not the army the town would hand it — SS 4B.10a says this is exactly what makes a rich AI pay more for a tavern hero. |

---

## H3ObjectValue.cpp — the SS 4.8 dispatch table

Object handlers whose reward the AI cannot see. In VCMI most of these are
`CRewardableObject` configurations or server-side rolls; the original read them straight
out of the map cell.

| Line | Object | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 234 | Artifact (5) | Which of the six pickup-condition arms applies (`objCell->d[0]` low nibble, jumptable `0x529670`). | VCMI | Free-pickup arm always used; the AI ignores the gold/resource price of guarded artifacts. |
| 276 | Black Market (7) | Needs item 1. | R-gap | Returns 0 — never a destination. |
| 301 | Campfire (12) | Rolled resource type and amount. | VCMI | Returns 0. |
| 328 | Creature Bank (16), Derelict Ship (24), Utopia (25), Crypt (84), Shipwreck (85), Pyramid (63) | The loot table. | VCMI | Only the combat term; banks are valued as pure fights. |
| 380 | Creature dwelling (17/20), victory condition 8 | SS 4.8 writes `+5 000 000 / obeliskCount` without saying which count. | R-amb | Undivided override applied. |
| 417 | Fountain of Fortune (30) | The luck bonus encoded in the object. | VCMI | Returns 0. |
| 471 | Hill Fort (35) | "value of upgrading the army minus the cost" — no formula given. | R-gap | Returns 0. |
| 484 | Idol of Fortune (38) | Which way the week test goes. | R-amb | Luck on days 1–6, morale added on day 7. |
| 532 | Magic Spring (48) / Magic Well (49) | `0x52B810` / `0x52A510`, mana refill value. | R-gap | Returns 0. |
| 605 | Monster (54) | The carried treasure term. | VCMI | Combat value only. |
| 629 | Oasis (56) | How `+400` movement points are priced. | R-gap | Morale term only. |
| 638 | Obelisk (57) | `0x52A2B0`; SS 4.14 leaves the Grail-area reduction unexpanded by choice. | R-gap | Returns 0 — the AI never chases obelisks, so its Grail estimate never narrows. |
| 646 | Redwood Observatory (58) / Pillar of Fire (60) | `0x432220`, scouting-radius value. | R-gap | Returns 0. |
| 675 | Rally Flag (64) | The movement-point term. | R-gap | Luck + morale only. |
| 681 | Refugee Camp (78) | `0x52A700`. | R-gap | Returns 0. |
| 713 | Seer Hut (83) | `0x5735A0`, quest reward minus quest cost. | R-gap | Returns 0. |
| 732 | Shrines (88–90) | Which spell the shrine holds. | VCMI | Returns 0. |
| 738 | Sirens (92) | `0x52A960`, XP for sacrificed troops. | R-gap | Returns 0. |
| 743 | Spell Scroll (93) | `0x52A8C0`. | R-gap | Returns 0. |
| 749 | Stables (94) | `0x52AAC0`; how `+400` mp becomes value. | R-gap | Returns 0. |
| 780 | Treasure Chest (101) | Which of the three tiers a chest rolls. | VCMI | First tier used, artifact term added. |
| 805 | Wagon (105) | The roll. | VCMI | Returns 0. |
| 811 | War Machine Factory (106) | Needs item 1. | R-gap | Returns 0. |
| 817 | School of War (107) | No valuation given for a primary-skill point. | R-gap | Returns 0. |
| 824 | Warrior's Tomb (108) | Needs item 1. | R-gap | Returns 0. |
| 830 | Water Wheel (109) | The base gold amount. | R-gap | Returns 0. |
| 839 | Watering Hole (110) | The movement-point half. | R-gap | Morale term only. |
| 845 | Windmill (112) | The roll. | VCMI | Returns 0. |
| 851 | Witch Hut (113) | Which skill it teaches. | VCMI | Returns 0. |
| 79 | `currentMorale` | `hero::get_morale` (`0x4E39B0`). | R-gap | VCMI's morale bonus total used instead. |
| 86 | `currentLuck` | The original's luck accessor. Note `CGHeroInstance::getCurrentLuck` is *declared* in VCMI but never defined. | R-gap | VCMI's luck bonus total used instead. |

## H3Movement.cpp — scan, influence map, movement

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 148 | `scanObjects` | Where SS 4.4's "critical" flag comes from. | R-gap | Negative-valued objects are always skipped. |
| 383 | `chooseDestination` | SS 4.5's final "best value / cost trade-off" expression. | R-amb | The influence-grid value at the entry's own cell is used as the score. |
| 569 | `moveToDestination` | All four adventure spells (SS 4F): Summon Boat, Fly, Water Walk, Dimension Door. They trigger on search-cell flags `0x400` / `0x800`, which this search does not model. | R-gap + VCMI | **The AI never casts an adventure spell.** No flying, no water walking, no Dimension Door, no summoned boats. |
| 626 | `heroTakeTurn` | What sets `searchArray + 0x20`, the flag that aborts the radius-widening loop. | R-gap | The 5-attempt doubling loop is never cut short. |
| 646 | `heroTakeTurn` | `gpGame + 0x1F63E`, the scenario mode word compared against 7. | VCMI | The "unexplored destination worth < 75" early-out is never taken. |
| 727 | `heroTurn` | Map-cell build flags `0x800` / `0x08` (SS 4.2 step 5). | R-gap | Not set; they only biased the engine's own search array. |

## H3Search.cpp — the pathfinder

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 35 | `index` | The two-plane cell index (SS 4D.1). Bit `0x400` separates the walking and airborne/water-walking states of the same tile. | VCMI | Only the ground plane exists. Directly causes the missing Fly / Water Walk above. |
| 218 | `compute` | The engine's per-turn movement accounting for `cell + 0x1A`. | R-gap | Turn index derived from accumulated cost. |
| 304 | `buildReachability` | How SS 4B.7's handicap is applied to the suppression test. | R-amb | Cells another friendly hero covers are suppressed outright. |

## H3CombatEstimate.cpp — SS 4.11 and the SS 5B quick-combat simulator

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 370 | `valueOfCombat` | `hero + 0x47A`, the hero's combat-strength modifier. | R-gap | 1.0 used. |
| 415 | `valueOfCombat` | `0x4E4840`, the float in the XP-reward term. | R-gap | 1.0 used. |
| 452 | `valueOfCombat` | SS 4.11 adds a loss-condition term; SS 4C.4 states the AI never reads the loss condition. | R-amb (report contradicts itself) | Loss-condition term not reproduced; victory-condition term is. |
| 77 | ctor | `0x4E5AA0`, a per-hero creature-speed bonus. | R-gap | No bonus applied. |
| 83 | ctor | `wall_speed_limit` and `wall_archery_penalty`. | R-gap | **Sieges simulate as open-field battles.** |
| 110 | ctor | `0x4E5B80`, per-hero creature HP bonus. | R-gap | No bonus applied. |
| 301 | `simulateCombat` | `choose_melee` (`0x4267C0`) and `do_general_melee` (`0x4264D0`). | R-gap | Both sides treated as melee every round. |
| 310 | `simulateCombat` | `cast_spell` (`0x425BD0`) and its seven helpers — SS 5B.4 leaves them unexpanded by choice. | Scope | No spell is cast in simulation; it is a pure troop exchange. |
| 321 | `simulateCombat` | SS 5B.3's "the ranged side takes … from the other" is ambiguous about which side owns the `get_attack` call. | R-amb | Read as "each side *deals* the named attack". |
| 352 | `simulateCombat` | `do_aftermath` (`0x426EE0`) — necromancy, town-tower fire. | R-gap | Not applied. |

## H3Player.cpp / H3Player.h — SS 4A, the economy

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| `H3Player.h:74` | `artifactValue` | See item 4 above. | R-gap | High. |
| 105 | `beginTurn` | `0x4B8AF0`, `0x429910`, `0x4280E0`, `0x429AD0`. | R-gap | Only the two specified steps run. |
| 113 | `beginTurn` | The Grail dig-site cache (SS 4.14). | R-gap | Never computed. |
| 141 | `computeWants` | Whether `town::get_buildable_mask` includes unaffordable buildings. | R-amb | Unaffordable included — they are what creates demand. |
| 303 | `computeResourceSupplyAndThreats` | Whether the traits level field is 0- or 1-based. | R-amb | Literal comparison against 6, read against VCMI's 1-based level. Off by one tier if the other reading is right. |
| 311 | `computeResourceSupplyAndThreats` | `ourTotal` / `enemyBest` in SS 4A.5's third threat test. | R-gap | Third test not reproduced. |
| 373 | `planTrades` | `AI_plan_trades` / `AI_do_trades` also *commit* a trade; market and rate selection unspecified. | R-gap | Only answers "could the deficit be covered"; **never actually trades**. |
| 407 | `getTotalValue` | What the original returns when cost is 0 (it would divide by zero). | R-gap | Guarded. |
| 418 | `reserveFunds` | `0x42A470`'s body. | R-gap | `cost * multiplier` accumulated — the obvious reading. |

## H3Kingdom.cpp — SS 4.10 / 4A.4 / 4B.6 / 4B.9 / 4B.10 / 4.13

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 141 | `evaluateBuilding` | How much extra weekly growth a Citadel/Castle gives. | R-gap | VCMI's growth model queried. |
| 182 | `evaluateBuilding` | What "under threat" means for the Resource Silo arm. | R-gap | The `-1` branch never fires. |
| 256 | `evaluateBuilding` | The per-faction special-building table at `0x42B4FC`. | R-gap | All faction specials score 0, so they are built only as prerequisites. |
| 400 | `offerResourcesToAlly` | A callback to hand resources to an ally. | VCMI | **The gift is computed exactly per SS 4B.6 but never committed.** |
| 421 | `heroArrivalValue` | See item 6 above. | VCMI | Medium-high. |
| 528 | `buyHero` | `hero::total_artifact_value` (`0x4339E0`). | R-gap | Only the documented floor of 10 per backpack artifact. |
| 697 | `hireHero` | The "plus `get_primary_skill_sum` weighting" term. | R-gap | Primary-skill term omitted. |
| 731 | `visitOwnTown` | Which artifact `0x81` is (the planner's `mode` flag). | R-gap | Treated as absent. |
| 764 | `visitOwnTown` | Mapping the planner's final slot layout onto VCMI's swap/merge/split callbacks. | VCMI | Only whole stacks move; SS 4B.4's "leave one behind" branch moves the whole stack. |
| 863 | `manageKingdom` | `0x4280E0`, the two kingdom-goal evaluators. | R-gap | Not run. |
| 884 | `manageKingdom` | The per-town AI build flags. | R-gap | Not set. |

## H3ArmyPlanner.cpp — SS 4B.4

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 35 | `AI_COMBAT_SKILL_LEVEL` | `gpGame->d[0x1F698]` has no VCMI counterpart. | VCMI | 0 used, selecting the branch that treats the four base elementals as alignment −1. |
| 91 | `countAlignments` | `armyGroup::count_alignments` (`0x44AE60`) body. | R-gap | Implemented as "distinct alignments held, ignoring alignment-free creatures". |
| 139 | `heroMoraleOf` | `hero::get_morale`. | R-gap | VCMI's morale total. |
| 311 | `writeback` | The sort comparator's direction. | R-amb | Ascending by speed — the reading that makes the two opposite-direction passes place shooters and walkers at opposite ends. |
| 407 | `normalise` | The exact predicate applied to the `value_of_adding` result. | R-amb | `<= 0` means "do not keep". |

## H3TownValue.cpp — SS 4B.1 / 4B.2 / 4B.3 / 4B.5

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 61 | `townVisitValue` | `0x525BF0`, the Conflux arm. | R-gap | Conflux special not valued. |
| 128 | `townRecruitValue` | Which artifact `0x81` is. | R-gap | Treated as absent. |
| 136 | `townRecruitValue` | `plan.armyValueAfter` — named in SS 4B.5 but absent from SS 4B.4's field table. | R-gap | Army value after the simulated exchange used. |
| 187 | `townCaptureValue` | SS 4B.2 adds `gpGame->w[0x1F63E]` (a scenario mode word) to a week count; no interpretation offered. | R-amb | Term dropped. |
| 289 | `townValue` | The contents of the 5-entry artifact table at `0x640558…0x64056C`. | R-gap | Artifact hand-off to a garrisoned hero not reproduced. |
| 303 | `townValue` | SS 4B.1 elides the rest of the hero-swap arm with "...". | R-gap | Only the documented 80 %-strength early-out is applied. |

## H3SecondarySkills.cpp — SS 4.12

| Line | Skill | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 94 | Pathfinding | Table `0x681860`; SS 4.12 says it reads as uniform 1.0 and is probably runtime-filled. | R-gap | 1.0 used — what the image holds. |
| 128 | Wisdom | SS 4.12 gives the two operands, not the expression combining them. | R-amb | Product used. |
| 162 | Fire / Air / Water / Earth Magic | `0x524D20`'s spellbook counterfactual (needs the same trio as item 3). | R-gap | All four magic schools score 0 — **the AI never picks a magic school on level-up**. |
| 178 | Artillery | What the arm returns once the Ballista gate passes. | R-gap | Returns 0. |
| 201 | First Aid | Same, for the First Aid Tent gate. | R-gap | Returns 0. |
| 240 | `rankAmongLearnableSkills` | SS 4.12 shows `val[]` being filled but elides the ranking rule. | R-amb | "at least as good as anything else the class could learn". |

## H3AI.cpp — the turn driver

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 137 | `takeTurn` | `advManager::AI_prepare` (`0x527960`). | R-gap | Not run. |
| 153 | `takeTurn` | `AI_pick_special_hero` (`0x526A90`) — what makes a hero "special". | R-gap | **SS 4.1's entire PASS 1 is skipped.** All heroes go through the ordinary PASS 2 loop. |
| 288 | `heroGotLevel` | What the caller passes for SS 4.12's `useArmy`. | R-amb | `true` — the reading under which the whole skill table is army-scaled. |
| 302 | `commanderGotLevel` | Commanders do not exist in the original. | Scope | First option taken. |
| 308 | `showBlockingDialog` | The report does not cover AI dialog answers. | R-gap | Accept, so a march can continue. |
| 329 | `showMapObjectSelectDialog` | Not covered. | R-gap | First option taken. |

## H3Valuations.cpp — SS 4.9

| Line | Function | Missing | Cause | Impact |
| --- | --- | --- | --- | --- |
| 145 | `computeHeroValuations` | What the original does at level 0, where the divisor is 0. | R-gap | Guarded, returns 0. |
| 155 | `computeHeroValuations` | See item 2 above. | R-gap | High. |

---

## Notes on fidelity

Three things are worth stating plainly, because they are *not* TODOs — they are
faithful:

- The pathfinder is a LIFO label-correcting flood, not a priority queue. SS 4D.1 warns
  that swapping in a heap changes tie-breaks, and the destination chooser reads that
  order.
- Conditions 1 and 2 (accumulate creatures / resources) are deliberately ignored:
  SS 4C.2 proves the original AI is blind to them.
- Victory condition 10 is handled as condition 0, because SS 4C.3 shows one test covers
  both.

Battles are ceded to the configured battle AI. The report's SS 5 chapters are out of
scope; only SS 5B's quick-combat simulator is reimplemented, because SS 4.11's
`AI_value_of_combat` depends on it.
