/*
 * H3VictoryConditions.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "H3VictoryConditions.h"

#include "../../lib/callback/CCallback.h"
#include "../../lib/mapObjects/CGObjectInstance.h"
#include "../../lib/mapping/CMapHeader.h"

namespace H3AI
{

namespace
{
/// Map one VCMI EventCondition onto the original H3 special-victory record.
/// SS 4C.2 lists the ten conditions and what each of them stores; VCMI keeps the same
/// information in a TriggeredEvent, so the translation is one-to-one apart from the
/// two H3 conditions VCMI expresses with the same enumerator (CONTROL).
bool translate(CCallback * cb, const EventCondition & condition, VictoryConditionInfo & out)
{
	switch(condition.condition)
	{
	case EventCondition::HAVE_ARTIFACT:
		out.condition = H3VictoryCondition::ACQUIRE_ARTIFACT;
		out.artifact = condition.objectType.as<ArtifactID>();
		return true;

	case EventCondition::TRANSPORT:
		// SS 4C.3 - "One test covers both conditions - the AI makes no distinction
		// between 'acquire' and 'transport to a location'".
		out.condition = H3VictoryCondition::TRANSPORT_ARTIFACT;
		out.artifact = condition.objectType.as<ArtifactID>();
		out.position = condition.position;
		out.targetObject = condition.objectID;
		return true;

	case EventCondition::HAVE_CREATURES:
		out.condition = H3VictoryCondition::ACCUMULATE_CREATURES;
		return true;

	case EventCondition::HAVE_RESOURCES:
		out.condition = H3VictoryCondition::ACCUMULATE_RESOURCES;
		return true;

	case EventCondition::HAVE_BUILDING:
	{
		const BuildingID building = condition.objectType.as<BuildingID>();

		out.position = condition.position;
		out.targetObject = condition.objectID;

		if(building == BuildingID::GRAIL)
		{
			out.condition = H3VictoryCondition::BUILD_GRAIL;
			return true;
		}

		// SS 4C.3, condition 3 - the record stores a hall level (building id 11 + level)
		// and a fort level (building id 7 + level) separately.  VCMI stores a single
		// building id, so it is decomposed back into the two halves here.
		out.condition = H3VictoryCondition::UPGRADE_TOWN;

		if(building >= BuildingID::VILLAGE_HALL && building <= BuildingID::CAPITOL)
			out.hallLevel = building.getNum() - BuildingID::VILLAGE_HALL;

		if(building >= BuildingID::FORT && building <= BuildingID::CASTLE)
			out.fortLevel = building.getNum() - BuildingID::FORT;

		return true;
	}

	case EventCondition::DESTROY:
	{
		out.targetObject = condition.objectID;
		out.position = condition.position;

		const CGObjectInstance * object = condition.objectID.hasValue() ? cb->getObj(condition.objectID, false) : nullptr;

		if(object != nullptr && object->ID == Obj::MONSTER)
			out.condition = H3VictoryCondition::DEFEAT_MONSTER;
		else
			out.condition = H3VictoryCondition::DEFEAT_HERO;

		return true;
	}

	case EventCondition::CONTROL:
	case EventCondition::CONTROL_CURRENT:
	{
		out.targetObject = condition.objectID;
		out.position = condition.position;

		const MapObjectID objectType = condition.objectType.as<MapObjectID>();

		// SS 4C.2 conditions 8 and 9 are the two "flag every X" variants; VCMI encodes
		// them as a generic type check with no specific object id.
		if(!condition.objectID.hasValue())
		{
			if(objectType == Obj::MINE)
				out.condition = H3VictoryCondition::FLAG_MINES;
			else
				out.condition = H3VictoryCondition::FLAG_DWELLINGS;

			return true;
		}

		out.condition = H3VictoryCondition::CAPTURE_TOWN;
		return true;
	}

	default:
		// STANDARD_WIN, DAYS_PASSED, IS_HUMAN, DAYS_WITHOUT_TOWN, CONST_VALUE:
		// SS 4C.4 - "Nothing else in the adventure or battle AI consults scenario
		// metadata.  In particular the AI never reads the loss condition, never reads
		// the turn limit, and never reads the campaign chain."
		return false;
	}
}
}

VictoryConditionInfo getVictoryConditionInfo(CCallback * cb)
{
	VictoryConditionInfo out;

	const CMapHeader * header = cb->getMapHeader();

	if(header == nullptr)
		return out;

	for(const TriggeredEvent & event : header->triggeredEvents)
	{
		if(event.effect.type != EventEffect::VICTORY)
			continue;

		bool found = false;

		event.trigger.morph([&](const EventCondition & condition) -> EventExpression::Variant
		{
			if(!found && translate(cb, condition, out))
				found = true;

			return condition;
		});

		if(found)
			return out;
	}

	return out;
}

}
