/*
 * H3SpellData.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/*
 * SS 4.9b / SS 5B.4 - the per-spell data the original AI reads out of its spell-traits
 * array (base 0x685450, stride 0x88).  VCMI models spells differently and, critically,
 * carries no per-school-level AI value at all (CSpell::LevelInfo has cost and power but
 * no aiValue) and no counterpart of the original's six-bucket category field.  Both are
 * reproduced here so the valuations of SS 4.9b can be evaluated exactly.
 *
 *   category  - spellTraits + 0x0C & 0x1F8000.  Read STATICALLY out of the shipped
 *               Heroes3.exe: this field is compiled in, not loaded from SPTRAITS.TXT.
 *   level     - SPTRAITS.TXT column 3.
 *   power     - spellTraits + 0x30, SPTRAITS.TXT column 12: amount per point of spell power.
 *   effect[]  - spellTraits + 0x34[level], SPTRAITS.TXT columns 13-16.
 *   aiValue[] - spellTraits + 0x68[level], SPTRAITS.TXT columns 26-29.
 *
 * Nothing here is invented: every number is read either out of the binary or out of the
 * shipped data file, and the two agree wherever they overlap.
 */

namespace H3AI
{

/// SS 5B.4 - spellTraits + 0x0C bits 15..20.  This is the classification the original
/// dispatches on; it is NOT the same partition as VCMI's isOffensive/isSpecial/etc.
enum class H3SpellCategory
{
	NONE            = 0x000000,  ///< no evaluator - value_of_spell returns the token 1
	DIRECT_DAMAGE   = 0x008000,
	OPENING_DAMAGE  = 0x010000,  ///< quick combat only considers these in round 1
	HITS_BOTH_SIDES = 0x020000,  ///< Armageddon family - damages the caster's army too
	ENCHANTMENT     = 0x040000,
	RESURRECTION    = 0x080000,
	ADVENTURE       = 0x100000,  ///< never cast in combat
};

struct H3SpellInfo
{
	/// The raw masked value.  Kept as an integer, not the enum, because one spell
	/// (57, Titan's Lightning Bolt) carries 0x108000 - two category bits at once - which
	/// matches no case of value_of_spell's switch and so falls through to its default,
	/// the token value 1.  Its effect[0] of 600 is the flat damage the quick-combat
	/// value_of_buff special-cases by reading spellTraits[57] + 0x34 directly (SS 5B.4).
	unsigned category;
	int level;
	int power;        ///< per point of spell power
	int effect[4];    ///< by school level: none / basic / advanced / expert
	int aiValue[4];
};

/// Indexed by SpellID.  70 entries - the range hero::AI_update_valuations iterates.
inline constexpr int H3_SPELL_COUNT = 70;

inline constexpr H3SpellInfo H3_SPELLS[H3_SPELL_COUNT] = {
	{ (unsigned)H3SpellCategory::ADVENTURE,    1,   0, {  50,  50,  75, 100 }, {  10,  10,  20,  20 } },  //  0 Summon Boat
	{ (unsigned)H3SpellCategory::NONE,         2,   0, {  50,  50,  75, 100 }, {   1,   1,   1,   1 } },  //  1 Scuttle Boat
	{ (unsigned)H3SpellCategory::NONE,         2,   0, {   1,   1,   2,   3 }, {   1,   1,   1,   1 } },  //  2 Visions
	{ (unsigned)H3SpellCategory::NONE,         1,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  //  3 View Earth
	{ (unsigned)H3SpellCategory::NONE,         2,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  //  4 Disguise
	{ (unsigned)H3SpellCategory::NONE,         1,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  //  5 View Air
	{ (unsigned)H3SpellCategory::ADVENTURE,    5,   0, {  60,  60,  80, 100 }, {  30,  30,  40,  50 } },  //  6 Fly
	{ (unsigned)H3SpellCategory::ADVENTURE,    4,   0, {  60,  60,  80, 100 }, {  15,  15,  20,  25 } },  //  7 Water Walk
	{ (unsigned)H3SpellCategory::ADVENTURE,    5,   0, {   1,   2,   3,   4 }, {  30,  30,  40,  50 } },  //  8 Dimension Door
	{ (unsigned)H3SpellCategory::ADVENTURE,    4,   0, {   0,   0,   0,   0 }, {   5,   5,  70,  70 } },  //  9 Town Portal
	{ (unsigned)H3SpellCategory::NONE,         2,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  // 10 Quicksand
	{ (unsigned)H3SpellCategory::NONE,         3,  10, {  25,  25,  50, 100 }, {   1,   1,   1,   1 } },  // 11 Land Mine
	{ (unsigned)H3SpellCategory::NONE,         3,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  // 12 Force Field
	{ (unsigned)H3SpellCategory::NONE,         2,  10, {  10,  10,  20,  50 }, {   1,   1,   1,   1 } },  // 13 Fire Wall
	{ (unsigned)H3SpellCategory::ADVENTURE,    3,   1, {   2,   2,   3,   4 }, {   1,   1,   1,   1 } },  // 14 Earthquake
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 1,  10, {  10,  10,  20,  30 }, {  10,  10,  10,  10 } },  // 15 Magic Arrow
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 2,  20, {  10,  10,  20,  50 }, {  20,  20,  20,  20 } },  // 16 Ice Bolt
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 2,  25, {  10,  10,  20,  50 }, {  25,  25,  25,  25 } },  // 17 Lightning Bolt
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 5,  75, { 100, 100, 200, 300 }, {  75,  75,  75,  75 } },  // 18 Implosion
	{ (unsigned)H3SpellCategory::OPENING_DAMAGE, 4,  40, {  25,  25,  50, 100 }, {  75,  75,  75,  75 } },  // 19 Chain Lightning
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 3,  10, {  15,  15,  30,  60 }, {  12,  12,  12,  12 } },  // 20 Frost Ring
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 3,  10, {  15,  15,  30,  60 }, {  15,  15,  15,  15 } },  // 21 Fireball
	{ (unsigned)H3SpellCategory::OPENING_DAMAGE, 4,  10, {  20,  20,  40,  80 }, {  20,  20,  20,  20 } },  // 22 Inferno
	{ (unsigned)H3SpellCategory::DIRECT_DAMAGE, 4,  25, {  25,  25,  50, 100 }, {  37,  37,  37,  37 } },  // 23 Meteor Shower
	{ (unsigned)H3SpellCategory::HITS_BOTH_SIDES, 2,   5, {  10,  10,  20,  30 }, {  20,  20,  20,  20 } },  // 24 Death Ripple
	{ (unsigned)H3SpellCategory::HITS_BOTH_SIDES, 3,  10, {  10,  10,  20,  50 }, {  40,  40,  40,  40 } },  // 25 Destroy Undead
	{ (unsigned)H3SpellCategory::HITS_BOTH_SIDES, 4,  50, {  30,  30,  60, 120 }, { 100, 100, 100, 100 } },  // 26 Armageddon
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {  85,  85,  70,  70 }, {  10,  10,  20,  20 } },  // 27 Shield
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  3,   0, {  75,  75,  50,  50 }, {  20,  20,  50,  50 } },  // 28 Air Shield
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {  20,  20,  25,  30 }, {  10,  10,  12,  15 } },  // 29 Fire Shield
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  2,   0, {  70,  70,  50,  50 }, {   2,   2,   4,   4 } },  // 30 Protection from Air
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {  70,  70,  50,  50 }, {   2,   2,   4,   4 } },  // 31 Protection from Fire
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {  70,  70,  50,  50 }, {   2,   2,   4,   4 } },  // 32 Prot. from Water
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  3,   0, {  70,  70,  50,  50 }, {   2,   2,   4,   4 } },  // 33 Prot. from Earth
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  3,   0, {   4,   4,   5,   6 }, {  10,  10,  15,  20 } },  // 34 Anti-Magic
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {   0,   0,   0,   0 }, {   3,   3,   3,   3 } },  // 35 Dispel
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  5,   0, {  20,  20,  30,  40 }, {  12,  12,  18,  24 } },  // 36 Magic Mirror
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   5, {  10,  10,  20,  30 }, {   4,   4,   4,   4 } },  // 37 Cure
	{ (unsigned)H3SpellCategory::RESURRECTION, 4,  50, {  40,  40,  80, 160 }, {  50,  50,  60, 120 } },  // 38 Resurrection
	{ (unsigned)H3SpellCategory::RESURRECTION, 3,  50, {  30,  30,  60, 160 }, {  60,  60,  60, 120 } },  // 39 Animate Dead
	{ (unsigned)H3SpellCategory::NONE,         5,   1, {   3,   3,   6,  10 }, {   1,   1,   1,   1 } },  // 40 Sacrifice
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {   0,   0,   1,   1 }, {  10,  10,  11,  11 } },  // 41 Bless
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {   0,   0,   1,   1 }, {  10,  10,  11,  11 } },  // 42 Curse
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {   3,   3,   6,   6 }, {   7,   7,  15,  15 } },  // 43 Bloodlust
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  2,   0, {   3,   3,   6,   6 }, {   7,   7,  15,  15 } },  // 44 Precision
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  2,   0, {   3,   3,   6,   6 }, {   7,   7,  15,  15 } },  // 45 Weakness
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {   3,   3,   6,   6 }, {   7,   7,  15,  15 } },  // 46 Stone Skin
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  2,   0, {   3,   3,   4,   5 }, {   7,   7,  15,  15 } },  // 47 Disrupting Ray
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {   2,   2,   4,   4 }, {   8,   8,  16,  16 } },  // 48 Prayer
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  3,   0, {   1,   1,   2,   2 }, {   1,   1,   2,   2 } },  // 49 Mirth
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {   1,   1,   2,   2 }, {   1,   1,   2,   2 } },  // 50 Sorrow
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  2,   0, {   1,   1,   2,   2 }, {   1,   1,   2,   2 } },  // 51 Fortune
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  3,   0, {   1,   1,   2,   2 }, {   1,   1,   2,   2 } },  // 52 Misfortune
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {   3,   3,   5,   5 }, {   5,   5,  10,  10 } },  // 53 Haste
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  1,   0, {  75,  75,  50,  50 }, {   5,   5,  15,  15 } },  // 54 Slow
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {   8,   8,   8,   8 }, {   1,   1,   1,   1 } },  // 55 Slayer
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, { 100, 100, 150, 200 }, {  10,  10,  20,  30 } },  // 56 Frenzy
	{ 0x108000u,                               5,   0, { 600, 600, 600, 600 }, {  75,  75,  75,  75 } },  // 57 Titan's Lightning Bolt
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {   1,   1,   2,   2 }, {  20,  20,  30,  30 } },  // 58 Counterstrike
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {   0,   0,   1,   2 }, {  60,  60,  60,  60 } },  // 59 Berserk
	{ (unsigned)H3SpellCategory::RESURRECTION, 3,  25, {  10,  10,  20,  50 }, {  25,  25,  50, 100 } },  // 60 Hypnotize
	{ (unsigned)H3SpellCategory::NONE,         3,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  // 61 Forgetfulness
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  2,   0, {  50,  50,  25,   0 }, {  50,  50,  50,  50 } },  // 62 Blind
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  3,   0, {   0,   0,   0,   0 }, {  20,  20,  20,  20 } },  // 63 Teleport
	{ (unsigned)H3SpellCategory::NONE,         2,   0, {   0,   0,   0,   0 }, {   1,   1,   1,   1 } },  // 64 Remove Obstacle
	{ (unsigned)H3SpellCategory::ENCHANTMENT,  4,   0, {   5,   5,   6,   7 }, {  20,  20,  40,  50 } },  // 65 Clone
	{ (unsigned)H3SpellCategory::RESURRECTION, 5,   0, {   2,   2,   3,   4 }, {  50,  50, 100, 150 } },  // 66 Fire Elemental
	{ (unsigned)H3SpellCategory::RESURRECTION, 5,   0, {   2,   2,   3,   4 }, {  50,  50, 100, 150 } },  // 67 Earth Elemental
	{ (unsigned)H3SpellCategory::RESURRECTION, 5,   0, {   2,   2,   3,   4 }, {  50,  50, 100, 150 } },  // 68 Water Elemental
	{ (unsigned)H3SpellCategory::RESURRECTION, 5,   0, {   2,   2,   3,   4 }, {  50,  50, 100, 150 } },  // 69 Air Elemental
};
/*
 * SS 4.9b - g_castCurve, the 14 x 4 table of doubles at 0x640398 that both
 * type_spellvalue::value_of_buff and its damage arm index.
 *
 *   column 0 - saturation by number of affordable casts (index min(casts, 13))
 *   column 1 - descending breakpoints; the buff arm finds the first row whose
 *              column 1 is <= x
 *   columns 2, 3 - the slope and intercept of that row's linear segment
 *
 * The table ends at 0x640558, which is where the five Legion-artifact ids begin
 * (SS 4.9b) - a useful check that the row count is 14 and not 15.
 */
