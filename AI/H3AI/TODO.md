# H3AI — what is not transcribed

`SS <n>` refers to a section of the reverse-engineering report
(`h3-ai-report.html`) that this AI is transcribed from.

Every remaining deviation is annotated **in the code, at the line it applies to**, with
one of five markers, so this file cannot drift out of sync with the source:

| Marker | Meaning |
| --- | --- |
| `GAP:` | The report *names* a routine, field or table but never gives its body or contents. Nothing to transcribe. |
| `AMBIGUITY:` | The report gives prose open to more than one reading. A reading was chosen and stated. |
| `NOT IN VCMI:` | VCMI's data model has no counterpart of what the original read out of game memory. |
| `NOT IN REPORT:` | The original's behaviour here is undocumented. A decision was made and stated. |
| `OUT OF SCOPE:` | The feature does not exist in the original game at all. |

```
grep -rn "GAP:\|AMBIGUITY:\|NOT IN VCMI:\|NOT IN REPORT\|OUT OF SCOPE" AI/H3AI/
```

There are **ten** such sites. None of them silently invents a number.

---

## The ten

| Where | Marker | What | Effect |
| --- | --- | --- | --- |
| `H3CombatEstimate.cpp` `simulateCombat` | GAP | `choose_melee` (`0x4267C0`) and `do_general_melee` (`0x4264D0`) are named but not given. | Both sides are treated as melee every round, which is what the original converges to once shooters are engaged. |
| `H3CombatEstimate.cpp` `simulateCombat` | GAP | `cast_spell` (`0x425BD0`) and its seven helpers — SS 5B.4 leaves them unexpanded on purpose. | No spell is cast in the quick-combat simulation; it is a pure troop exchange. Battle AI is out of scope (SS 5). |
| `H3TownValue.cpp` `townValue` | GAP | SS 4B.1 elides the rest of the hero-swap arm with "…". | Only the documented 80 %-strength early-out is applied. |
| `H3ObjectValue.cpp` creature dwellings | AMBIGUITY | SS 4.8 writes `+5 000 000 / obeliskCount` for victory condition 8 without saying which count. | The undivided override is used (the mine arm, condition 9, divides by the number of mines). |
| `H3ArmyPlanner.cpp` `AI_COMBAT_SKILL_LEVEL` | NOT IN VCMI | `gpGame->d[0x1F698]` has no VCMI setting. | 0 is used, selecting the branch that treats the four base elementals as alignment −1. |
| `H3AI.cpp` `recruitFromDwelling` | NOT IN REPORT | SS 4.8 prices a dwelling by "the creatures buyable now" and says nothing about the purchase. | Strongest tier first, as many as the purse and free slots allow — otherwise every dwelling visit would be a scored no-op. |
| `H3AI.cpp` `commanderGotLevel` | OUT OF SCOPE | Commanders do not exist in the original. | The first offer is taken. |
| `H3AI.cpp` `showBlockingDialog` | NOT IN REPORT | Dialog answers are not covered. | Accept — the object was already priced as worth visiting before the hero stepped on it. |
| `H3AI.cpp` `showMapObjectSelectDialog` | NOT IN REPORT | Not covered. | First option. In practice unreachable: the dialog is raised by Town Portal and the View spells, and SS 4F's four spells are the only ones this AI casts. |
| `H3Kingdom.h` `buyUniversitySkill` | NOT IN REPORT | The original has no University handler at all. | The best skill the SS 4.12 valuer ranks, bought when it is worth more than its gold. |

---

## Deliberate, and not deviations

Three things are faithful rather than simplified, and are worth stating so they are not
"fixed" by mistake:

- The pathfinder is a LIFO label-correcting flood, not a priority queue. SS 4D.1 warns
  that swapping in a heap changes tie-breaks, and the destination chooser reads that
  order.
- Victory conditions 1 and 2 (accumulate creatures / resources) are ignored: SS 4C.2
  proves the original AI is blind to them. Condition 10 is handled as condition 0,
  because SS 4C.3 shows one test covers both.
- A siege simulates as an open-field battle *when no town is passed*, which is what every
  adventure-side caller of the original's constructor does. The wall parameters are
  transcribed and do apply when a town is supplied.

Three hero-*specialty* refinements are omitted rather than guessed — the +2 creature
speed specialist (SS 5D.3), the Learning specialist's `xp_reward_factor` term (SS 5D.3)
and the First Aid specialist's scaling (SS 4.9a). VCMI expresses specialties as bonus
lists rather than as the original's class-id test, and each applies to a handful of
heroes; the omission is noted at all three sites.

Battles are ceded to the configured battle AI. Only SS 5B's quick-combat simulator is
reimplemented, because SS 4.11's `AI_value_of_combat` depends on it.

---

## How the map is read

The original AI reads object contents straight out of the map record: SS 4.8's Pandora
arm, the creature-bank loot table, the shrine's spell and the witch hut's skill are all
direct memory reads. VCMI keeps the same information on the object as a resolved
`Rewardable::Configuration` — the random roll is baked in at map init — so
`H3RewardValue` reads it there and prices it with SS 4.8's Pandora arithmetic, which is
the shape every loot-carrying handler in the dispatch table reuses.

Reading through the fog is separate and is a *cheat*, gated in
`H3AdventureAI::initGameInterface`: on, matching the original, unless a human shares this
AI's team.
