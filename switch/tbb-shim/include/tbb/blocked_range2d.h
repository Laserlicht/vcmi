/*
 * blocked_range2d.h - sequential drop-in replacement for Intel oneTBB
 *
 * Part of the VCMI Nintendo Switch port.
 */
#pragma once

#include "blocked_range.h"

#include <cstddef>

namespace tbb
{

/// Sequential equivalent of tbb::blocked_range2d.
/// Holds a row range and a column range; in sequential mode neither is split.
template<typename RowValue, typename ColValue = RowValue>
class blocked_range2d
{
public:
	using row_range_type = blocked_range<RowValue>;
	using col_range_type = blocked_range<ColValue>;
	using size_type = std::size_t;

	blocked_range2d(
		RowValue row_begin, RowValue row_end,
		typename row_range_type::size_type row_grainsize,
		ColValue col_begin, ColValue col_end,
		typename col_range_type::size_type col_grainsize)
		: myRows(row_begin, row_end, row_grainsize)
		, myCols(col_begin, col_end, col_grainsize)
	{
	}

	blocked_range2d(
		RowValue row_begin, RowValue row_end,
		ColValue col_begin, ColValue col_end)
		: myRows(row_begin, row_end)
		, myCols(col_begin, col_end)
	{
	}

	bool empty() const { return myRows.empty() || myCols.empty(); }

	bool is_divisible() const { return false; }

	const row_range_type & rows() const { return myRows; }
	const col_range_type & cols() const { return myCols; }

private:
	row_range_type myRows;
	col_range_type myCols;
};

} // namespace tbb

namespace oneapi { namespace tbb = ::tbb; }
