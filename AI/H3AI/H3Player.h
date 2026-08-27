/*
 * H3Player.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3Constants.h"

#include "../../lib/ResourceSet.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/int3.h"

#include <array>
#include <map>

VCMI_LIB_NAMESPACE_BEGIN
class CCallback;
class CGTownInstance;
class CGHeroInstance;
VCMI_LIB_NAMESPACE_END

class CCallback;

namespace H3AI
{

/// SS 2 / SS 4A - type_AI_player (0x692950, stride 0x98) merged with the parts of
/// playerData (0x168 bytes) that only the AI writes.  One instance per AI player.
class H3Player
{
public:
	H3Player() = default;

	void init(CCallback * callback, PlayerColor color);

	/// SS 4.16 - type_AI_player::begin_turn @ 0x4297C0, step 3 of the turn driver.
	void beginTurn();

	/// SS 4A.1 - type_AI_player::compute_wants @ 0x428740.
	/// Runs at the start of every AI turn and again after every purchase.
	void computeWants();

	/// SS 4A.5 - AI_compute_resource_supply_and_threats @ 0x429D50.
	void computeResourceSupplyAndThreats();

	/// SS 4A.2 - AI_resource_cost @ 0x526C70.
	int resourceCost(const ResourceSet & resources) const;

	/// SS 4A.3 - type_AI_player::get_total_value @ 0x42A150.
	/// Returns -1 when the purchase cannot be financed even after trading.
	int getTotalValue(int base, const ResourceSet & cost) const;

	/// SS 4A.4 / SS 4B.4 - type_AI_player::reserve_funds @ 0x42A470.
	void reserveFunds(const ResourceSet & cost, int multiplier);

	/// SS 4.11 - type_AI_player::get_attack_bonus @ 0x428710.
	/// 0.0 for an unowned target, 0.5 for a computer- or human-owned one.
	float getAttackBonus(PlayerColor targetOwner) const;

	/// SS 4A.1 - the AI's marginal value of one unit of a resource (playerData + 0x128).
	double resourceValue(GameResID resource) const { return resourceValues[resource.getNum()]; }
	double goldValue() const { return resourceValues[GameResID::GOLD]; }

	/// SS 4A.1 - playerData + 0x160, "average non-gold value": ftol(sum of the first six
	/// resource values) / 5.  Used by Lean To (SS 4.8).
	int averageResourceValue() const { return avgResourceValue; }

	/// SS 2 - playerData + 0x164, the average artifact value used for Pandora and
	/// creature-bank estimates.  SS 4G.1 identifies its producer: AI_prepare averages
	/// AI_get_value_of_artifact over artifact ids 7..143, which is what the turn driver
	/// feeds in through setAverageArtifactValue.
	int artifactValue() const { return avgArtifactValue; }

	/// SS 4G.1 - playerData + 0x164 is written by advManager::AI_prepare, not by
	/// compute_wants, so it is supplied from the turn driver where H3Context exists.
	void setAverageArtifactValue(int value) { avgArtifactValue = value; }

	/// SS 2 - type_AI_player + 0x04, the magus-hut value (SS 4.8, object 37).
	int magusHutValue() const { return magusHut; }
	/// SS 4G.3 step 3 - begin_turn's Eye-of-the-Magi sweep (0x429910) fills it.  Like the
	/// average artifact value it needs the object valuations, so the turn driver supplies
	/// it from where H3Context exists.
	void setMagusHutValue(int value) { magusHut = value; }
	/// SS 4.3 - AI_player::reset_magus_hut_value @ 0x429AB0.
	void resetMagusHutValue() { magusHut = 0; }

	/// SS 4.14 - AI_update_grail_guess (0x4BAE50 -> 0x52C9B0): the Grail dig site, once
	/// enough obelisks have been visited to pin it to a single tile.  Invalid until then.
	int3 grailDigSite() const { return grailSite; }

	int supply(GameResID resource) const { return resourceSupply[resource.getNum()]; }
	int demand(GameResID resource) const { return resourceDemand[resource.getNum()]; }
	int reserved(GameResID resource) const { return reservedFunds[resource.getNum()]; }

	/// SS 4A.5 - the "creature flagged as undesirable" byte array.
	bool creatureFlagged(const CreatureID & creature) const;

	/// SS 4B.4a - playerData::AnyHeroHasArtifact(pd, 0x81) @ 0x4BACB0.  Artifact 0x81 is
	/// 129 = Angelic Alliance; the army planner takes it as its `angelicAlliance` flag,
	/// which is what suppresses the alignment-morale penalty.
	bool anyHeroHasArtifact(const ArtifactID & artifact) const;

	/// SS 4.10 step 1 - reserved_funds[i] -= income[i], clamped at 0.
	void decayReservedFunds();

	ResourceSet dailyIncome() const;

	PlayerColor getColor() const { return player; }

private:
	CCallback * cb = nullptr;
	PlayerColor player;

	std::array<int, GameConstants::RESOURCE_QUANTITY> resourceSupply = {};
	std::array<int, GameConstants::RESOURCE_QUANTITY> resourceDemand = {};
	std::array<int, GameConstants::RESOURCE_QUANTITY> reservedFunds = {};
	std::array<double, GameConstants::RESOURCE_QUANTITY> resourceValues = {};

	int avgResourceValue = 0;
	int avgArtifactValue = 0;
	int magusHut = 0;
	int3 grailSite = int3(-1, -1, -1);

	std::map<CreatureID, bool> creatureThreatFlags;

	/// SS 4A.3 - AI_plan_trades @ 0x42A2B0 / AI_do_trades @ 0x42A580.
	bool planTrades(const ResourceSet & cost) const;
};

}