inline constexpr double H3_CAST_CURVE[14][4] = {
	{   0.0000,   4.8000,   0.0000,   4.0000 },  //  0 casts
	{   0.8300,   1.5570,   0.5000,   1.6000 },  //  1 casts
	{   1.5300,   0.7520,   0.9870,   0.8420 },  //  2 casts
	{   2.1100,   0.4330,   1.4520,   0.4920 },  //  3 casts
	{   2.5900,   0.2750,   1.8880,   0.3030 },  //  4 casts
	{   2.9900,   0.1860,   2.2910,   0.1920 },  //  5 casts
	{   3.3300,   0.1310,   2.6570,   0.1240 },  //  6 casts
	{   3.6000,   0.0950,   2.9860,   0.0810 },  //  7 casts
	{   3.8400,   0.0710,   3.2770,   0.0530 },  //  8 casts
	{   4.0300,   0.0540,   3.3530,   0.0350 },  //  9 casts
	{   4.1900,   0.0410,   3.7550,   0.0230 },  // 10 casts
	{   4.3300,   0.0320,   3.9470,   0.0160 },  // 11 casts
	{   4.4400,   0.0250,   4.1120,   0.0100 },  // 12 casts
	{   4.5300,   0.0000,   4.5300,   0.0000 },  // 13 casts
};

