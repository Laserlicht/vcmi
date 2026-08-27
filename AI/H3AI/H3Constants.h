/*
 * H3Constants.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/*
 * Every magic number in this AI comes from the reverse-engineering report of the
 * original Heroes III (Complete 4.0) adventure AI.  Section numbers in comments
 * (e.g. "SS 4.11") refer to that report.  Nothing here is invented: if the report
 * does not state a value, the symbol says so at its definition instead of carrying a
 * silent guess.
 */

namespace H3AI
{

/// SS 4.11 / SS 6A.3 - 0x6604D0, multiplier applied to the *defender's* simulated army,
/// indexed by game difficulty (0 Easy .. 4 Impossible) and used ONLY when the defender
/// is human-owned; an AI-owned defender always uses the 1.25 default.
///
/// SS 6A.3 settles the direction of the difficulty word from five tables that must all
/// agree, the decisive one being the flat movement grant every AI hero receives on
/// Expert and Impossible (0x4E4B58) - a bonus with no reading other than "cheat".
inline constexpr double AI_ENEMY_STRENGTH_MULTIPLIER[5] = { 0.5, 0.5, 1.0, 1.25, 1.25 };

/// SS 4G.1 - 0x6604F8 / 0x6604FC.  These are NOT constants: advManager::AI_prepare
/// (0x527960) recomputes both from the difficulty every turn.  The 0.5/0.5 pair that
/// sits in the image is only what difficulty 1 happens to produce.
///
///   difficulty 0 (Easy):  computer +1.00, human -0.40
///   otherwise:            human = (d + 1) * 0.25,  computer = 0.75 - d * 0.25
///
/// Read as a handicap, not a cheat: the number inflates how the AI rates the armies of
/// the player it names, so a large computer bonus makes the AI overrate itself and pick
/// fights it loses.  That is why the largest value sits on Easy.
inline constexpr float ATTACK_BONUS_EASY_COMPUTER = 1.00f;
inline constexpr float ATTACK_BONUS_EASY_HUMAN = -0.40f;
inline constexpr float ATTACK_BONUS_STEP = 0.25f;
inline constexpr float ATTACK_BONUS_COMPUTER_BASE = 0.75f;

/// SS 4G.1 - computer side, by difficulty.
inline constexpr float attackComputerBonus(int difficulty)
{
	return difficulty == 0
		? ATTACK_BONUS_EASY_COMPUTER
		: ATTACK_BONUS_COMPUTER_BASE - difficulty * ATTACK_BONUS_STEP;
}

/// SS 4G.1 - human side, by difficulty.
inline constexpr float attackHumanBonus(int difficulty)
{
	return difficulty == 0
		? ATTACK_BONUS_EASY_HUMAN
		: (difficulty + 1) * ATTACK_BONUS_STEP;
}

/// SS 4.9 - 0x63B780 / 0x63B788 / 0x63B790 / 0x63B794
inline constexpr double MORALE_GOOD = 0.0173;
inline constexpr double MORALE_BAD = -0.0833;
inline constexpr double LUCK_GOOD = 0.0173;
inline constexpr double LUCK_BAD = -0.0122;

/// SS 4.9 - 0x67814C. Base constant of the experience valuation, and (SS 4B.10 step 3)
/// the tavern price of a hero. The report proves these are the same immediate.
inline constexpr int XP_VALUATION_BASE = 2500;
inline constexpr int TAVERN_HERO_COST = 2500;

/// SS 4.9 - the divisor of the XP valuation: (2500 + armyValue) / (40 * expForNextLevel)
inline constexpr int XP_VALUATION_LEVEL_DIVISOR = 40;

/// SS 4C.3 - the single constant every victory-condition branch injects.
inline constexpr int VICTORY_CONDITION_OVERRIDE = 5000000;

/// SS 4B.1 / 4B.2 - a fortified town we own, and the first town we capture.
inline constexpr int TOWN_BASE_VALUE = 5000000;

/// SS 4.6 / 4B.8 - "we would certainly lose" sentinel (0xE2329B00) and the
/// hard block written into the danger map (0xC4653600).
inline constexpr int CERTAIN_DEFEAT = -500000000;
inline constexpr int ABSOLUTE_NO_GO = -1000000000;

/// SS 4B.2 - the value a townless player still assigns to a hopeless town.
inline constexpr int HOPELESS_TOWN_FALLBACK = -2500000;

/// SS 4.5 - the decay law is value * 300 / (300 + extraMoveCost).
/// 300 movement points is ~1.5 tiles of grass (a plain grass step costs 100).
inline constexpr int INFLUENCE_DECAY_RANGE = 300;

/// SS 4.5 - the object's value is smeared over an 11x11 window around it.
inline constexpr int INFLUENCE_WINDOW = 11;
/// SS 4.5 - the local search is run with these limits.
inline constexpr int INFLUENCE_LOCAL_RANGE = 60000;
inline constexpr int INFLUENCE_LOCAL_TURN_LIMIT = 500;

/// SS 4.3 - the destination search radius ladder.
inline constexpr int SEARCH_RADIUS_UNLIMITED = 32000;  // 0x7D00
inline constexpr int SEARCH_RADIUS_MIN_WITH_GOAL = 1000;
inline constexpr int SEARCH_RADIUS_GOAL_SLACK = 200;
inline constexpr int SEARCH_RADIUS_PER_TILE = 200;
inline constexpr int DESTINATION_ATTEMPTS = 5;

/// SS 4.3 - an unexplored destination below this value is not worth walking to
/// when the scenario mode word equals 7.
inline constexpr int UNEXPLORED_MIN_VALUE = 75;

/// SS 4.4 - unexplored cells are simply worth this much (plus 100000 in "critical" mode).
inline constexpr int EXPLORATION_VALUE = 100;
inline constexpr int EXPLORATION_CRITICAL_BONUS = 100000;

/// SS 4.5 - "one movement quantum" = maxMovement * 21 / 100, ~1/5 of a turn.
inline constexpr int MOVEMENT_QUANTUM_NUMERATOR = 21;
inline constexpr int MOVEMENT_QUANTUM_DENOMINATOR = 100;

/// SS 4.6 - an enemy hero threatens everything within movementRemaining + 300.
inline constexpr int THREAT_RANGE_SLACK = 300;

/// SS 4G.4 - the kingdom-goal threat flood is wider than the danger map's: it runs from
/// each enemy hero with (that hero's MAXIMUM movement + 800), i.e. about 1.5 turns.
inline constexpr int KINGDOM_THREAT_RANGE_SLACK = 800;

/// SS 4A.1 - 0x678344, indexed by the number of marketplaces the player owns,
/// clamped to [1,10].  Index 0 is unused (the clamp never produces it).
inline constexpr double TRADE_RATE[11] = { 0.10, 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.50 };

/// SS 4A.1 - 0x68C482, base value per resource unit (wood, mercury, ore, sulfur, crystal, gems, gold).
inline constexpr double BASE_RESOURCE_VALUE[7] = { 250, 500, 250, 500, 500, 500, 1 };

/// SS 4A.1 - the creature wish-list keeps this many top entries.
inline constexpr int CREATURE_WISHLIST_TOP = 3;

/// SS 4.8 (Mine) - 0x678288, the daily yield of a mine per resource.
inline constexpr int MINE_DAILY_YIELD[7] = { 2, 1, 2, 1, 1, 1, 1000 };

/// SS 4B.9 - 0x660518, the largest hero count the AI will ever own, by difficulty.
inline constexpr int MAX_AI_HEROES[5] = { 2, 3, 4, 5, 6 };
/// SS 4B.9 - 0x66052C, once the humans collectively own this many heroes the AI stops hiring.
inline constexpr int HUMAN_HERO_CAP[5] = { 8, 11, 14, 17, 20 };
/// SS 4B.9 - engine cap on heroes per player.
inline constexpr int ENGINE_HERO_CAP = 8;

/// SS 4.12 - 0x640380 / 0x64038C, marginal value of the *next* level of the skill,
/// indexed by the level currently held (0 = absent, 1 = basic, 2 = advanced).
inline constexpr int ARCHERY_TABLE[3] = { 16, 7, 11 };
inline constexpr int ESTATES_TABLE[3] = { 725, 725, 1550 };

/// SS 4.12 - the two floors used when pricing a skill against the hero's army.
inline constexpr int SKILL_ARMY_FLOOR = 1000;
inline constexpr int SKILL_SHOOTER_FLOOR = 500;

/// SS 4.12 - 0x640570 and 0x63AC40, the two static divisors of the Pathfinding
/// and Navigation arms.
inline constexpr double PATHFINDING_DIVISOR = 4.0;
inline constexpr double NAVIGATION_DIVISOR = 2.0;

/// SS 4B.4 - take_best_stack scores every candidate as skillDiff * valueOfAdding / 40.
inline constexpr int TROOP_EXCHANGE_DIVISOR = 40;

/// SS 4B.4 - the alignment gate: a second town's creatures are only mixed in when
/// the army we already have is worth at least ten times the newcomer.
inline constexpr int ALIGNMENT_MIX_RATIO = 10;

/// SS 4B.4 - the movement penalty adds +500 to the army value before scaling.
inline constexpr int MOVEMENT_PENALTY_ARMY_SLACK = 500;

/// SS 4.1 - hero::get_primary_skill_sum clamps each primary skill at 99, and
/// spell power / knowledge count as at least 1.
inline constexpr int PRIMARY_SKILL_CLAMP = 99;

/// SS 4.9 - every luck/morale object scales by (primarySkillSum + 40) / 40.
inline constexpr int PRIORITY_SCALE_OFFSET = 40;
inline constexpr int PRIORITY_SCALE_DIVISOR = 40;

/// SS 4B.6 - ally gifting thresholds.
inline constexpr int ALLY_GOLD_RESERVE = 10000;
inline constexpr int ALLY_RESOURCE_RESERVE = 20;
inline constexpr int ALLY_MIN_GOLD_GIFT = 1000;
inline constexpr int ALLY_MIN_RESOURCE_GIFT = 5;
inline constexpr int ALLY_POVERTY_RATIO = 5;

/// SS 4A.3 - AI_plan_trades clamps reserved_funds up to this floor.
inline constexpr int TRADE_RESERVE_FLOOR = 20;

/// SS 4A.3 - get_total_value returns base * 1000 / cost.
inline constexpr int TOTAL_VALUE_SCALE = 1000;

/// SS 4B.5 - refuse a garrison exchange that leaves the hero with less than a third
/// of the army it arrived with, but only when the move limit is at least this large.
inline constexpr int RECRUIT_VALUE_MOVE_LIMIT = 400;
inline constexpr int RECRUIT_VALUE_ARMY_DIVISOR = 3;

/// SS 4B.1 - fortified-town revisit baseline and Grail delivery use the same override.
/// SS 4.8 - Learning Stone grants a flat 1000 experience (0x63E4F0).
inline constexpr float LEARNING_STONE_XP = 1000.0f;

/// SS 4.8 - Treasure Chest, three tiers of max(xpValue * k, goldValue * g).
inline constexpr int TREASURE_CHEST_XP[3] = { 160, 320, 465 };
inline constexpr int TREASURE_CHEST_GOLD[3] = { 320, 480, 620 };
inline constexpr int TREASURE_CHEST_ARTIFACT_DIVISOR = 20;

/// SS 4.8 - assorted flat rewards read straight off the handlers.
inline constexpr int KEYMASTER_VALUE = 5000;
inline constexpr int CAMPFIRE_GOLD = 100;
inline constexpr int FLOTSAM_GOLD = 175;
inline constexpr int FLOTSAM_WOOD = 5;
inline constexpr int FOUNTAIN_OF_YOUTH_MP = 200;
inline constexpr int FOUNTAIN_OF_YOUTH_FALLBACK = 10000;
inline constexpr int LIGHTHOUSE_VALUE = 1000;
inline constexpr int SHIPYARD_VALUE = 1000;
inline constexpr int CORPSE_ARTIFACT_DIVISOR = 5;
inline constexpr int CORPSE_GOLD = 200;
inline constexpr int LEAN_TO_MULTIPLIER = 3;
inline constexpr int SEA_CHEST_ARTIFACT_DIVISOR = 10;
inline constexpr int SEA_CHEST_GOLD = 1200;
inline constexpr int MYSTICAL_GARDEN_GOLD = 500;
inline constexpr int MYSTICAL_GARDEN_GEMS = 5;
inline constexpr int PRISON_XP_GOLD = 2500;
inline constexpr int SCHOOL_OF_MAGIC_COST = 1000;

/// SS 4.8a - 0x52B790: the School of War refuses to be valued below this much gold.
inline constexpr int SCHOOL_OF_WAR_COST = 1000;
inline constexpr int TREE_OF_KNOWLEDGE_GOLD_COST = 1000;
inline constexpr int UNIVERSITY_GEMS_COST = 10;
inline constexpr int UNIVERSITY_GOLD_COST = 2000;
inline constexpr int UNIVERSITY_DIVISOR = 3;
inline constexpr int STABLES_MOVEMENT = 400;
inline constexpr int OASIS_MOVEMENT = 400;
inline constexpr int RALLY_FLAG_MOVEMENT = 200;
inline constexpr int WATERING_HOLE_MOVEMENT = 200;

/// SS 4.8a - the flat value a movement-granting object returns when its grant covers the
/// whole remaining approach cost, i.e. "this object is free to reach".  The original's
/// `int* limit` argument is the movement cost of reaching the object, not a bound.
inline constexpr int MOVEMENT_GRANT_SENTINEL = 10000;

/// SS 4.8a - 0x52AAC0: what the Stables movement grant is worth when the hero could
/// have made the trip anyway.
inline constexpr int STABLES_SMALL_VALUE = 50;
/// SS 4.8a - the x1.2 the Stables applies when the hero already fields Champions.
inline constexpr double STABLES_MERGE_BONUS = 1.2;
/// SS 4.8a - creature ids the Stables arm names directly (CRTRAITS.TXT order).
inline constexpr int STABLES_CAVALIER = 10;
inline constexpr int STABLES_CHAMPION = 11;

/// SS 4.8a - 0x52A960: Sirens keep 70 % of every stack of more than one.
inline constexpr float SIRENS_KEEP_FRACTION = 0.7f;

/// SS 4.8a - 0x52A8C0: the Spell Scroll's floor, added again on top of a guard fight.
inline constexpr int SPELL_SCROLL_FLOOR = 10;

/// SS 4.8 - the gold each war machine costs at the War Machine Factory.
inline constexpr int WAR_MACHINE_COST = 1000;

/// SS 4.8a - g_hillFortDiscount, floats at 0x63EB4C, indexed by creature level - 1.
/// The gold half of an upgrade cost is scaled by this; levels 5-7 pay in full.
inline constexpr float HILL_FORT_GOLD_DISCOUNT[7] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 1.0f };

