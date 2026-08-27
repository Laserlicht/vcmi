/*
 * H3ArtifactData.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

/*
 * SS 4.9a - the artifact -> effect binding the original builds at start-up.
 *
 * In Heroes3.exe this is g_artifactEffects, a std::vector<type_artifact_effect*>[144] at
 * 0x692E18, populated by the loader at 0x4340E0 from an int32 stream at 0x63AC7C.  The
 * stream is decoded here into static data because VCMI expresses artifact effects as
 * bonuses, which is a different model: the AI does not price an artifact by what it does,
 * it prices it by which of 24 hard-coded valuation classes are attached to it.
 *
 * The decode is checkable against ARTRAITS.TXT and agrees on every cross-check:
 * Titan's Thunder -> SPELL(57) and spell 57 is Titan's Lightning Bolt; Everflowing Crystal
 * Cloak -> INCOME(1, 4) and resource 4 is crystal; Tome of Fire Magic -> TOME(2) and Orb of
 * Tempestuous Fire -> SCHOOL(2, 100), fixing the school mask as 1 Air / 2 Fire / 4 Water /
 * 8 Earth; The Grail -> INCOME(5000, gold) plus growth on all seven dwelling levels.
 *
 * Nine artifact ids carry no effects at all (0 Spell Book, 1 Spell Scroll, 3 Catapult,
 * 4 Ballista, 5 Ammo Cart, 6 First Aid Tent and 141-143): the first six are handled by the
 * hard-coded arms of AI_get_value_of_artifact instead (SS 4.9a).
 */

