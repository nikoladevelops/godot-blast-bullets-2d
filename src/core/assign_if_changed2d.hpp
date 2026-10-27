#pragma once

#include "godot_cpp/variant/array.hpp"
#include "godot_cpp/variant/dictionary.hpp"
#include <type_traits>
#include <utility>

namespace BlastBullets2D {

// The data resources' setter rule: a write that leaves the value as it was
// is not a change (no `changed` emit: a spawner would otherwise re-duplicate
// its spawn data for nothing). Returns whether the field changed.
// Scalars, vectors, Packed*Array (copy-on-write values) compare by value and
// Resources by instance. Array and Dictionary are references a script may
// have mutated in place before handing the very same object back, so a
// write of one always counts as a change.
template <class T, class U>
bool assign_if_changed2d(T &field, U &&value) {
	if constexpr (std::is_base_of_v<godot::Array, T> || std::is_same_v<T, godot::Dictionary>) {
		field = std::forward<U>(value);
		return true;
	} else {
		const T converted = static_cast<T>(std::forward<U>(value));
		if (field == converted) {
			return false;
		}
		field = converted;
		return true;
	}
}

} // namespace BlastBullets2D