/// SS 4.9b - the clamps of the damage arm (0x63B7E0 and 0x640578).
inline constexpr double H3_SPELL_DAMAGE_FRACTION_CAP = 0.9;
inline constexpr double H3_SPELL_DAMAGE_TOTAL_CAP = 3.9;

/// SS 4.9b - the utility arm: sqrt(casts) * 0.001 + 0.009  (0x63A6A0, 0x640580).
inline constexpr double H3_SPELL_UTILITY_SLOPE = 0.001;
inline constexpr double H3_SPELL_UTILITY_BASE = 0.009;

/// SS 4.9b - the divisor of the mass-effect arm (0x5273A7, magic 0x5D9F7391 >> 8).
inline constexpr int H3_SPELL_MASS_DIVISOR = 700;

/// SS 4.9b - value_of_spell returns this when the category matches no arm.
inline constexpr int H3_SPELL_TOKEN_VALUE = 1;

/// SS 4.9b - get_best_spell_value's mask: all six category bits at once.
inline constexpr unsigned H3_SPELL_ALL_CATEGORIES = 0x1F8000u;

/// SS 4.9b - AI_get_spell_value compares only within this group when deciding whether a
/// new spell improves on what the hero already has.
inline constexpr unsigned H3_SPELL_COMPETING_GROUP = 0x38000u;

/// SS 4.9b - Recanter's Cloak (artifact 83) caps the hero at spell level 2.
inline constexpr int H3_ARTIFACT_RECANTERS_CLOAK = 83;
/// SS 4.9b - the Orb of Inhibition (126) makes type_spellvalue bail entirely.
inline constexpr int H3_ARTIFACT_ORB_OF_INHIBITION = 126;

}
