/*
 * BattleTools.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */

#include "StdInc.h"
#include "BattleTools.h"
#include "ToolContext.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/callback/CCallback.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CBattleInfoEssentials.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/Unit.h"

#ifdef ENABLE_MCP_SERVER

namespace
{
	/// After a battle action is acknowledged, the interesting part is what comes next: either
	/// another unit's turn or the battle's end. Awaiting these keeps the whole exchange in one
	/// tool response instead of forcing a wait_for_event round trip per action.
	const std::vector<std::string> afterUnitAction = {"battleUnitActive", "battleResult", "battleEnded", "battleCancelled"};
	const std::vector<std::string> afterBattleEnd = {"battleResult", "battleEnded", "battleCancelled"};

	std::shared_ptr<CPlayerBattleCallback> activeBattle(CCallback & cb, BattleID & outId)
	{
		auto battles = cb.getActiveBattles();
		if(battles.empty())
			throw std::runtime_error("No active battle");
		outId = battles.begin()->first;
		return battles.begin()->second;
	}

	const battle::Unit * requireUnit(CPlayerBattleCallback & battleCB, int unitId)
	{
		auto * unit = battleCB.battleGetUnitByID(static_cast<uint32_t>(unitId));
		if(!unit)
			throw std::runtime_error("No battle unit with id " + std::to_string(unitId));
		return unit;
	}

	/// Regular unit actions go through battleMakeUnitAction, but during the tactics phase the
	/// same move/action must be sent via battleMakeTacticAction instead - mirrors
	/// BattleInterface::sendCommand's tacticsMode branch.
	void sendUnitAction(CCallback & cb, CPlayerBattleCallback & battleCB, const BattleID & battleId, const BattleAction & action)
	{
		if(battleCB.battleGetTacticsSide() == battleCB.battleGetMySide())
			cb.battleMakeTacticAction(battleId, action);
		else
			cb.battleMakeUnitAction(battleId, action);
	}

	mcp::json handleBattleMove(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();
		auto hex = params["hex"].get<int>();

		return mcptool::actionTool([unitId, hex]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);
			sendUnitAction(cb, *battleCB, battleId, BattleAction::makeMove(unit, BattleHex(static_cast<si16>(hex))));
		}, afterUnitAction);
	}

	mcp::json handleBattleAttack(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();
		auto targetUnitId = params["targetUnitId"].get<int>();
		auto attackFromHex = params["attackFromHex"].get<int>();
		bool returnAfterAttack = params.contains("returnAfterAttack") ? params["returnAfterAttack"].get<bool>() : true;

		return mcptool::actionTool([unitId, targetUnitId, attackFromHex, returnAfterAttack]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);
			auto * target = requireUnit(*battleCB, targetUnitId);
			auto action = BattleAction::makeMeleeAttack(unit, target, BattleHex(static_cast<si16>(attackFromHex)), returnAfterAttack);
			sendUnitAction(cb, *battleCB, battleId, action);
		}, afterUnitAction);
	}

	mcp::json handleBattleShoot(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();
		auto targetUnitId = params["targetUnitId"].get<int>();

		return mcptool::actionTool([unitId, targetUnitId]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);
			auto * target = requireUnit(*battleCB, targetUnitId);
			sendUnitAction(cb, *battleCB, battleId, BattleAction::makeShotAttack(unit, target));
		}, afterUnitAction);
	}

	mcp::json handleBattleWait(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();

		return mcptool::actionTool([unitId]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);
			sendUnitAction(cb, *battleCB, battleId, BattleAction::makeWait(unit));
		}, afterUnitAction);
	}

	mcp::json handleBattleDefend(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();

		return mcptool::actionTool([unitId]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);
			sendUnitAction(cb, *battleCB, battleId, BattleAction::makeDefend(unit));
		}, afterUnitAction);
	}

	mcp::json handleBattleHeal(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();
		auto targetUnitId = params["targetUnitId"].get<int>();

		return mcptool::actionTool([unitId, targetUnitId]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);
			auto * target = requireUnit(*battleCB, targetUnitId);
			sendUnitAction(cb, *battleCB, battleId, BattleAction::makeHeal(unit, target));
		}, afterUnitAction);
	}

	mcp::json handleBattleCatapult(const mcp::json & params, const std::string &)
	{
		auto unitId = params["unitId"].get<int>();
		auto hex = params["hex"].get<int>();

		return mcptool::actionTool([unitId, hex]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			auto * unit = requireUnit(*battleCB, unitId);

			BattleAction ba;
			ba.side = unit->unitSide();
			ba.actionType = EActionType::CATAPULT;
			ba.stackNumber = unit->unitId();
			ba.aimToHex(BattleHex(static_cast<si16>(hex)));
			cb.battleMakeUnitAction(battleId, ba);
		}, afterUnitAction);
	}

	mcp::json handleBattleCastSpell(const mcp::json & params, const std::string &)
	{
		auto spellId = params["spellId"].get<int>();
		bool hasTargetUnit = params.contains("targetUnitId");
		bool hasTargetHex = params.contains("hex");
		int targetUnitId = hasTargetUnit ? params["targetUnitId"].get<int>() : -1;
		int hex = hasTargetHex ? params["hex"].get<int>() : -1;

		return mcptool::actionTool([spellId, hasTargetUnit, hasTargetHex, targetUnitId, hex]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);

			BattleAction ba;
			ba.actionType = EActionType::HERO_SPELL;
			ba.spell = SpellID(spellId);
			ba.stackNumber = -1;
			ba.side = battleCB->battleGetMySide();

			if(hasTargetUnit)
				ba.aimToUnit(requireUnit(*battleCB, targetUnitId));
			else if(hasTargetHex)
				ba.aimToHex(BattleHex(static_cast<si16>(hex)));
			else
				ba.aimToHex(BattleHex::INVALID);

			cb.battleMakeSpellAction(battleId, ba);
		}, afterUnitAction);
	}

	mcp::json handleBattleRetreat(const mcp::json &, const std::string &)
	{
		return mcptool::actionTool([]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			cb.battleMakeUnitAction(battleId, BattleAction::makeRetreat(battleCB->battleGetMySide()));
		}, afterBattleEnd);
	}

	mcp::json handleBattleSurrender(const mcp::json &, const std::string &)
	{
		return mcptool::actionTool([]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			cb.battleMakeUnitAction(battleId, BattleAction::makeSurrender(battleCB->battleGetMySide()));
		}, afterBattleEnd);
	}

	mcp::json handleBattleEndTactics(const mcp::json &, const std::string &)
	{
		return mcptool::actionTool([]()
		{
			auto & cb = mcptool::activeCallback();
			BattleID battleId = BattleID::NONE;
			auto battleCB = activeBattle(cb, battleId);
			cb.battleMakeTacticAction(battleId, BattleAction::makeEndOFTacticPhase(battleCB->battleGetMySide()));
		}, afterUnitAction);
	}
}

