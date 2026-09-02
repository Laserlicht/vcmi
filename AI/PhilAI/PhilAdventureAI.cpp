/*
 * PhilAdventureAI.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhilAdventureAI.h"

#include "PhilBattleAI.h"
#include "PhilCombatSim.h"
#include "PhilValuation.h"

#include "../../lib/CCreatureHandler.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/StartInfo.h"
#include "../../lib/UnlockGuard.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleStateInfoForRetreat.h"
#include "../../lib/callback/CCallback.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/gameState/UpgradeInfo.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactInstance.h"
#include "../../lib/entities/artifact/CArtifactSet.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapObjects/army/CCreatureSet.h"
#include "../../lib/pathfinder/CGPathNode.h"

#include <algorithm>
#include <shared_mutex>
#include <thread>

namespace PhilAI
{

/// H3: const_archery_value[] - the per-rank Archery table, applied to the shooter subtotal.
/// PHILAI-GAP: the table lives in the binary's data section. These preserve the documented
/// shape (rising with mastery) but are not recovered figures.
static constexpr int ARCHERY_VALUE[4] = { 0, 10, 25, 50 };

/// H3: const_estate_value[] - the small fixed per-rank Estates table.
/// PHILAI-GAP: same data-section situation as above.
static constexpr int ESTATE_VALUE[4] = { 0, 125, 250, 500 };

/// Guard with no counterpart in the original: VCMI can refuse a move request without spending
/// anything, so a hero that keeps asking and getting nowhere is retired from the queue.
static constexpr int MAX_MOVE_ATTEMPTS = 16;

/// Same guard on the per-move step walk: a path can never legitimately need more steps than a
/// hero has movement points, and this keeps a refused step from spinning.
static constexpr int MAX_PATH_STEPS = 256;

// ---------------------------------------------------------------------------
// II.11 - growing a hero
// ---------------------------------------------------------------------------

int getSkillValue(const CGHeroInstance * hero, SecondarySkill skill, const PhilEconomy & economy)
{
	// H3: get_skill_value philai.cpp:3469 - recovered near-verbatim from the decompile.
	if(!hero)
		return 0;

	const int level = hero->getSecSkillLevel(skill);

	// An expert skill has nothing left to gain, and a hero with a full slate cannot take a new
	// one at all.
	if(level == 3)
		return 0;
	if(level == 0 && static_cast<int>(hero->secSkills.size()) >= 8)
		return 0;

	// The shared base: 1,000 plus the AI's assessed value of the hero's whole army, with a
	// separate running subtotal (starting at 500) for stacks carrying the shooter trait.
	int base = 1000;
	int shooterBase = 500;

	for(const auto & slot : hero->Slots())
	{
		const CCreature * c = hero->getCreature(slot.first);
		if(!c)
			continue;

		const int value = hero->getStackCount(slot.first) * c->getAIValue();
		base += value;
		if(c->hasBonusOfType(BonusType::SHOOTER))
			shooterBase += value;
	}

	const int power = hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER);
	const int knowledge = hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
	const int attack = hero->getPrimSkillLevel(PrimarySkill::ATTACK);
	const bool hasWisdom = hero->getSecSkillLevel(SecondarySkill::WISDOM) > 0;

	switch(skill.toEnum())
	{
		// Diplomacy's divisor of 100 leaves it essentially unable to win the ranking pass
		// against any other skill - the provable reason the AI never invests in it.
		case SecondarySkill::DIPLOMACY:     return base / 100;

		case SecondarySkill::SCOUTING:      return base / 20;
		case SecondarySkill::NECROMANCY:    return base / 20;
		case SecondarySkill::LEARNING:      return base / 20;
		case SecondarySkill::LOGISTICS:     return base / 10;
		case SecondarySkill::LEADERSHIP:    return base / 50;
		case SecondarySkill::LUCK:          return base / 50;
		case SecondarySkill::TACTICS:       return base / 50;
		case SecondarySkill::NAVIGATION:    return base / 2;
		case SecondarySkill::RESISTANCE:    return base / 40;
		case SecondarySkill::OFFENCE:       return base * 7 / 100;
		case SecondarySkill::ARMORER:       return base * 7 / 100;
		case SecondarySkill::ARTILLERY:     return base / 8;

		// Archery scales with the ranged troop count, not the whole army.
		case SecondarySkill::ARCHERY:
			return shooterBase * ARCHERY_VALUE[std::clamp(level, 0, 3)] / 100;

		// Wisdom is itself often the gate for the skills below it.
		case SecondarySkill::WISDOM:
			return power * economy.valueOfPower / 2;

		case SecondarySkill::MYSTICISM:
			return economy.valueOfKnowledge / 10;

		// A binary "zero unless a prerequisite is already met" gate, sharply different from
		// the smoothly-scaled majority of the list.
		case SecondarySkill::EAGLE_EYE:
			return hasWisdom ? power / 5 : 0;

		case SecondarySkill::SCHOLAR:
			return hasWisdom ? power * economy.valueOfPower / 10 : 0;

		// The four magic schools: the marginal spell-value simulation on the normal path, and
		// a flat fallback that requires Wisdom for any nonzero value at all. Fire specifically
		// is valued at flat zero in that fallback, which looks more like an oversight than a
		// deliberate ranking.
		case SecondarySkill::FIRE_MAGIC:    return hasWisdom ? power * 10 : 0;
		case SecondarySkill::AIR_MAGIC:     return hasWisdom ? power * 10 : 25;
		case SecondarySkill::WATER_MAGIC:   return hasWisdom ? power * 10 : 3;
		case SecondarySkill::EARTH_MAGIC:   return hasWisdom ? power * 10 : 30;

		// Zero unless one specific artifact is already owned.
		case SecondarySkill::BALLISTICS:
			return hero->hasArt(ArtifactID(4)) ? attack * 10 : 0;

		case SecondarySkill::FIRST_AID:
			return hero->hasArt(ArtifactID(6)) ? 250 : 0;

		case SecondarySkill::INTELLIGENCE:
			return knowledge * economy.valueOfKnowledge / 4;

		case SecondarySkill::SORCERY:
			return power * economy.valueOfPower / 20;

		// Estates gets more attractive specifically when the AI is gold-hungry.
		case SecondarySkill::ESTATES:
			return ESTATE_VALUE[std::clamp(level, 0, 3)] * economy.getResourceValue(GameResID::GOLD);

		case SecondarySkill::PATHFINDING:
			// PHILAI-GAP: Pathfinding's per-rank coefficient comes from a compiled data table
			// (DAT_001cdb90) that was not recoverable; the x4.0 scaling seen in the decompile
			// is applied to the base here.
			return base * 4 / 100;

		default:
			return 0;
	}
}

bool wantsSkill(const CGHeroInstance * hero, SecondarySkill skill, const PhilEconomy & economy, int slotBudget)
{
	// H3: wants_skill philai.cpp:3645
	// Score every secondary skill the hero does not yet hold - but only among skills that
	// hero's class could ever realistically be offered - sort them, and accept a new skill only
	// if it lands within the hero's remaining skill-slot budget.
	if(!hero)
		return false;

	const int candidateValue = getSkillValue(hero, skill, economy);
	if(candidateValue <= 0)
		return false;

	int better = 0;
	for(int i = 0; i < static_cast<int>(SecondarySkill::SKILL_SIZE); ++i)
	{
		const SecondarySkill other(i);
		if(other == skill)
			continue;
		if(hero->getSecSkillLevel(other) > 0)
			continue;

		if(getSkillValue(hero, other, economy) > candidateValue)
			++better;
	}

	return better < slotBudget;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

PhilAdventureAI::~PhilAdventureAI()
{
	if(turnThread.joinable())
		turnThread.join();
}

void PhilAdventureAI::finish()
{
	if(turnThread.joinable())
		turnThread.join();
}

void PhilAdventureAI::answerQuery(QueryID queryID, int selection)
{
	// VCMI hands out QueryID(-1) where there is nothing to answer.
	if(queryID == QueryID(-1))
		return;

	// Dialog callbacks reach us on the game-events thread, which is also the thread that
	// applies the reply - so this one request must not wait for its own round trip.
	const bool wasWaiting = cb->waitTillRealize;
	cb->waitTillRealize = false;
	cb->selectionMade(selection, queryID);
	cb->waitTillRealize = wasWaiting;
}

void PhilAdventureAI::settle()
{
	auto unlock = vstd::makeUnlockSharedGuard(CGameState::mutex);
	std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

std::string PhilAdventureAI::getBattleAIName() const
{
	// The adventure and battle halves share no valuation abstraction in the original either -
	// they are genuinely independent implementations of conceptually similar work.
	return "PhilAI";
}

void PhilAdventureAI::initGameInterface(std::shared_ptr<Environment> ENV, std::shared_ptr<CCallback> CB)
{
	env = ENV;
	cb = CB;
	human = false;
	playerID = *cb->getPlayerID();

	// The original's every action took effect the instant it was issued, and all of its greedy
	// loops re-derive from the post-action state. Waiting for each request to be realized is
	// what reproduces that; it is safe because the turn runs on its own thread.
	cb->waitTillRealize = true;

	economy = std::make_unique<PhilEconomy>(cb);
	mapScoring = std::make_unique<PhilMapScoring>(cb, *economy);

	// Difficulty is a single global byte in the original, read in at least thirty distinct
	// places rather than resolved once into a config object.
	if(const auto * startInfo = cb->getStartInfo())
		economy->setDifficulty(startInfo->difficulty);
}

void PhilAdventureAI::yourTurn(QueryID queryID)
{
	answerQuery(queryID, 0);

	// Return to the game-events thread at once so it can keep delivering callbacks - notably
	// the garrison and blocking dialogs the turn's own actions will raise.
	if(turnThread.joinable())
		turnThread.join();

	turnThread = std::thread([this]() { runTurn(); });
}

void PhilAdventureAI::runTurn()
{
	// Every request this thread sends is answered with waitTillRealize, and CClient::sendRequest
	// releases a *shared* gamestate lock around that wait - so the lock has to be held here for
	// the whole turn. Without it the release corrupts the reader count and the events thread can
	// never take its unique lock to apply anything.
	std::shared_lock gsLock(CGameState::mutex);

	// H3: philAI::DoAI philai.cpp:1261 - a fixed four-beat sequence, run identically every
	// turn on every difficulty. This is not a loop that runs until nothing changes.
	mapScoring->beginTurn();

	startTurn();

	// H3: philAI::DoAI philai.cpp:1261 - the danger grid is computed once here and read
	// unchanged by both movement passes. The board keeps changing underneath it, and the AI's
	// sense of where the map is dangerous deliberately does not refresh mid-turn.
	mapScoring->markDangerZones(getBestHero());

	moveAllHeroes();
	endTurn();
	moveAllHeroes();

	cb->endTurn();
}

// ---------------------------------------------------------------------------
// I - the shape of a turn
// ---------------------------------------------------------------------------

void PhilAdventureAI::startTurn()
{
	// H3: type_AI_player::start_turn ai_player.cpp:695
	heroMoved.clear();

	economy->calculateDemand();
	economy->calculateReserve();

	// The garrison-purchase and town-threat evaluation, run before any hero moves.
	mapScoring->checkTowns();

	// H3: AI_set_hero_bonuses philai.cpp:1725 - refresh, for every hero the player owns, how
	// much one more point of Power and one more point of Knowledge is currently worth given
	// that hero's present spellbook and army.
	int bestPower = Const::MIN_PRIMARY_POINT_VALUE;
	int bestKnowledge = Const::MIN_PRIMARY_POINT_VALUE;
	for(const auto * hero : cb->getHeroesInfo())
	{
		if(!hero->hasSpellbook())
			continue;
		bestPower = std::max(bestPower, getArmyAIValue(hero) / 100);
		bestKnowledge = std::max(bestKnowledge, getArmyAIValue(hero) / 200);
	}
	economy->valueOfPower = bestPower;
	economy->valueOfKnowledge = bestKnowledge;

	// H3: philAI::GetTurnAIVars philai.cpp:1770 - sum the AI's valuation of every artifact
	// type in the game into one reusable "typical artifact worth" figure.
	// PHILAI-GAP: the original sums AI_get_value_of_artifact over the whole artifact table and
	// divides by ~120. Approximated here from the average market price, since the per-effect
	// artifact classes are priced against hero state the sum does not have.
	economy->averageArtifactValue = economy->averageResourceValue * 20;

	// Emergency garrison buying for a threatened town reuses the ordinary purchaser with the
	// cost-efficiency discount turned off - it just buys the strongest army affordable.
	for(const auto * town : cb->getTownsInfo(true))
		if(mapScoring->getThreatCount(town) > 0)
			economy->buyCreatures(town, town, false);

	economy->purchaseBuildings(mapScoring->anyTownThreatened());
}

void PhilAdventureAI::endTurn()
{
	// H3: type_AI_player::end_turn ai_player.cpp:414
	// Production is recalculated a second time, the reserve is drawn down by what the turn
	// actually consumed, and the garrison-purchase/threat check runs again. That second check
	// is exactly why a second movement pass follows.
	economy->calculateDemand();
	mapScoring->checkTowns();

	for(const auto * town : cb->getTownsInfo(true))
		if(mapScoring->getThreatCount(town) > 0)
			economy->buyCreatures(town, town, false);

	economy->hireHeroes();
	economy->makeGifts();
}

const CGHeroInstance * PhilAdventureAI::determineHeroToMove() const
{
	// H3: DetermineHeroToMove philai.cpp:1156
	// PHILAI-GAP: the original's exact ordering criterion was not traced; heroes are taken in
	// descending summed-primary-skill order here, matching the metric used everywhere else
	// this AI compares heroes.
	const CGHeroInstance * best = nullptr;
	int bestScore = -1;

	for(const auto * hero : cb->getHeroesInfo())
	{
		if(hero->isGarrisoned())
			continue;
		if(heroMoved.count(hero))
			continue;
		if(hero->movementPointsRemaining() <= 0)
			continue;

		int score = 0;
		for(auto skill : { PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE })
			score += hero->getPrimSkillLevel(skill);

		if(score > bestScore)
		{
			bestScore = score;
			best = hero;
		}
	}

	return best;
}

void PhilAdventureAI::moveAllHeroes()
{
	// H3: move_all_heroes philai.cpp:1239 - a plain work queue carrying no strategy itself.
	// It repeatedly asks which hero should act next, moves it, and stops when none remain.
	//
	// The original's MoveHero always resolved to a real board change; here a move request can
	// be refused by the server without consuming anything, so each hero also gets a bounded
	// number of attempts before the queue gives up on it.
	std::map<const CGHeroInstance *, int> attempts;

	while(true)
	{
		const CGHeroInstance * hero = determineHeroToMove();
		if(!hero)
			return;

		// Whether this is the last hero to act decides how wide the destination search goes.
		bool anyOther = false;
		for(const auto * other : cb->getHeroesInfo())
			if(other != hero && !other->isGarrisoned() && !heroMoved.count(other) && other->movementPointsRemaining() > 0)
				anyOther = true;

		const int before = hero->movementPointsRemaining();
		const bool keepGoing = moveHero(hero, !anyOther);

		// No movement actually spent, or too many fruitless passes: this hero is done.
		if(!keepGoing || hero->movementPointsRemaining() >= before || ++attempts[hero] > MAX_MOVE_ATTEMPTS)
			heroMoved[hero] = true;
	}
}

// ---------------------------------------------------------------------------
// II.7 - on the move
// ---------------------------------------------------------------------------

bool PhilAdventureAI::moveHero(const CGHeroInstance * hero, bool isLastHero)
{
	// H3: move_hero philai.cpp:934
	// The search radius is not fixed: it escalates through several stages depending on the
	// hero's situation and how late in the turn's movement queue it is. A hero searches harder
	// the later it acts, and much more narrowly while on patrol.
	if(!hero)
		return false;

	int3 committed(-1, -1, -1);
	const auto it = committedTargets.find(hero);
	if(it != committedTargets.end())
		committed = it->second;

	int radius = Const::SEARCH_RADIUS_START;

	// Already has a committed target: widen to cover it plus a margin.
	if(committed.z >= 0)
	{
		const int distance = static_cast<int>(hero->visitablePos().dist2d(committed)) * 100;
		radius = std::max(distance + Const::SEARCH_RADIUS_COMMITTED_PAD + hero->movementPointsRemaining(),
			Const::SEARCH_RADIUS_START);
	}

	// Last hero to move this turn: jump to an effectively map-wide radius.
	if(isLastHero)
		radius = Const::SEARCH_RADIUS_LAST_HERO;

	// Every candidate costs a full cloned-army prediction to price, so the map is read once at
	// the widest radius the escalation below could reach and the steps then filter that list.
	const std::vector<HeroDestination> candidates =
		mapScoring->findAllDestinations(hero, Const::SEARCH_RADIUS_LAST_HERO);
	mapScoring->markStrategicMap(hero, candidates);

	HeroDestination destination;
	for(int attempt = 0; attempt < Const::SEARCH_RADIUS_RETRIES; ++attempt)
	{
		destination = mapScoring->chooseDestination(hero, candidates, radius, committed);
		if(destination.object)
			break;

		// Nothing found: double the radius and retry, up to five times.
		radius *= 2;
	}

	if(!destination.object)
	{
		committedTargets.erase(hero);
		return false;
	}

	committedTargets[hero] = destination.coord;
	return attemptMove(hero, destination.coord);
}

bool PhilAdventureAI::attemptMove(const CGHeroInstance * hero, const int3 & destination)
{
	// H3: AI_AttemptMove ai_player.cpp:4179
	// The computed path is walked one step at a time rather than teleported along, and target
	// selection is not strictly a once-per-turn decision: if a step is flagged for
	// reconsideration, the hero still has more than about 99 movement points left, and the
	// current goal is not locked in as critical, the destination is re-chosen mid-move.
	if(!hero || destination.z < 0)
		return false;

	for(int step = 0; step < MAX_PATH_STEPS; ++step)
	{
		if(hero->visitablePos() == destination)
			break;

		int3 next(-1, -1, -1);
		EPathfindingLayer layer = EPathfindingLayer::LAND;
		if(!mapScoring->getNextStep(hero, destination, next, layer))
			return false;

		const int before = hero->movementPointsRemaining();
		cb->moveHero(hero, next, false, layer);

		// The cached path was computed from the tile the hero has just left; recomputing it is
		// what keeps the next step adjacent to where the hero actually stands.
		mapScoring->invalidatePaths();

		// Let any dialog the step opened reach us before another action is sent.
		settle();

		// The step was refused, or produced no progress at all - stop asking.
		if(hero->movementPointsRemaining() >= before)
			return false;

		// Arrived on one of our own towns: run the full eight-step visit sequence.
		if(const auto * town = hero->getVisitedTown())
			if(town->getOwner() == playerID)
				enterTown(town, hero);

		if(hero->movementPointsRemaining() <= 0)
			return false;
	}

	return hero->movementPointsRemaining() > Const::REPATH_MOVEMENT_GATE;
}

// ---------------------------------------------------------------------------
// II.8 - towns & garrisons
// ---------------------------------------------------------------------------

const CGHeroInstance * PhilAdventureAI::getBestHero() const
{
	// H3: get_best_hero philai.cpp:636 - summed primary skills, the same metric used every
	// time this AI compares two heroes.
	const CGHeroInstance * best = nullptr;
	int bestScore = -1;

	for(const auto * hero : cb->getHeroesInfo())
	{
		int score = 0;
		for(auto skill : { PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE })
			score += hero->getPrimSkillLevel(skill);

		if(score > bestScore)
		{
			bestScore = score;
			best = hero;
		}
	}

	return best;
}

void PhilAdventureAI::considerGarrisoning(const CGTownInstance * town, const CGHeroInstance * hero)
{
	// H3: consider_garrisoning philai.cpp:687
	// Only runs when the player controls more than one hero and no enemy hero currently
	// threatens the town. If an existing garrison hero is already stronger by total primary
	// skills than the visiting one, nothing changes.
	if(!town || !hero)
		return;
	if(cb->howManyHeroes(true) <= 1)
		return;
	if(mapScoring->getThreatCount(town) > 0)
		return;

	auto skillSum = [](const CGHeroInstance * h)
	{
		int total = 0;
		for(auto skill : { PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE })
			total += h->getPrimSkillLevel(skill);
		return total;
	};

	const CGHeroInstance * garrisoned = town->getGarrisonHero();
	if(garrisoned && skillSum(garrisoned) >= skillSum(hero))
		return;

	// H3: should_garrison_town philai.cpp:662 - despite the name this is not a general strength
	// heuristic. It returns true only under one narrow scenario condition (a specific "capture
	// and hold this exact town" win/loss condition, and only when the visiting hero is not
	// already the player's single best one) and false in every other case.
	const bool narrowScenarioCondition = false; // PHILAI-GAP: no VCMI equivalent for that check
	if(!narrowScenarioCondition)
		return;
	if(hero == getBestHero())
		return;

	cb->swapGarrisonHero(town);
}

void PhilAdventureAI::buyMageGuild(const CGTownInstance * town, const CGHeroInstance * hero)
{
	// H3: type_AI_player::buy_mage_guild ai_player.cpp:1954
	// More deliberate than a flat "upgrade if affordable" rule.
	if(!town || !hero)
		return;

	// Never build a tier higher than the visiting hero's own magic level could use: the target
	// tier is capped at the hero's relevant skill level plus two.
	int heroMagicLevel = 0;
	for(auto school : { SecondarySkill::FIRE_MAGIC, SecondarySkill::AIR_MAGIC,
		SecondarySkill::WATER_MAGIC, SecondarySkill::EARTH_MAGIC })
		heroMagicLevel = std::max<int>(heroMagicLevel, hero->getSecSkillLevel(school));

	const int maxTier = std::min(5, heroMagicLevel + 2);
	const int currentTier = town->mageGuildLevel();

	if(currentTier >= maxTier)
		return;

	const int targetTier = currentTier + 1;

	// For the very first tier, if the hero already knows the two basic spells it would teach,
	// there is nothing left to gain and it is skipped outright.
	if(targetTier == 1 && hero->getSpellsInSpellbook().size() >= 2)
		return;

	// Before spending on a mid-or-higher tier, check every other town: if a different town
	// already has a Guild tier this hero could equally learn from, skip the purchase. The AI
	// does not build the same magical infrastructure twice across its empire.
	if(targetTier >= 3)
	{
		for(const auto * other : cb->getTownsInfo(true))
			if(other != town && other->mageGuildLevel() >= targetTier)
				return;
	}

	const BuildingID guild(BuildingID::MAGES_GUILD_1 + targetTier - 1);
	if(cb->canBuildStructure(town, guild) == EBuildingState::ALLOWED)
		cb->buildBuilding(town, guild);
}

void PhilAdventureAI::visitHillFort(const CGTownInstance * town, const CGHeroInstance * hero)
{
	// H3: AI_visit_hill_fort philai.cpp:2513 - loop every upgradeable stack, apply a tiered
	// discount by creature rank, and buy every affordable upgrade unconditionally, with no
	// comparison against other uses for the gold.
	if(!hero)
		return;

	// Snapshot the slots first: each upgrade is applied before the next loop iteration, and
	// that rewrites the army map we would otherwise still be walking.
	//
	// The original visits a real Hill Fort, where every upgrade on offer is by definition
	// legal. Reached from an ordinary town visit the same call has to ask what this town can
	// actually upgrade, and at what price, before requesting anything.
	std::vector<std::pair<SlotID, CreatureID>> upgrades;

	for(const auto & slot : hero->Slots())
	{
		const CCreature * c = hero->getCreature(slot.first);
		if(!c || !c->hasUpgrades())
			continue;

		UpgradeInfo info(c->getId());
		cb->fillUpgradeInfo(hero, slot.first, info);
		if(info.getAvailableUpgrades().empty())
			continue;

		const CreatureID target = info.getUpgrade();
		if(!cb->getResourceAmount().canAfford(info.getUpgradeCostsFor(target)))
			continue;

		upgrades.emplace_back(slot.first, target);
	}

	for(const auto & upgrade : upgrades)
	{
		cb->upgradeCreature(hero, upgrade.first, upgrade.second);
		settle();
	}
}

void PhilAdventureAI::enterTown(const CGTownInstance * town, const CGHeroInstance * hero)
{
	// H3: AI_enter_town philai.cpp:732
	// A full, unconditional eight-step sequence, in a fixed order, every time an AI hero
	// enters a town it controls or has just captured. The order is never re-prioritized.
	if(!town || !hero)
		return;

	// 1 - auto-build the Grail if the hero carries one and it is legal here.
	if(cb->canBuildStructure(town, BuildingID::GRAIL) == EBuildingState::ALLOWED)
		cb->buildBuilding(town, BuildingID::GRAIL);

	// 2 - consider leaving this hero behind as the town's garrison.
	considerGarrisoning(town, hero);

	// 3 - buy creatures, then Mage Guild spells.
	economy->buyCreatures(town, hero, true);
	buyMageGuild(town, hero);

	// 4 - buy a spellbook for a flat 500 gold if the hero is missing one.
	if(!hero->hasSpellbook() && cb->getResourceAmount(GameResID::GOLD) >= Const::SPELLBOOK_PRICE)
		cb->buyArtifact(hero, ArtifactID::SPELLBOOK);

	// 5 - upgrade at a Hill Fort, where the town has one.
	visitHillFort(town, hero);

	// 6 - buy buildings and artifacts.
	economy->purchaseBuildings(mapScoring->anyTownThreatened());

	// 7 - loot a garrison hero defeated in the capture itself, both ways.
	if(town->getGarrisonHero())
		friendlyHeroMeeting(hero, town->getGarrisonHero());

	// 8 - buy siege engines.
	// PHILAI-GAP: buy_siege_engine philai.cpp:594 gates on a 501-movement-point reach check
	// against the intended siege target, which this AI does not track per-town.

	arrangeArmy(hero);
}

// ---------------------------------------------------------------------------
// II.4 / II.13 - arranging forces and hero meetings
// ---------------------------------------------------------------------------

void PhilAdventureAI::arrangeArmy(const CGHeroInstance * hero)
{
	// H3: AI_arrange_army ai_player.cpp:2718
	// Sorts army slots by creature value and places ranged stacks into alternating slots so
	// they are spread across the formation rather than clustered - a formation choice that
	// limits how many shooters a single area attack can catch at once.
	// PHILAI-GAP: VCMI resolves battlefield placement from slot order server-side, and there is
	// no AI-facing callback to reorder slots without a real swap sequence. The spread-shooters
	// rule is documented here rather than silently dropped.
	(void)hero;
}

void PhilAdventureAI::friendlyHeroMeeting(const CGHeroInstance * a, const CGHeroInstance * b)
{
	// H3: AI_friendly_hero_meeting philai.cpp:811
	// Compare the two heroes' total primary-skill sums and move the weaker one's troops into
	// the stronger one's army - concentrating strength rather than splitting it evenly.
	// Regardless of that outcome, gear is reallocated between the two in both directions.
	if(!a || !b || a == b)
		return;

	auto skillSum = [](const CGHeroInstance * h)
	{
		int total = 0;
		for(auto skill : { PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE })
			total += h->getPrimSkillLevel(skill);
		return total;
	};

	const CGHeroInstance * stronger = skillSum(a) >= skillSum(b) ? a : b;
	const CGHeroInstance * weaker = stronger == a ? b : a;

	// Same reason as the Hill Fort loop: each move rewrites the army map being iterated.
	std::vector<SlotID> toMove;
	for(const auto & slot : weaker->Slots())
		toMove.push_back(slot.first);

	for(const auto & slot : toMove)
		cb->bulkMoveArmy(weaker->id, stronger->id, slot);

	// H3: AI_swap_artifacts ai_player.cpp:5967 - the same net-value check runs in both
	// directions every time, so gear genuinely migrates toward whichever hero benefits more.
	cb->bulkMoveArtifacts(weaker->id, stronger->id, true, true, true);
}

// ---------------------------------------------------------------------------
// Interface plumbing
// ---------------------------------------------------------------------------

void PhilAdventureAI::heroGotLevel(const CGHeroInstance * hero, PrimarySkill, std::vector<SecondarySkill> & skills, QueryID queryID)
{
	// H3: AI_choose_secondary_skill philai.cpp:4180
	// If both candidates are equally "known" - both already held, or both brand new - simply
	// score each and take the higher. Where one is already known and the other is new, the new
	// skill only wins if it separately passes wants_skill; otherwise the hero keeps developing
	// what it already has.
	if(skills.empty())
	{
		answerQuery(queryID, 0);
		return;
	}

	const int slotBudget = 8 - static_cast<int>(hero->secSkills.size());

	int bestIndex = 0;
	int bestValue = std::numeric_limits<int>::min();

	for(size_t i = 0; i < skills.size(); ++i)
	{
		const bool known = hero->getSecSkillLevel(skills[i]) > 0;
		int value = getSkillValue(hero, skills[i], *economy);

		if(!known && !wantsSkill(hero, skills[i], *economy, slotBudget))
			value = std::numeric_limits<int>::min() + 1;

		if(value > bestValue)
		{
			bestValue = value;
			bestIndex = static_cast<int>(i);
		}
	}

	answerQuery(queryID, bestIndex);
}

void PhilAdventureAI::commanderGotLevel(const CCommanderInstance *, std::vector<ui32> skills, QueryID queryID)
{
	// PHILAI-GAP: commanders do not exist in Restoration of Erathia, so the original has no
	// opinion here at all.
	answerQuery(queryID, skills.empty() ? 0 : CRandomGenerator::getDefault().nextInt(static_cast<int>(skills.size()) - 1));
}

void PhilAdventureAI::showBlockingDialog(const std::string &, const std::vector<Component> &, QueryID askID, const int, bool selection, bool, bool)
{
	// H3: AI_join_decision philai.cpp:4204 and AI_bribe_monsters philai.cpp:3277 both resolve
	// through the ordinary creature-purchase model rather than bespoke logic.
	// PHILAI-GAP: the dialog carries no machine-readable description of what is being offered,
	// so the offer cannot be routed back through value_of_recruiting here. Accepting is the
	// original's outcome whenever the purchaser scores the offer positively.
	answerQuery(askID, selection ? 1 : 0);
}

void PhilAdventureAI::showGarrisonDialog(const CArmedInstance *, const CGHeroInstance *, bool, QueryID queryID, const MetaString &)
{
	answerQuery(queryID, 0);
}

void PhilAdventureAI::showTeleportDialog(const CGHeroInstance *, TeleportChannelID, TTeleportExitsList, bool, QueryID askID)
{
	answerQuery(askID, 0);
}

void PhilAdventureAI::showMapObjectSelectDialog(QueryID askID, const Component &, const MetaString &, const MetaString &, const std::vector<ObjectInstanceID> &)
{
	answerQuery(askID, 0);
}

std::optional<BattleAction> PhilAdventureAI::makeSurrenderRetreatDecision(const BattleID & battleID, const BattleStateInfoForRetreat & battleState)
{
	// The adventure side reuses the identical retreat gauntlet, but with the real difficulty
	// and treasury the battle interface does not carry.
	if(!battleState.canFlee && !battleState.canSurrender)
		return std::nullopt;

	int lootValue = 0;
	if(battleState.ourHero)
	{
		for(const auto & slot : battleState.ourHero->artifactsWorn)
			if(slot.second.getArt())
				lootValue += std::max(1, static_cast<int>(slot.second.getArt()->getType()->getPrice()) / 2);
		for(const auto & art : battleState.ourHero->artifactsInBackpack)
			if(art.getArt())
				lootValue += std::max(1, static_cast<int>(art.getArt()->getType()->getPrice()) / 2);
	}

	const bool retreat = checkRetreat(
		battleState.ourHero,
		battleState.enemyHero,
		battleState.getOurStrength(),
		battleState.getEnemyStrength(),
		false,
		true,
		economy->getDifficulty(),
		cb->getResourceAmount(GameResID::GOLD),
		lootValue,
		0);

	if(!retreat)
		return std::nullopt;

	if(battleState.canFlee)
		return BattleAction::makeRetreat(battleState.ourSide);

	return BattleAction::makeSurrender(battleState.ourSide);
}

} // namespace PhilAI
