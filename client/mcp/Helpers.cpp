/*
 * Helpers.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Helpers.h"

#ifdef ENABLE_MCP_SERVER
#include <mcp_server.h>
#endif

#include "../../lib/json/JsonNode.h"
#include "../../lib/json/JsonUtils.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/artifact/ArtSlotInfo.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGDwelling.h"

PlayerColor parsePlayerColor(const std::string & name)
{
	for(int i = 0; i < PlayerColor::PLAYER_LIMIT_I; i++)
	{
		auto c = PlayerColor(i);
		if(c.toString() == name)
			return c;
	}
	return PlayerColor::NEUTRAL;
}

JsonNode getFullGameConfig()
{
	JsonNode result;
	try
	{
		result = JsonUtils::assembleFromFiles("config/gameConfig.json");
		if(!result.isStruct())
			return result;
		std::vector<std::string> keys;
		for(auto & kv : result.Struct())
			keys.push_back(kv.first);
		for(auto & name : keys)
		{
			auto & value = result[name];
			if(name == "settings" || !value.isVector())
				continue;
			JsonNode merged;
			for(auto & entry : value.Vector())
			{
				JsonNode fileData = JsonUtils::assembleFromFiles(entry.String());
				JsonUtils::mergeCopy(merged, fileData);
			}
			result[name] = merged;
		}
	}
	catch(const std::exception & e)
	{
		logGlobal->error("Failed to assemble game config: %s", e.what());
	}
	return result;
}

JsonNode heroToJson(const CGHeroInstance * h)
{
	JsonNode entry;
	entry["name"] = JsonNode(h->getNameTranslated());
	entry["id"] = JsonNode(h->id.getNum());
	entry["typeId"] = JsonNode(h->getHeroTypeID().getNum());
	entry["level"] = JsonNode(h->level);
	entry["experience"] = JsonNode(h->exp);
	entry["mana"] = JsonNode(h->mana);
	entry["maxMana"] = JsonNode(h->manaLimit());
	entry["attack"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::ATTACK));
	entry["defense"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::DEFENSE));
	entry["spellPower"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::SPELL_POWER));
	entry["knowledge"] = JsonNode(h->getPrimSkillLevel(PrimarySkill::KNOWLEDGE));
	entry["movement"] = JsonNode(h->movementPointsRemaining());
	entry["maxMovement"] = JsonNode(h->movementPointsLimit());
	JsonNode army;
	for(auto & [slot, stack] : h->stacks)
	{
		JsonNode stackEntry;
		stackEntry["slot"] = JsonNode(slot.getNum());
		stackEntry["id"] = JsonNode(stack->getId().getNum());
		stackEntry["count"] = JsonNode(stack->getCount());
		auto creature = stack->getCreature();
		if(creature)
		{
			stackEntry["name"] = JsonNode(creature->getNameSingularTranslated());
			stackEntry["level"] = JsonNode(creature->getLevel());
		}
		army.Vector().push_back(stackEntry);
	}
	entry["army"] = army;
	return entry;
}

JsonNode townToJson(const CGTownInstance * t)
{
	JsonNode entry;
	entry["name"] = JsonNode(t->getNameTranslated());
	entry["id"] = JsonNode(t->id.getNum());
	entry["faction"] = JsonNode(t->getFactionID());
	entry["hasFort"] = JsonNode(t->hasFort());
	JsonNode built;
	for(auto & bid : t->getBuildings())
		built.Vector().push_back(JsonNode(bid.getNum()));
	entry["built"] = built;
	return entry;
}

JsonNode artifactToJson(const Artifact * a)
{
	JsonNode entry;
	entry["id"] = JsonNode(a->getId().getNum());
	entry["name"] = JsonNode(a->getNameTranslated());
	entry["price"] = JsonNode(static_cast<int>(a->getPrice()));
	entry["isBig"] = JsonNode(a->isBig());
	entry["isTradable"] = JsonNode(a->isTradable());
	auto wm = a->getWarMachine();
	entry["warMachine"] = JsonNode(wm.getNum());
	auto * ca = dynamic_cast<const CArtifact *>(a);
	if(ca)
	{
		entry["class"] = JsonNode(ca->getArtClassSerial());
		entry["isCombined"] = JsonNode(ca->isCombined());
		entry["isScroll"] = JsonNode(ca->isScroll());
	}
	return entry;
}

JsonNode spellToJson(const spells::Spell * s)
{
	JsonNode entry;
	entry["id"] = JsonNode(s->getId().getNum());
	entry["name"] = JsonNode(s->getNameTranslated());
	entry["level"] = JsonNode(s->getLevel());
	entry["isAdventure"] = JsonNode(s->isAdventure());
	entry["isCombat"] = JsonNode(s->isCombat());
	entry["isPositive"] = JsonNode(s->isPositive());
	entry["isNegative"] = JsonNode(s->isNegative());
	entry["isDamage"] = JsonNode(s->isDamage());
	entry["isOffensive"] = JsonNode(s->isOffensive());
	entry["basePower"] = JsonNode(s->getBasePower());
	JsonNode costs;
	for(int lvl = 0; lvl < 3; lvl++)
		costs.Vector().push_back(JsonNode(s->getCost(lvl)));
	entry["cost"] = costs;
	auto * cs = dynamic_cast<const CSpell *>(s);
	if(cs)
	{
		JsonNode schoolsJson;
		for(auto & school : cs->schools)
			schoolsJson.Vector().push_back(JsonNode(static_cast<int>(school)));
		entry["schools"] = schoolsJson;
	}
	return entry;
}

JsonNode skillToJson(const Skill * s)
{
	JsonNode entry;
	entry["id"] = JsonNode(s->getId().getNum());
	entry["name"] = JsonNode(s->getNameTranslated());
	JsonNode descriptions;
	for(int lvl = 0; lvl < 3; lvl++)
		descriptions.Vector().push_back(JsonNode(s->getDescriptionTranslated(lvl)));
	entry["descriptions"] = descriptions;
	auto * cs = dynamic_cast<const CSkill *>(s);
	if(cs)
	{
		entry["isWisdom"] = JsonNode(cs->isWisdom());
		entry["isSpellSchool"] = JsonNode(cs->isSpellSchool());
		entry["gainChanceMight"] = JsonNode(cs->gainChance[0]);
		entry["gainChanceMagic"] = JsonNode(cs->gainChance[1]);
	}
	return entry;
}

JsonNode artifactInstanceToJson(const CArtifactInstance * ai)
{
	JsonNode entry;
	entry["instanceId"] = JsonNode(ai->getId().getNum());
	auto type = ai->getType();
	if(type)
	{
		entry["typeId"] = JsonNode(type->getId().getNum());
		entry["name"] = JsonNode(type->getNameTranslated());
		entry["price"] = JsonNode(static_cast<int>(type->getPrice()));
		entry["class"] = JsonNode(type->getArtClassSerial());
		entry["isCombined"] = JsonNode(type->isCombined());
		entry["isBig"] = JsonNode(type->isBig());
	}
	return entry;
}

JsonNode heroDetailsToJson(const CGHeroInstance * h)
{
	JsonNode entry = heroToJson(h);

	JsonNode spells;
	for(auto & sid : h->getSpellsInSpellbook())
	{
		auto * spell = LIBRARY->spells()->getById(sid);
		if(spell)
			spells.Vector().push_back(spellToJson(spell));
	}
	entry["spells"] = spells;

	JsonNode wornArts;
	for(auto & [pos, slotInfo] : h->artifactsWorn)
	{
		auto * ai = slotInfo.getArt();
		if(!ai)
			continue;
		JsonNode ae = artifactInstanceToJson(ai);
		ae["slot"] = JsonNode(pos.getNum());
		wornArts.Vector().push_back(ae);
	}
	entry["artifactsWorn"] = wornArts;

	JsonNode backpackArts;
	for(auto & slotInfo : h->artifactsInBackpack)
	{
		auto * ai = slotInfo.getArt();
		if(!ai)
			continue;
		backpackArts.Vector().push_back(artifactInstanceToJson(ai));
	}
	entry["artifactsInBackpack"] = backpackArts;

	JsonNode secSkills;
	for(auto & [skill, level] : h->secSkills)
	{
		JsonNode se;
		se["id"] = JsonNode(skill.getNum());
		se["level"] = JsonNode(level);
		auto * sd = skill.toSkill();
		if(sd)
			se["name"] = JsonNode(sd->getNameTranslated());
		secSkills.Vector().push_back(se);
	}
	entry["secondarySkills"] = secSkills;

	entry["hasSpellbook"] = JsonNode(h->hasSpellbook());
	entry["biography"] = JsonNode(h->getBiographyTranslated());
	entry["gender"] = JsonNode(static_cast<int>(h->gender));
	auto * hc = h->getHeroClass();
	if(hc)
	{
		entry["heroClass"] = JsonNode(hc->getNameTranslated());
		entry["heroClassId"] = JsonNode(hc->getId().getNum());
		entry["affinity"] = JsonNode(static_cast<int>(hc->affinity));
	}

	return entry;
}

JsonNode townDetailsToJson(const CGTownInstance * t)
{
	JsonNode entry = townToJson(t);

	auto * townType = t->getTown();
	if(townType)
	{
		entry["hallLevel"] = JsonNode(t->hallLevel());
		entry["fortLevel"] = JsonNode(static_cast<int>(t->fortLevel()));
		entry["mageGuildLevel"] = JsonNode(t->mageGuildLevel());
	}

	JsonNode guildSpells;
	for(int level = 0; level < static_cast<int>(t->spells.size()); level++)
	{
		for(auto & sid : t->spells[level])
		{
			JsonNode se;
			se["id"] = JsonNode(sid.getNum());
			auto * spell = LIBRARY->spells()->getById(sid);
			if(spell)
			{
				se["name"] = JsonNode(spell->getNameTranslated());
				se["level"] = JsonNode(spell->getLevel());
			}
			se["guildLevel"] = JsonNode(level + 1);
			guildSpells.Vector().push_back(se);
		}
	}
	entry["mageGuildSpells"] = guildSpells;

	JsonNode availCreatures;
	for(size_t level = 0; level < t->creatures.size(); level++)
	{
		auto & [available, types] = t->creatures[level];
		if(available > 0 || !types.empty())
		{
			JsonNode clevel;
			clevel["level"] = JsonNode(static_cast<int>(level));
			clevel["available"] = JsonNode(static_cast<int>(available));
			JsonNode typesJson;
			for(auto & cid : types)
			{
				JsonNode ce;
				ce["id"] = JsonNode(cid.getNum());
				auto * cre = cid.toCreature();
				if(cre)
					ce["name"] = JsonNode(cre->getNameSingularTranslated());
				typesJson.Vector().push_back(ce);
			}
			clevel["types"] = typesJson;
			availCreatures.Vector().push_back(clevel);
		}
	}
	entry["creatures"] = availCreatures;

	auto * visiting = t->getVisitingHero();
	if(visiting)
		entry["visitingHero"] = JsonNode(visiting->id.getNum());

	auto * garrison = t->getGarrisonHero();
	if(garrison)
		entry["garrisonHero"] = JsonNode(garrison->id.getNum());

	return entry;
}

JsonNode battleUnitToJson(const battle::Unit * u)
{
	JsonNode entry;
	entry["id"] = JsonNode(static_cast<int>(u->unitId()));
	entry["side"] = JsonNode(static_cast<int>(u->unitSide()));
	entry["owner"] = JsonNode(u->unitOwner().toString());
	entry["count"] = JsonNode(u->getCount());
	entry["position"] = JsonNode(u->getPosition().toInt());
	entry["alive"] = JsonNode(u->alive());
	entry["doubleWide"] = JsonNode(u->doubleWide());
	entry["isTurret"] = JsonNode(u->isTurret());
	entry["isCatapult"] = JsonNode(u->isCatapult());
	entry["isShooter"] = JsonNode(u->isShooter());
	entry["clone"] = JsonNode(u->isClone());
	entry["summoned"] = JsonNode(u->isSummoned());
	entry["availableHealth"] = JsonNode(static_cast<int>(u->getAvailableHealth()));
	entry["totalHealth"] = JsonNode(static_cast<int>(u->getTotalHealth()));
	entry["firstHPleft"] = JsonNode(u->getFirstHPleft());
	auto * creature = u->unitType();
	if(creature)
	{
		entry["creatureId"] = JsonNode(creature->getId().getNum());
		entry["creatureName"] = JsonNode(creature->getNameSingularTranslated());
	}
	return entry;
}