/// SS 4.8a - 0x432220 is called with radius 20 for the Redwood Observatory and the
/// Pillar of Fire, and with radius 10 for the Eye of the Magi sweep (SS 4G.3).
inline constexpr int SCOUTING_RADIUS = 20;
inline constexpr int EYE_OF_MAGI_RADIUS = 10;

/// SS 5D.3 - the artifacts hero::creature_speed_bonus (0x4E5AA0) and
/// hero::creature_hp_bonus (0x4E5B80) name by raw id, checked against ARTRAITS.TXT.
inline constexpr int ART_RING_OF_THE_WAYFARER = 69;
inline constexpr int ART_RING_OF_VITALITY = 94;
inline constexpr int ART_RING_OF_LIFE = 95;
inline constexpr int ART_VIAL_OF_LIFEBLOOD = 96;
inline constexpr int ART_NECKLACE_OF_SWIFTNESS = 97;
inline constexpr int ART_CAPE_OF_VELOCITY = 99;
inline constexpr int ART_ELIXIR_OF_LIFE = 131;

/// SS 4.12 - the First Aid arm returns this flat value once its Tent gate passes.
/// SS 5D.3 - g_learning_factor, indexed by Learning secondary-skill level, used by
/// hero::xp_reward_factor (0x4E4840) as `1.0 + factor`.
inline constexpr float LEARNING_XP_FACTOR[4] = { 0.00f, 0.05f, 0.10f, 0.15f };

