#pragma once

#include <type_traits>

// Every enum bound to scripts carries a fixed `int` underlying type
// (`enum X : int`). GDScript may pass any int to an enum argument, and an
// out-of-range value held by an enum without a fixed type is undefined
// behavior before any setter can reject it (UBSan: "load of value
// 4294967295, which is not a valid value for type ..."). GCC gives an
// unfixed enum without negative enumerators `unsigned int`, so a forgotten
// `: int` fails this assert at compile time.
#define BLAST_VARIANT_ENUM_CAST(m_enum)                                                                  \
	static_assert(std::is_same_v<std::underlying_type_t<m_enum>, int>, #m_enum " must be `enum : int`"); \
	VARIANT_ENUM_CAST(m_enum)