void registerBattleTools(mcp::server * srv)
{
	srv->register_tool(
		mcp::tool_builder("battle_move")
			.with_description("Move a battle unit to a hex (also used for tactics-phase repositioning)")
			.with_number_param("unitId", "Acting unit ID", true)
			.with_number_param("hex", "Destination hex index", true)
			.build(),
		handleBattleMove
	);

	srv->register_tool(
		mcp::tool_builder("battle_attack")
			.with_description("Melee-attack another unit; moves first if not adjacent")
			.with_number_param("unitId", "Attacking unit ID", true)
			.with_number_param("targetUnitId", "Unit ID being attacked", true)
			.with_number_param("attackFromHex", "Hex the attacker should attack from", true)
			.with_boolean_param("returnAfterAttack", "Return to origin hex after attacking, if unit has that ability (default true)", false)
			.build(),
		handleBattleAttack
	);

	srv->register_tool(
		mcp::tool_builder("battle_shoot")
			.with_description("Shoot at another unit with a ranged attacker")
			.with_number_param("unitId", "Shooting unit ID", true)
			.with_number_param("targetUnitId", "Unit ID being shot at", true)
			.build(),
		handleBattleShoot
	);

	srv->register_tool(
		mcp::tool_builder("battle_wait")
			.with_description("Have a unit wait, acting later this round")
			.with_number_param("unitId", "Unit ID", true)
			.build(),
		handleBattleWait
	);

	srv->register_tool(
		mcp::tool_builder("battle_defend")
			.with_description("Have a unit defend")
			.with_number_param("unitId", "Unit ID", true)
			.build(),
		handleBattleDefend
	);

	srv->register_tool(
		mcp::tool_builder("battle_heal")
			.with_description("Heal/repair another unit (First Aid Tent)")
			.with_number_param("unitId", "Healer unit ID", true)
			.with_number_param("targetUnitId", "Unit ID being healed", true)
			.build(),
		handleBattleHeal
	);

	srv->register_tool(
		mcp::tool_builder("battle_catapult")
			.with_description("Fire the catapult at a wall/gate hex during a siege")
			.with_number_param("unitId", "Catapult unit ID (must be the active unit)", true)
			.with_number_param("hex", "Target wall hex", true)
			.build(),
		handleBattleCatapult
	);

	srv->register_tool(
		mcp::tool_builder("battle_cast_spell")
			.with_description("Cast a combat spell with the hero (creature ability casts are not yet supported)")
			.with_number_param("spellId", "Spell ID", true)
			.with_number_param("targetUnitId", "Target unit ID, for unit-targeted spells", false)
			.with_number_param("hex", "Target hex, for area/hex-targeted spells", false)
			.build(),
		handleBattleCastSpell
	);

	srv->register_tool(
		mcp::tool_builder("battle_retreat")
			.with_description("Retreat from the current battle")
			.build(),
		handleBattleRetreat
	);

	srv->register_tool(
		mcp::tool_builder("battle_surrender")
			.with_description("Surrender the current battle (costs gold)")
			.build(),
		handleBattleSurrender
	);

	srv->register_tool(
		mcp::tool_builder("battle_end_tactics")
			.with_description("End the tactics phase")
			.build(),
		handleBattleEndTactics
	);
}

#endif