inline constexpr int FIRST_AID_SKILL_VALUE = 250;
/// SS 4.12 - a hero may hold eight secondary skills; the ranking rule counts free slots.
inline constexpr int MAX_SECONDARY_SKILLS = 8;
inline constexpr int LIBRARY_LEVEL_REQUIREMENT = 10;
inline constexpr int LIBRARY_ARMY_DIVISOR = 10;
inline constexpr int BACKPACK_FULL = 64;

/// SS 4.8 - the artifact pickup sub-switch (jump table 0x529670).
/// SS 4G.1 - AI_prepare's average-artifact loop runs artifact ids 7..143.  Ids 0..6
/// (spellbook, spell scroll, the Grail and the four war machines) are excluded outright.
inline constexpr int AI_PREPARE_FIRST_ARTIFACT = 7;
inline constexpr int AI_PREPARE_LAST_ARTIFACT = 143;

inline constexpr int ARTIFACT_MIN_VALUE = 10;
inline constexpr int ARTIFACT_PAY_GOLD_1 = 2000;
inline constexpr int ARTIFACT_PAY_GOLD_4 = 2500;
inline constexpr int ARTIFACT_PAY_GOLD_5 = 3000;

/// SS 4F.4 - Dimension Door: the AI keeps this much mana in reserve.
inline constexpr int DIMENSION_DOOR_MANA_RESERVE = 20;

/// SS 4D.1 - a portal transition costs a flat 50 movement points.
inline constexpr int PORTAL_TRANSITION_COST = 50;

/// SS 4B.11 - the step-list builder asks for a path far longer than any real
/// movement allowance so that the list spans the whole route.
inline constexpr int PATH_BUILD_LIMIT = 99999;

/// SS 4.8 - the Campfire arm: "100 * amount * goldValue + amount * resourceValue[type]",
/// where `amount` is the non-gold half of the roll.
inline constexpr int CAMPFIRE_GOLD_PER_UNIT = 100;

/// SS 4.8a - the Seer Hut arm's floor for a quest whose terms the hero has not been told
/// yet: "return max(reward, 20)".
inline constexpr int SEER_HUT_UNKNOWN_FLOOR = 20;

/// SS 4.9 - experience_for_level uses a geometric tail with this ratio above level 12.
inline constexpr double EXPERIENCE_GEOMETRIC_RATIO = 1.2;
inline constexpr int EXPERIENCE_TABLE_LAST_LEVEL = 12;

}
