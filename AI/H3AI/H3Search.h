/*
 * H3Search.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "H3HeroState.h"

#include "../../lib/int3.h"

#include <limits>
#include <memory>
#include <vector>

VCMI_LIB_NAMESPACE_BEGIN
class CGHeroInstance;
VCMI_LIB_NAMESPACE_END

class CCallback;

namespace H3AI
{

/// SS 4D.1 / SS 4B.11 - one record of searchArray::cells (stride 0x1E).  Only the
/// four fields the AI actually reads back out of the engine search are modelled.
struct SearchCell
{
	/// cell + 0x04 bit 0 - the cell is in the tree
	bool reachable = false;
	/// cell + 0x18 - accumulated movement cost (held as unsigned 16-bit in the original)
	int cost = std::numeric_limits<int>::max();
	/// cell + 0x1A - turn count
	int turns = 0;
	/// cell + 0x0C - packed coord of the Dijkstra predecessor
	int3 predecessor = int3(-1, -1, -1);
	/// cell + 0x10 - accumulated value (SS 4B.10a)
	int64_t value = 0;
	/// set by AI_build_reachability (SS 4B.7) for cells another friendly hero covers
	bool suppressed = false;
};

/// SS 4D.1 - searchArray::compute @ 0x56B440, the engine's shared movement flood.
///
/// The report is explicit that this is *not* a priority-queue Dijkstra: the frontier is
/// a LIFO of cell records popped from the back, with a re-push on improvement, i.e. a
/// label-correcting flood.  It converges to the same distances but settles cells in a
/// different order, and the destination chooser reads that order - so the LIFO is
/// reproduced here rather than replaced with a heap.
class H3Search
{
public:
	H3Search();
	~H3Search();

	/// @param movementLimit  budget in movement points; cells costing more are not expanded
	/// @param windowRadius   when >= 0, the flood is clamped to a
	///        (2*windowRadius+1) square around @p start.  SS 4.5 runs exactly such a
	///        bounded search - an 11x11 window around the object - when it smears an
	///        object's value across the influence map.
	void compute(CCallback * cb, const CGHeroInstance * hero, const int3 & start, int movementLimit, int windowRadius = -1);

	const SearchCell & at(const int3 & tile) const;
	SearchCell & at(const int3 & tile);

	bool isInside(const int3 & tile) const;

	/// searchArray + 0x5C / +0x60 - the list of cells the flood actually reached.
	const std::vector<int3> & reachedCells() const { return reached; }

	int3 getStart() const { return origin; }

	/// SS 4B.11 - searchArray::build_path @ 0x56A0D0 followed by hero::AI_build_step_list
	/// @ 0x430610: the route from the search origin to @p destination, in travel order.
	std::vector<int3> buildPath(const int3 & destination) const;

	void clear();

private:
	size_t index(const int3 & tile) const;

	int3 mapSize = int3(0, 0, 0);
	int3 origin = int3(-1, -1, -1);
	std::vector<SearchCell> cells;
	std::vector<int3> reached;
	SearchCell unreachable;
};

/// SS 4B.7 - hero::AI_build_reachability @ 0x42F570.  Computes @p out for @p hero and
/// then, for every *other* hero the player owns, runs a second flood and strikes the
/// cells that hero already covers out of the result.  This is the adventure AI's only
/// inter-hero coordination.
void buildReachability(
	CCallback * cb,
	const CGHeroInstance * hero,
	int range,
	const HeroStateMap & heroStates,
	H3Search & out);

}