namespace H3AI
{

/// SS 4.9a - the 24 type_artifact_effect classes, in the order of the loader's jump
/// table at 0x434530.  Each is one valuation formula; see SS 4.9a for the bodies.
enum class H3ArtifactEffectKind
{
	MIGHT              =  0,
	POWER              =  1,
	KNOWLEDGE          =  2,
	MORALE             =  3,
	LUCK               =  4,
	SCOUTING           =  5,
	NECROMANCY         =  6,
	COMBAT             =  7,
	MOVEMENT           =  8,
	SPELLCASTER        =  9,
	DURATION           = 10,
	SCHOOL             = 11,
	TOME               = 12,
	ANTIMAGIC          = 13,
	ANTIMORALE         = 14,
	ANTILUCK           = 15,
	INCOME             = 16,
	CREATURE_GROWTH    = 17,
	SPELL              = 18,
	SHOOTER_BONUS      = 19,
	ANGELIC_ALLIANCE   = 20,
	UNDEAD_KING_CLOAK  = 21,
	ELIXIR_OF_LIFE     = 22,
	STATUE_OF_LEGION   = 23,
};

struct H3ArtifactEffect
{
	H3ArtifactEffectKind kind;
	int magnitude;   ///< effect + 0x04
	int aux;         ///< effect + 0x08 (school mask, resource id, growth count); 0 when unused
};

struct H3ArtifactEffects
{
	int artifact;
	int count;
	H3ArtifactEffect effects[8];
};

inline constexpr int H3_ARTIFACT_EFFECT_ROWS = 135;

inline constexpr H3ArtifactEffects H3_ARTIFACT_EFFECTS[H3_ARTIFACT_EFFECT_ROWS] = {
	{   2, 8, { { H3ArtifactEffectKind::INCOME, 5000, 6 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 0, 5 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 1, 4 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 2, 3 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 3, 2 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 4, 1 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 5, 1 }, { H3ArtifactEffectKind::CREATURE_GROWTH, 6, 1 } } },  // The Grail
	{   7, 1, { { H3ArtifactEffectKind::MIGHT, 1, 0 } } },  // Centaurs Axe
	{   8, 1, { { H3ArtifactEffectKind::MIGHT, 2, 0 } } },  // Blackshard of the Dead Knight
	{   9, 1, { { H3ArtifactEffectKind::MIGHT, 3, 0 } } },  // Greater Gnoll's Flail
	{  10, 1, { { H3ArtifactEffectKind::MIGHT, 4, 0 } } },  // Ogre's Club of Havoc
	{  11, 1, { { H3ArtifactEffectKind::MIGHT, 5, 0 } } },  // Sword of Hellfire
	{  12, 1, { { H3ArtifactEffectKind::MIGHT, 9, 0 } } },  // Titan's Gladius
	{  13, 1, { { H3ArtifactEffectKind::MIGHT, 1, 0 } } },  // Shield of the Dwarven Lords
	{  14, 1, { { H3ArtifactEffectKind::MIGHT, 2, 0 } } },  // Shield of the Yawning Dead
	{  15, 1, { { H3ArtifactEffectKind::MIGHT, 3, 0 } } },  // Buckler of the Gnoll King
	{  16, 1, { { H3ArtifactEffectKind::MIGHT, 4, 0 } } },  // Targ of the Rampaging Ogre
	{  17, 1, { { H3ArtifactEffectKind::MIGHT, 5, 0 } } },  // Shield of the Damned
	{  18, 1, { { H3ArtifactEffectKind::MIGHT, 9, 0 } } },  // Sentinel's Shield
	{  19, 1, { { H3ArtifactEffectKind::KNOWLEDGE, 1, 0 } } },  // Helm of the Alabaster Unicorn
	{  20, 1, { { H3ArtifactEffectKind::KNOWLEDGE, 2, 0 } } },  // Skull Helmet
	{  21, 1, { { H3ArtifactEffectKind::KNOWLEDGE, 3, 0 } } },  // Helm of Chaos
	{  22, 1, { { H3ArtifactEffectKind::KNOWLEDGE, 4, 0 } } },  // Crown of the Supreme Magi
	{  23, 1, { { H3ArtifactEffectKind::KNOWLEDGE, 5, 0 } } },  // Hellstorm Helmet
	{  24, 2, { { H3ArtifactEffectKind::KNOWLEDGE, 12, 0 }, { H3ArtifactEffectKind::POWER, -3, 0 } } },  // Thunder Helmet
	{  25, 1, { { H3ArtifactEffectKind::POWER, 1, 0 } } },  // Breastplate of Petrified Wood
	{  26, 1, { { H3ArtifactEffectKind::POWER, 2, 0 } } },  // Rib Cage
	{  27, 1, { { H3ArtifactEffectKind::POWER, 3, 0 } } },  // Scales of the Greater Basilisk
	{  28, 1, { { H3ArtifactEffectKind::POWER, 4, 0 } } },  // Tunic of the Cyclops King
	{  29, 1, { { H3ArtifactEffectKind::POWER, 5, 0 } } },  // Breastplate of Brimstone
	{  30, 2, { { H3ArtifactEffectKind::POWER, 12, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, -3, 0 } } },  // Titan's Cuirass
	{  31, 3, { { H3ArtifactEffectKind::MIGHT, 2, 0 }, { H3ArtifactEffectKind::POWER, 1, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 1, 0 } } },  // Armor of Wonder
	{  32, 3, { { H3ArtifactEffectKind::MIGHT, 4, 0 }, { H3ArtifactEffectKind::POWER, 2, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 2, 0 } } },  // Sandals of the Saint
	{  33, 3, { { H3ArtifactEffectKind::MIGHT, 6, 0 }, { H3ArtifactEffectKind::POWER, 3, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 3, 0 } } },  // Celestial Necklace of Bliss
	{  34, 3, { { H3ArtifactEffectKind::MIGHT, 8, 0 }, { H3ArtifactEffectKind::POWER, 4, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 4, 0 } } },  // Lion's Shield of Courage
	{  35, 3, { { H3ArtifactEffectKind::MIGHT, 10, 0 }, { H3ArtifactEffectKind::POWER, 5, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 5, 0 } } },  // Sword of Judgement
	{  36, 3, { { H3ArtifactEffectKind::MIGHT, 12, 0 }, { H3ArtifactEffectKind::POWER, 6, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 6, 0 } } },  // Helm of Heavenly Enlightenment
	{  37, 1, { { H3ArtifactEffectKind::MIGHT, 2, 0 } } },  // Quiet Eye of the Dragon
	{  38, 1, { { H3ArtifactEffectKind::MIGHT, 4, 0 } } },  // Red Dragon Flame Tongue
	{  39, 1, { { H3ArtifactEffectKind::MIGHT, 6, 0 } } },  // Dragon Scale Shield
	{  40, 1, { { H3ArtifactEffectKind::MIGHT, 8, 0 } } },  // Dragon Scale Armor
	{  41, 2, { { H3ArtifactEffectKind::POWER, 1, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 1, 0 } } },  // Dragonbone Greaves
	{  42, 2, { { H3ArtifactEffectKind::POWER, 2, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 2, 0 } } },  // Dragon Wing Tabard
	{  43, 2, { { H3ArtifactEffectKind::POWER, 3, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 3, 0 } } },  // Necklace of Dragonteeth
	{  44, 2, { { H3ArtifactEffectKind::POWER, 4, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 4, 0 } } },  // Crown of Dragontooth
	{  45, 2, { { H3ArtifactEffectKind::LUCK, 1, 0 }, { H3ArtifactEffectKind::MORALE, 1, 0 } } },  // Still Eye of the Dragon
	{  46, 1, { { H3ArtifactEffectKind::LUCK, 1, 0 } } },  // Clover of Fortune
	{  47, 1, { { H3ArtifactEffectKind::LUCK, 1, 0 } } },  // Cards of Prophecy
	{  48, 1, { { H3ArtifactEffectKind::LUCK, 1, 0 } } },  // Ladybird of Luck
	{  49, 1, { { H3ArtifactEffectKind::MORALE, 1, 0 } } },  // Badge of Courage
	{  50, 1, { { H3ArtifactEffectKind::MORALE, 1, 0 } } },  // Crest of Valor
	{  51, 1, { { H3ArtifactEffectKind::MORALE, 1, 0 } } },  // Glyph of Gallantry
	{  52, 1, { { H3ArtifactEffectKind::SCOUTING, 1, 0 } } },  // Speculum
	{  53, 1, { { H3ArtifactEffectKind::SCOUTING, 1, 0 } } },  // Spyglass
	{  54, 1, { { H3ArtifactEffectKind::NECROMANCY, 5, 0 } } },  // Amulet of the Undertaker
	{  55, 1, { { H3ArtifactEffectKind::NECROMANCY, 10, 0 } } },  // Vampire's Cowl
	{  56, 1, { { H3ArtifactEffectKind::NECROMANCY, 15, 0 } } },  // Dead Man's Boots
	{  57, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Garniture of Interference
	{  58, 1, { { H3ArtifactEffectKind::COMBAT, 3, 0 } } },  // Surcoat of Counterpoise
	{  59, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Boots of Polarity
	{  60, 1, { { H3ArtifactEffectKind::SHOOTER_BONUS, 2, 0 } } },  // Bow of Elven Cherrywood
	{  61, 1, { { H3ArtifactEffectKind::SHOOTER_BONUS, 5, 0 } } },  // Bowstring of the Unicorn's Mane
	{  62, 1, { { H3ArtifactEffectKind::SHOOTER_BONUS, 7, 0 } } },  // Angel Feather Arrows
	{  63, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Bird of Perception
	{  64, 1, { { H3ArtifactEffectKind::COMBAT, 2, 0 } } },  // Stoic Watchman
	{  65, 1, { { H3ArtifactEffectKind::COMBAT, 3, 0 } } },  // Emblem of Cognizance
	{  66, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Statesman's Medal
	{  67, 1, { { H3ArtifactEffectKind::COMBAT, 2, 0 } } },  // Diplomat's Ring
	{  68, 1, { { H3ArtifactEffectKind::COMBAT, 3, 0 } } },  // Ambassador's Sash
	{  69, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Ring of the Wayfarer
	{  70, 1, { { H3ArtifactEffectKind::MOVEMENT, 12, 0 } } },  // Equestrian's Gloves
	{  71, 1, { { H3ArtifactEffectKind::MOVEMENT, 25, 0 } } },  // Necklace of Ocean Guidance
	{  72, 1, { { H3ArtifactEffectKind::MOVEMENT, 50, 0 } } },  // Angel Wings
	{  73, 1, { { H3ArtifactEffectKind::SPELLCASTER, 2, 0 } } },  // Charm of Mana
	{  74, 1, { { H3ArtifactEffectKind::SPELLCASTER, 4, 0 } } },  // Talisman of Mana
	{  75, 1, { { H3ArtifactEffectKind::SPELLCASTER, 6, 0 } } },  // Mystic Orb of Mana
	{  76, 1, { { H3ArtifactEffectKind::DURATION, 1, 0 } } },  // Collar of Conjuring
	{  77, 1, { { H3ArtifactEffectKind::DURATION, 2, 0 } } },  // Ring of Conjuring
	{  78, 1, { { H3ArtifactEffectKind::DURATION, 3, 0 } } },  // Cape of Conjuring
	{  79, 1, { { H3ArtifactEffectKind::SCHOOL, 100, 1 } } },  // Orb of the Firmament
	{  80, 1, { { H3ArtifactEffectKind::SCHOOL, 100, 8 } } },  // Orb of Silt
	{  81, 1, { { H3ArtifactEffectKind::SCHOOL, 100, 2 } } },  // Orb of Tempestuous Fire
	{  82, 1, { { H3ArtifactEffectKind::SCHOOL, 100, 4 } } },  // Orb of Driving Rain
	{  83, 1, { { H3ArtifactEffectKind::ANTIMAGIC, 3, 0 } } },  // Recanter's Cloak
	{  84, 1, { { H3ArtifactEffectKind::ANTIMORALE, 0, 0 } } },  // Spirit of Oppression
	{  85, 1, { { H3ArtifactEffectKind::ANTILUCK, 0, 0 } } },  // Hourglass of the Evil Hour
	{  86, 1, { { H3ArtifactEffectKind::TOME, 0, 2 } } },  // Tome of Fire Magic
	{  87, 1, { { H3ArtifactEffectKind::TOME, 0, 1 } } },  // Tome of Air Magic
	{  88, 1, { { H3ArtifactEffectKind::TOME, 0, 4 } } },  // Tome of Water Magic
	{  89, 1, { { H3ArtifactEffectKind::TOME, 0, 8 } } },  // Tome of Earth Magic
	{  90, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Boots of Levitation
	{  91, 1, { { H3ArtifactEffectKind::SHOOTER_BONUS, 5, 0 } } },  // Golden Bow
	{  92, 1, { { H3ArtifactEffectKind::COMBAT, 2, 0 } } },  // Sphere of Permanence
	{  93, 1, { { H3ArtifactEffectKind::COMBAT, 10, 0 } } },  // Orb of Vulnerability
	{  94, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Ring of Vitality
	{  95, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Ring of Life
	{  96, 1, { { H3ArtifactEffectKind::COMBAT, 2, 0 } } },  // Vial of Lifeblood
	{  97, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Necklace of Swiftness
	{  98, 1, { { H3ArtifactEffectKind::MOVEMENT, 25, 0 } } },  // Boots of Speed
	{  99, 1, { { H3ArtifactEffectKind::COMBAT, 10, 0 } } },  // Cape of Velocity
	{ 100, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Pendant of Dispassion
	{ 101, 1, { { H3ArtifactEffectKind::COMBAT, 10, 0 } } },  // Pendant of Second Sight
	{ 102, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Pendant of Holiness
	{ 103, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Pendant of Life
	{ 104, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Pendant of Death
	{ 105, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Pendant of Free Will
	{ 106, 1, { { H3ArtifactEffectKind::COMBAT, 10, 0 } } },  // Pendant of Negativity
	{ 107, 1, { { H3ArtifactEffectKind::COMBAT, 1, 0 } } },  // Pendant of Total Recall
	{ 108, 1, { { H3ArtifactEffectKind::COMBAT, 10, 0 } } },  // Pendant of Courage
	{ 109, 1, { { H3ArtifactEffectKind::INCOME, 1, 4 } } },  // Everflowing Crystal Cloak
	{ 110, 1, { { H3ArtifactEffectKind::INCOME, 1, 5 } } },  // Ring of Infinite Gems
	{ 111, 1, { { H3ArtifactEffectKind::INCOME, 1, 1 } } },  // Everpouring Vial of Mercury
	{ 112, 1, { { H3ArtifactEffectKind::INCOME, 1, 2 } } },  // Inexhaustible Cart of Ore
	{ 113, 1, { { H3ArtifactEffectKind::INCOME, 1, 3 } } },  // Eversmoking Ring of Sulfur
	{ 114, 1, { { H3ArtifactEffectKind::INCOME, 1, 0 } } },  // Inexhaustible Cart of Lumber
	{ 115, 1, { { H3ArtifactEffectKind::INCOME, 1000, 6 } } },  // Endless Sack of Gold
	{ 116, 1, { { H3ArtifactEffectKind::INCOME, 750, 6 } } },  // Endless Bag of Gold
	{ 117, 1, { { H3ArtifactEffectKind::INCOME, 500, 6 } } },  // Endless Purse of Gold
	{ 118, 1, { { H3ArtifactEffectKind::CREATURE_GROWTH, 1, 5 } } },  // Legs of Legion
	{ 119, 1, { { H3ArtifactEffectKind::CREATURE_GROWTH, 2, 4 } } },  // Loins of Legion
	{ 120, 1, { { H3ArtifactEffectKind::CREATURE_GROWTH, 3, 3 } } },  // Torso of Legion
	{ 121, 1, { { H3ArtifactEffectKind::CREATURE_GROWTH, 4, 2 } } },  // Arms of Legion
	{ 122, 1, { { H3ArtifactEffectKind::CREATURE_GROWTH, 5, 1 } } },  // Head of Legion
	{ 123, 1, { { H3ArtifactEffectKind::MOVEMENT, 5, 0 } } },  // Sea Captain's Hat
	{ 124, 1, { { H3ArtifactEffectKind::SPELLCASTER, 20, 0 } } },  // Spellbinder's Hat
	{ 125, 1, { { H3ArtifactEffectKind::COMBAT, 5, 0 } } },  // Shackles of War
	{ 126, 1, { { H3ArtifactEffectKind::ANTIMAGIC, 0, 0 } } },  // Orb of Inhibition
	{ 127, 1, { { H3ArtifactEffectKind::MIGHT, 10, 0 } } },  // Vial of Dragon Blood
	{ 128, 3, { { H3ArtifactEffectKind::MIGHT, 6, 0 }, { H3ArtifactEffectKind::POWER, 3, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 6, 0 } } },  // Armageddon's Blade
	{ 129, 1, { { H3ArtifactEffectKind::ANGELIC_ALLIANCE, 0, 0 } } },  // Angelic Alliance
	{ 130, 1, { { H3ArtifactEffectKind::UNDEAD_KING_CLOAK, 0, 0 } } },  // Cloak of the Undead King
	{ 131, 1, { { H3ArtifactEffectKind::ELIXIR_OF_LIFE, 0, 0 } } },  // Elixir of Life
	{ 132, 3, { { H3ArtifactEffectKind::COMBAT, 15, 0 }, { H3ArtifactEffectKind::MIGHT, 6, 0 }, { H3ArtifactEffectKind::SHOOTER_BONUS, 10, 0 } } },  // Armor of the Damned
	{ 133, 1, { { H3ArtifactEffectKind::STATUE_OF_LEGION, 0, 0 } } },  // Statue of Legion
	{ 134, 4, { { H3ArtifactEffectKind::MIGHT, 12, 0 }, { H3ArtifactEffectKind::POWER, 6, 0 }, { H3ArtifactEffectKind::KNOWLEDGE, 6, 0 }, { H3ArtifactEffectKind::COMBAT, 10, 0 } } },  // Power of the Dragon Father
	{ 135, 1, { { H3ArtifactEffectKind::SPELL, 57, 0 } } },  // Titan's Thunder
	{ 136, 1, { { H3ArtifactEffectKind::MOVEMENT, 12, 0 } } },  // Admiral's Hat
	{ 137, 1, { { H3ArtifactEffectKind::SHOOTER_BONUS, 7, 0 } } },  // Bow of the Sharpshooter
	{ 138, 1, { { H3ArtifactEffectKind::SPELLCASTER, 10, 0 } } },  // Wizard's Well
	{ 139, 1, { { H3ArtifactEffectKind::DURATION, 50, 0 } } },  // Ring of the Magi
	{ 140, 4, { { H3ArtifactEffectKind::INCOME, 4, 4 }, { H3ArtifactEffectKind::INCOME, 4, 5 }, { H3ArtifactEffectKind::INCOME, 4, 1 }, { H3ArtifactEffectKind::INCOME, 4, 3 } } },  // Cornucopia
};

}
