/*
 * PhilConstants.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/*
 * Every hard-coded numeric constant recovered from the original Heroes III AI
 * (ai.cpp, ai_combat.cpp, ai_tactical.cpp, ai_player.cpp, philai.cpp).
 *
 * The original scatters these inline across 30+ call sites; they are collected here
 * so a single place documents the source citation for each one.
 *
 * Citation format below is `function file.cpp:line` from the recovered 1999 symbol table.
 *
 * PHILAI-GAP markers throughout this AI mean: the original behaviour is known but the
 * exact number lived in the binary's compiled data section (not inline code) and was
 * not recoverable, or VCMI has no equivalent of the game state the original read.
 */

namespace PhilAI
{
namespace Const
{

// ---------------------------------------------------------------------------
// A.1 - Turn orchestration & the ledger
// ---------------------------------------------------------------------------

/// philAI::DoAI philai.cpp:1261 - move_all_heroes is called exactly twice, hard-coded.
inline constexpr int MOVEMENT_PASSES_PER_TURN = 2;

/// calculate_demand ai_player.cpp:258 - difficulty above this doubles projected creature growth.
inline constexpr int DIFFICULTY_GROWTH_DOUBLING_THRESHOLD = 4;

/// philAI::GetTurnAIVars philai.cpp:1770 - normalizes summed artifact values into one average.
inline constexpr int AVERAGE_ARTIFACT_DIVISOR = 120;

/// philAI::GetTurnAIVars philai.cpp:1770 - floor on "value of +1 Power" / "+1 Knowledge".
inline constexpr int MIN_PRIMARY_POINT_VALUE = 10;

/// calculate_demand ai_player.cpp:258 - a second producing source of a resource counts double.
inline constexpr int SECOND_RESOURCE_SOURCE_WEIGHT = 2;

/// calculate_demand ai_player.cpp:258 - top N growing creature types folded into demand.
inline constexpr int DEMAND_CREATURE_TYPES = 3;

/// calculate_reserve ai_player.cpp:752 - top N growing creature types reserved for, per town.
inline constexpr int RESERVE_CREATURE_TYPES = 2;

/// calculate_demand ai_player.cpp:258 - marketplace count clamp indexing the trade-efficiency table.
inline constexpr int MARKET_COUNT_MIN = 1;
inline constexpr int MARKET_COUNT_MAX = 10;

/// calculate_demand ai_player.cpp:258 - a 6-resource running total is divided by 5, not 6.
inline constexpr int AVERAGE_RESOURCE_DIVISOR = 5;

/// calculate_demand ai_player.cpp:258 - fallback when a resource has zero recorded demand.
inline constexpr int DEFAULT_DEMAND = 4;

/// fill_prohibited_array ai_player.cpp:926 - baseline affordability is a week's production.
inline constexpr int AFFORDABILITY_WEEKS = 7;

// ---------------------------------------------------------------------------
// A.2 - Building & town valuation
// ---------------------------------------------------------------------------

/// value_of_hall ai_player.cpp:1109 - identical for Village Hall and Town Hall.
inline constexpr int HALL_VALUE_VILLAGE = 3500;
inline constexpr int HALL_VALUE_TOWN = 3500;
inline constexpr int HALL_VALUE_CITY = 7000;
inline constexpr int HALL_VALUE_CAPITOL = 14000;

/// value_of_silo ai_player.cpp:1045 - Resource Silo value is its priced income times seven.
inline constexpr int SILO_INCOME_MULTIPLIER = 7;

/// type_AI_player::get_total_value ai_player.cpp:1337 - ROI is value * 1000 / priced cost.
inline constexpr int ROI_NUMERATOR = 1000;

/// type_AI_player::get_total_value ai_player.cpp:1337 - unaffordable and untradeable is blocked.
inline constexpr int VALUE_BLOCKED = -1;

/// value_of_building ai_player.cpp:1147 - faction special-building valuations.
inline constexpr int SPECIAL_TOWER_FLAT = 100;
inline constexpr int SPECIAL_NECRO_PER_NECROMANCER_HERO = 1000;
inline constexpr int SPECIAL_NECRO_ARTIFACT_MERCHANT = 10;
inline constexpr int SPECIAL_STRONGHOLD_ARTIFACT_MERCHANT = 5000;
inline constexpr int SPECIAL_FORTRESS_GARRISON_DIVISOR = 20;
inline constexpr int SPECIAL_RAMPART_GATE_GOLD_DIVISOR = 10;
inline constexpr int SPECIAL_RAMPART_ARTIFACT_MERCHANT_AVG_MULT = 2;

/// value_of_castle_upgrade ai_player.cpp:1006, value_of_hall ai_player.cpp:1109
inline constexpr int WIN_CONDITION_OVERRIDE = 5000000;

// ---------------------------------------------------------------------------
// A.3 - Creature purchasing & army
// ---------------------------------------------------------------------------

/// type_AI_creature_purchaser::do_best_purchase ai_player.cpp:2535
/// The cost discount is capped at three quarters of the value gained (value*3 >> 2).
inline constexpr int PURCHASE_DISCOUNT_NUM = 3;
inline constexpr int PURCHASE_DISCOUNT_DEN = 4;

/// type_AI_creature_swapper::value_of_adding_army ai_player.cpp:2350 - morale gates on a merge.
inline constexpr int MERGE_MORALE_THRESHOLD_DEFAULT = 2;
inline constexpr int MERGE_MORALE_THRESHOLD_NEUTRAL = 1;

// ---------------------------------------------------------------------------
// A.4 - Map scoring, danger & movement
// ---------------------------------------------------------------------------

/// mark_strategic_map ai_player.cpp:3390 - outer spread rectangle and inner core rectangle.
inline constexpr int SPREAD_OUTER_RADIUS = 5;
inline constexpr int SPREAD_CORE_RADIUS = 1;

/// mark_strategic_map ai_player.cpp:3390 - value*300 / (cost + 300 - core).
inline constexpr int SPREAD_SOFTENING = 300;

/// net_value_of_location ai_player.cpp:3498 - catastrophic clamp.
inline constexpr int DANGER_CATASTROPHIC_THRESHOLD = -499999999;
inline constexpr int BARRIER_CATASTROPHIC_THRESHOLD = 4999999;
inline constexpr int CATASTROPHIC_CLAMPED_VALUE = -2500000;

/// net_value_of_location ai_player.cpp:3498 - stickiness toward an already-committed target.
inline constexpr double STICKINESS_MULTIPLIER = 1.5;
inline constexpr int STICKINESS_BONUS = 20;

/// net_value_of_location ai_player.cpp:3498 - anti-clustering de-weighting of every other candidate.
inline constexpr int CANDIDATE_RANDOM_MIN_PCT = 76;
inline constexpr int CANDIDATE_RANDOM_MAX_PCT = 100;

/// net_value_of_location ai_player.cpp:3498 - a still-positive score is floored at 1.
inline constexpr int POSITIVE_VALUE_FLOOR = 1;

/// AI_choose_destination ai_player.cpp:3645
inline constexpr int NEARBY_MOBILITY_PERCENT = 21;
inline constexpr int VALUE_PER_MOVEMENT_UNIT = 100;
inline constexpr int UNSCOUTED_DIVISOR = 10;

/// find_all_destinations ai_player.cpp:3225 - baseline value of an unowned, event-less tile.
inline constexpr int EMPTY_TILE_VALUE = 100;
inline constexpr int EMPTY_TILE_VALUE_EXPLORING = 100000;
inline constexpr int FORCED_PATH_MOVE_COST = 10000;

/// check_holy_grail ai_player.cpp:3164
inline constexpr int GRAIL_WIN_CONDITION_VALUE = 5000000;

/// AI_AttemptMove ai_player.cpp:4179
inline constexpr int SUBTERRANEAN_GATE_COST = 100;
inline constexpr int MIN_REPATH_RADIUS = 350;
inline constexpr int REPATH_MOVEMENT_GATE = 99;
inline constexpr int TOWN_PORTAL_COST_EXPERT = 200;
inline constexpr int TOWN_PORTAL_COST_NORMAL = 300;

/// attempt_teleport ai_player.cpp:4000
inline constexpr int DIMENSION_DOOR_MANA_BUFFER = 20;
inline constexpr int DIMENSION_DOOR_MAX_CASTS = 6;
inline constexpr int DIMENSION_DOOR_HOP_COST = 200;

/// move_hero philai.cpp:934 - the destination-search radius escalation.
inline constexpr int SEARCH_RADIUS_START = 1000;
inline constexpr int SEARCH_RADIUS_LAST_HERO = 32000;
inline constexpr int SEARCH_RADIUS_COMMITTED_PAD = 200;
inline constexpr int SEARCH_RADIUS_RETRIES = 5;
inline constexpr int PATROL_RADIUS_MULTIPLIER = 200;

// ---------------------------------------------------------------------------
// A.5 - Ships & hero hiring
// ---------------------------------------------------------------------------

/// AI_build_ship ai_player.cpp:4607 - flat cost, gated on >9 wood and >999 gold.
inline constexpr int SHIP_COST_WOOD = 10;
inline constexpr int SHIP_COST_GOLD = 1000;

/// AI_get_ship_cost ai_player.cpp:4643 - sentinel when no shipyard is reachable at all.
inline constexpr int NO_SHIPYARD_SENTINEL = -200000;

/// type_AI_player::hire_heroes ai_player.cpp:4670 - hard cap before hiring is even considered.
inline constexpr int MAX_HEROES = 8;

/// hero_limits[difficulty] / global_limits[difficulty] ai_player.cpp:4670
/// PHILAI-GAP: the two 5-entry tables live in the binary's data section, not inline code.
/// These are plausible reconstructions consistent with observed play, not recovered values.
inline constexpr int HERO_LIMIT_BY_DIFFICULTY[5] = { 3, 4, 5, 6, 8 };
inline constexpr int GLOBAL_HERO_LIMIT_BY_DIFFICULTY[5] = { 12, 16, 20, 24, 32 };

// ---------------------------------------------------------------------------
// A.6 - Map-object valuators
// ---------------------------------------------------------------------------

inline constexpr int LIBRARY_GATE = 10;              ///< ValueOfLibrary philai.cpp:2550, Level+2*Wisdom
inline constexpr int LIBRARY_DIVISOR = 10;
inline constexpr int LIBRARY_PER_KNOWLEDGE = 2;
inline constexpr int LIBRARY_PER_POWER = 2;

inline constexpr double SEA_CHEST_HIGH = 1200.0;     ///< ValueOfSeaChest philai.cpp:2931
inline constexpr double SEA_CHEST_LOW = 200.0;

inline constexpr int PRISON_HERO_GATE = 8;           ///< ValueOfPrison philai.cpp:2794
inline constexpr int PRISON_GUARD_MULTIPLIER = 2500;

inline constexpr int IDOL_LUCK_DIVISOR_A = 2;        ///< value_of_idol philai.cpp:2250
inline constexpr int IDOL_LUCK_DIVISOR_B = 5;

inline constexpr double FLOTSAM_A = 175.0;           ///< ValueOfFlotsam philai.cpp:2274
inline constexpr double FLOTSAM_B = 5.0;

inline constexpr double MINE_MULTIPLIER = 2.0;       ///< ValueOfMine philai.cpp:2626

inline constexpr double SKELETON_A = 5.0;            ///< ValueOfSkeleton philai.cpp:2948
inline constexpr double SKELETON_B = 200.0;

inline constexpr double TREASURE_LOW = 160.0;        ///< ValueOfTreasure philai.cpp:3307
inline constexpr double TREASURE_HIGH = 320.0;

inline constexpr int GOLD_FOR_SKILL_GATE = 1000;     ///< value_of_war_school philai.cpp:3402
inline constexpr int LIGHTHOUSE_SHIPYARD_VALUE = 1000;
inline constexpr int BACKPACK_SLOTS = 64;            ///< buy_artifacts philai.cpp:326
inline constexpr int SIEGE_ENGINE_REACH_GATE = 501;  ///< buy_siege_engine philai.cpp:594
inline constexpr int UNIVERSITY_SKILL_PRICE = 2000;  ///< AI_visit_university philai.cpp:3744
inline constexpr int SPELLBOOK_PRICE = 500;          ///< AI_enter_town philai.cpp:732
inline constexpr int SIRENS_KEEP_PERCENT = 70;       ///< AI_VisitSirens philai.cpp:3007

inline constexpr int OBSERVATORY_RADIUS_MAP = 20;    ///< AI_value_of_observatory ai_player.cpp:4728
inline constexpr int OBSERVATORY_RADIUS_TURN_START = 10;

inline constexpr int BORDER_TENT_VALUE = 5000;       ///< inline in AI_value_of_event dispatcher

/// value_of_reinforcing philai.cpp:3096
inline constexpr int REINFORCE_TRIP_THRESHOLD = 399;
inline constexpr int REINFORCE_GAIN_DIVISOR = 3;

/// ComputeUpgradeValue philai.cpp:1833 - upgrading into an existing stack is worth ~20% more.
inline constexpr double UPGRADE_EXISTING_STACK_BONUS = 1.2;

/// value_of_experience philai.cpp:1717 - (army value + hire cost) / (experience * 40).
inline constexpr int EXPERIENCE_VALUE_DIVISOR = 40;

/// get_artifact_purchase_price philai.cpp:263 - the three real Artifact Merchant tiers.
inline constexpr int ARTIFACT_PRICE_TIER1_GOLD = 2000;
inline constexpr int ARTIFACT_PRICE_TIER2_GOLD = 2500;
inline constexpr int ARTIFACT_PRICE_TIER2_RES = 3;
inline constexpr int ARTIFACT_PRICE_TIER3_GOLD = 3000;
inline constexpr int ARTIFACT_PRICE_TIER3_RES = 5;

// ---------------------------------------------------------------------------
// A.7 - Artifacts
// ---------------------------------------------------------------------------

inline constexpr int ART_COMBAT_DIVISOR = 100;       ///< type_combat_artifact ai_player.cpp:5081
inline constexpr int ART_MIGHT_DIVISOR = 40;         ///< type_might_artifact ai_player.cpp:5098
inline constexpr int ART_MOVEMENT_DIVISOR = 100;     ///< type_movement_artifact ai_player.cpp:5189
inline constexpr int ART_MOVEMENT_ARMY_FLOOR = 2500;
inline constexpr int ART_ANTIMAGIC_FULL_DIVISOR = 5; ///< type_antimagic_artifact ai_player.cpp:5355
inline constexpr int ART_ANTIMAGIC_FULL_POWER_PENALTY = 50;
inline constexpr int ART_ANTIMAGIC_PART_DIVISOR = 8;
inline constexpr int ART_ANTIMAGIC_PART_POWER_PENALTY = 25;
inline constexpr int ART_INCOME_DAYS = 3;            ///< type_income_artifact ai_player.cpp:5486
inline constexpr int ART_CREATURE_GROWTH_DIVISOR = 40;
inline constexpr int ART_NECROMANCY_DIVISOR = 250;   ///< type_necromancy_artifact ai_player.cpp:5152
inline constexpr int ART_SPELLCASTER_DIVISOR = 100;  ///< type_spellcaster_artifact ai_player.cpp:5205
inline constexpr int ART_BALLISTICS_SCALE = 500;     ///< AI_get_value_of_artifact param_2==4
inline constexpr int ART_HEALING_SCALE = 1000;       ///< AI_get_value_of_artifact param_2==6
inline constexpr int ART_VALUE_FLOOR = 10;           ///< AI_get_value_of_artifact ai_player.cpp:5557
inline constexpr int EQUIP_SLOTS = 18;               ///< AI_get_equip_value ai_player.cpp:5643

// ---------------------------------------------------------------------------
// A.8 - Combat strength, retreat & turn order
// ---------------------------------------------------------------------------

/// combatManager::AICheckRetreat ai.cpp:162
inline constexpr int RETREAT_BLOCKING_ARTIFACT = 125;    ///< 0x7d, blocks retreat unconditionally
inline constexpr int RETREAT_SCRIPTED_MODE = 5;
inline constexpr int RETREAT_SKIP_ROLL_MAX = 100;
inline constexpr int RETREAT_SKIP_ROLL_UNDER = 51;       ///< Random(1,100) < 51 on Normal
inline constexpr int RETREAT_LOOT_GATE = 1000;
inline constexpr int RETREAT_GOLD_GATE = 2000;
inline constexpr int RETREAT_TREASURY_MINIMUM = 2500;    ///< surrender cost + 2500 must be affordable
inline constexpr double RETREAT_SIEGE_DEFENDER_BONUS = 1.1;
inline constexpr double RETREAT_THRESHOLD_BASE_A = 0.16;
inline constexpr double RETREAT_THRESHOLD_BASE_B = 0.22;
inline constexpr int RETREAT_LOOT_TIER_1 = 1;
inline constexpr int RETREAT_LOOT_TIER_2 = 5001;
inline constexpr int RETREAT_LOOT_TIER_3 = 10001;
/// PHILAI-GAP: the three loot-tier threshold replacements live in the binary's data section
/// (DAT_00034fc0 / DAT_00034dac / DAT_00034da8) and were not recoverable. Structure is exact.
inline constexpr double RETREAT_GOLD_RESERVE_UNIT = 200000.0;
inline constexpr double RETREAT_GOLD_RESERVE_RATE_A = 0.015;
inline constexpr double RETREAT_GOLD_RESERVE_RATE_B = 0.03;
/// Recovered directly from the decompile: threshold -= (4 - difficulty) * 0.015.
inline constexpr double RETREAT_DIFFICULTY_RATE = 0.015;
inline constexpr int RETREAT_DIFFICULTY_BASE = 4;

/// get_move_order ai.cpp:610 - turn-order priority sentinels.
inline constexpr int MOVE_ORDER_WAR_MACHINE = -100000;
inline constexpr int MOVE_ORDER_BLINDED = -10000;
inline constexpr int MOVE_ORDER_INCAPACITATED_OFFSET = -1000;
inline constexpr int MOVE_ORDER_BLIND_ROUNDS_GATE = 2;

/// combatManager::choose_melee_target ai.cpp:1896 - independent melee randomizer.
inline constexpr int MELEE_RANDOM_MIN_PCT = 75;
inline constexpr int MELEE_RANDOM_MAX_PCT = 100;

/// AI_quick_combat ai_combat.cpp:1511 / AI_auto_combat ai_combat.cpp:1539
inline constexpr int AUTORESOLVE_RANDOM_MIN_PCT = 75;
inline constexpr int AUTORESOLVE_RANDOM_MAX_PCT = 125;

/// hero::get_combat_value_modifier hero.cpp:6171 - 5% per point, combined geometrically.
inline constexpr double COMBAT_MODIFIER_PER_POINT = 0.05;

/// can_take_town ai_player.cpp:69 - the threat simulation deliberately favours the attacker.
inline constexpr double THREAT_ATTACKER_BIAS = 1.25;
inline constexpr double THREAT_DEFENDER_BIAS = 0.75;

/// type_town_threat_checker::mark_towns ai_player.cpp:146
inline constexpr int TOWN_THREAT_BOUNTY_POOL = 5000000;

/// value_of_hero_event philai.cpp:2392 - baseline "kill enemy heroes" incentive at zero bounty.
inline constexpr int HERO_ATTACK_BASELINE_BOUNTY = 10000;

/// type_AI_combat_data::check_wall_archery_penalty ai_combat.cpp:332
inline constexpr int WALL_ARCHERY_FORT = 4;
inline constexpr int WALL_ARCHERY_CITADEL = 5;
inline constexpr int WALL_ARCHERY_CASTLE = 6;
inline constexpr int WALL_ARCHERY_FLOOR = 2;
inline constexpr int WALL_ARCHERY_CAP = 4;

// ---------------------------------------------------------------------------
// A.9 - Spellcasting, morale & luck
// ---------------------------------------------------------------------------

/// value_of_luck_and_morale ai_tactical.cpp:34 - exact per-point coefficients.
inline constexpr double VALUE_PER_POSITIVE_POINT = 0.0173;
inline constexpr double VALUE_PER_NEGATIVE_LUCK = -0.0122;
inline constexpr double VALUE_PER_NEGATIVE_MORALE = -0.0833;
inline constexpr int LUCK_MORALE_FLOOR = -3;
inline constexpr int LUCK_MORALE_CEILING = 3;

/// simulate_attack ai_tactical.cpp:249 / get_exchange_effect ai_tactical.cpp:367
inline constexpr int SIMULATION_ROUNDS = 6;
inline constexpr int EXCHANGE_THRESHOLD_MULTIPLIER = 5;

/// get_attack_time ai_tactical.cpp:575 - cost sentinels for a zero-speed acting creature.
inline constexpr int ATTACK_TIME_UNREACHABLE = 100;
inline constexpr int ATTACK_TIME_ADJACENT = 1;

/// consider_resurrect ai_tactical.cpp:2608 / consider_sacrifice ai_tactical.cpp:2685
inline constexpr int RESURRECT_MIN_LOSS_SHIFT = 2;   ///< skipped below 25% recoverable loss (>>2)
inline constexpr int LATE_FIGHT_ROUNDS = 2;
inline constexpr int LATE_FIGHT_MULTIPLIER = 2;

/// type_AI_spellcaster::cast_spell ai_tactical.cpp:3420
inline constexpr int ANTIMAGIC_ARTIFACT_LEVEL3 = 0x53;  ///< blocks level 3+ spells, either hero
inline constexpr int ANTIMAGIC_ARTIFACT_TOTAL = 0x7e;   ///< blocks casting entirely (combatManager::can_cast_spells ai.cpp:897)
inline constexpr int ANTIMAGIC_BLOCKED_LEVEL = 3;
inline constexpr int MANA_ABUNDANCE_MULTIPLE = 7;       ///< pool < 7x cost -> sqrt scale-down
inline constexpr double MANA_ABUNDANCE_BOOST = 2.5;     ///< exactly x5 then /2
inline constexpr int SPELL_RANDOM_MIN_PCT = 75;
inline constexpr int SPELL_RANDOM_MAX_PCT = 100;

/// consider_single_enchantment ai_tactical.cpp:2455 - exact Backlash resistance discount.
inline constexpr int RESIST_DISCOUNT_NUM = 2;
inline constexpr int RESIST_DISCOUNT_DEN = 100;

/// get_disease_value ai_tactical.cpp:1498 - value is the tenth left after subtracting 90%.
inline constexpr double DISEASE_SUBTRAHEND = 0.90;

/// get_disruptive_ray_value ai_tactical.cpp:1638 / get_weakness_value ai_tactical.cpp:1671
inline constexpr double DEBUFF_RATE = 0.05;
inline constexpr double DEBUFF_CEILING = 1.0;

/// get_blind_value ai_tactical.cpp:1732
inline constexpr double BLIND_BASE_VALUE = 400.0;
inline constexpr double BLIND_CONDITIONAL_HALVING = 0.5;

/// get_attack_boost_value ai_tactical.cpp:1158 and three siblings share this unity rate.
inline constexpr double UNITY_RATE = 1.0;

inline constexpr int EARTHQUAKE_DIVISOR = 3;         ///< consider_earthquake ai_tactical.cpp:3016
inline constexpr int SUMMON_MULTIPLIER = 1000;       ///< consider_summon ai_tactical.cpp:3093
inline constexpr int COUNTERSTROKE_MULTIPLIER = 2;   ///< get_counterstroke_value ai_tactical.cpp:2249
inline constexpr int BACKLASH_ASSUMED_LEVEL = 5;     ///< get_backlash_value ai_tactical.cpp:2235
inline constexpr int AGE_DIVISOR = 3;                ///< get_age_value ai_tactical.cpp:1142
inline constexpr int AREA_EFFECT_HEX_SCAN = 187;     ///< get_area_effect_value ai_tactical.cpp:1008

// ---------------------------------------------------------------------------
// IV - Difficulty & personality
// ---------------------------------------------------------------------------

/// hero::GetMobility hero.cpp:5833 - the one place `personality` is actually consumed.
inline constexpr int MOBILITY_BONUS_DIFFICULTY_GATE = 2;
inline constexpr int MOBILITY_BONUS_DEFAULT = 75;    ///< 0x4b
inline constexpr int MOBILITY_BONUS_PERSONALITY2 = 125; ///< 0x7d
inline constexpr int PERSONALITY_MOBILITY = 2;

/// AI_value_of_combat ai_combat.cpp:1565 - defense_estimates[difficulty].
/// PHILAI-GAP: the 5-entry table is in the binary's data section. These preserve the
/// documented direction (the AI judges an identical enemy less threatening as difficulty rises)
/// but the exact figures were not recoverable.
inline constexpr double DEFENSE_ESTIMATE_BY_DIFFICULTY[5] = { 1.25, 1.10, 1.00, 0.90, 0.80 };

/// philAI::GetTurnAIVars philai.cpp:1770 - attack_computer_bonus, read straight off difficulty.
/// PHILAI-GAP: exact per-difficulty figures not recoverable; direction and use site are exact.
inline constexpr double ATTACK_BONUS_BY_DIFFICULTY[5] = { 0.80, 0.90, 1.00, 1.15, 1.30 };

} // namespace Const
} // namespace PhilAI
