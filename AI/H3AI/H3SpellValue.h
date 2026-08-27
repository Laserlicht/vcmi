/*
 * H3SpellValue.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3SpellData.h"

#include "../../lib/constants/EntityIdentifiers.h"

#include <vector>

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
VCMI_LIB_NAMESPACE_END

namespace H3AI
{

/// SS 4.9b - type_spellvalue, constructed at 0x526D40.
///
/// One class prices every spell in the game.  Shrines, Pandora spells, the Mage Guild
/// half of AI_town_visit_value, the Tomes and Orbs, Spell Scrolls and the four magic
/// schools all reduce to calls on this object, and hero::AI_update_valuations builds
/// hero + 0x47E / +0x482 / +0x486 / +0x48A / +0x48E out of five counterfactual probes
/// of it.
///
/// spellPower, duration and mana are public and non-const on purpose: the probes mutate
/// them and re-run bestSpellValue, exactly as the original does.
class H3SpellValue
{
public:
	explicit H3SpellValue(const CGHeroInstance * hero);

	/// The original's gate: the constructor leaves spellPower at 0 when the hero has no
	/// spell book or carries the Orb of Inhibition, and every caller treats that as
	/// "spells are worth nothing to this hero".
	bool valid() const { return spellPower > 0; }

	/// SS 4.9b - type_spellvalue::value_of_spell @ 0x5273D0.
	int valueOfSpell(const SpellID & spell) const;

	/// SS 4.9b - type_spellvalue::get_best_spell_value @ 0x5275B0.
	int bestSpellValue(unsigned categoryMask) const;

	int spellPower = 0;   ///< this + 0x08, clamp(hero spell power, 1, 99)
	int duration = 0;     ///< this + 0x0C, spellPower + the duration bonus
	int mana = 0;         ///< this + 0x10, the hero's mana limit

private:
	/// SS 4.9b - value_of_buff @ 0x5270E0, the shared piecewise-linear/saturating curve.
	int valueOfBuff(const SpellID & spell, int schoolLevel, int casts, int64_t reference) const;

	const CGHeroInstance * hero = nullptr;
	int64_t armyValue = 0;   ///< this + 0x04

	/// SS 4.9b - the 12-byte records the constructor pushes and then sorts.  The sort
	/// order matters: the mass-effect arm takes "the first N stacks" off this list.
	struct Stack
	{
		int creature = 0;
		int64_t totalValue = 0;
		int count = 0;
	};

	std::vector<Stack> stacks;
};

/// SS 4.9b - AI_get_spell_value @ 0x527640.  What a Shrine, a Pandora spell or a Spell
/// Scroll is worth: the spell's own value, minus the best the hero already has in the
/// same competing group, or the token value 1 when it is no improvement.
int aiGetSpellValue(const CGHeroInstance * hero, const SpellID & spell);

}
