#pragma once

#include "godot_cpp/core/defs.hpp"
#include <climits>
#include <vector>

class DynamicSparseSet {
private:
	int max_size = 0;

	// Contains ONLY the active indexes - Packed active IDs for cache-friendly looping
	std::vector<int> dense;

	// Maps data indexes to their position inside the dense - used for O(1) removal from the active indexes dense vector
	std::vector<int> sparse;

public:
	// Resizes the sparse vector to accommodate new size
	_ALWAYS_INLINE_ void resize(int new_size) {
		if (new_size < 0) {
			return;
		}
		if (new_size < max_size) {
			// Shrinking: dense may hold indexes beyond the new range. Strip them and
			// rebuild sparse positions, otherwise get_active_indexes() would return
			// out-of-bounds indexes after a shrink (consumers index arrays with them).
			std::vector<int> kept;
			kept.reserve(dense.size());
			for (int idx : dense) {
				if (idx < new_size) {
					kept.push_back(idx);
				}
			}
			dense.swap(kept);
			for (int pos = 0; pos < (int)dense.size(); ++pos) {
				sparse[dense[pos]] = pos;
			}
		}
		max_size = new_size;
		sparse.resize(new_size, -1);
		dense.reserve(new_size);
	}

	// Marks a specific index as active by adding it to the dense vector. Note that if index is bigger than current size, the structure is resized to accommodate it
	_ALWAYS_INLINE_ void activate_data(int index) {
		if (index < 0) {
			return;
		}

		// Allocate more space if needed. The growth arithmetic saturates:
		// max_size * 2 and index + 1 overflow an int at the extremes (UB),
		// even though no script-reachable index gets near them.
		if (index >= max_size) {
			int new_size = (max_size == 0) ? 8 : (max_size > INT_MAX / 2 ? INT_MAX : max_size * 2);
			if (index >= new_size && index < INT_MAX) {
				new_size = index + 1;
			}
			resize(new_size);
		}

		// If it's already active, do nothing - this avoids duplicates just in case
		if (sparse[index] != -1) {
			return;
		}

		// Make it active by default
		dense.push_back(index);

		// Store where this index is located inside the active indexes dense vec
		sparse[index] = static_cast<int>(dense.size()) - 1;
	}

	// Marks a specific index as inactive by removing it from the dense vector
	_ALWAYS_INLINE_ void disable_data(int index) {
		if (index < 0 || index >= max_size) {
			return;
		}

		// Get the index's position inside the active indexes dense vec
		int position_in_dense = sparse[index];

		// If the index is not active, do nothing. The range check is
		// defensive: a desynced slot must never index dense out of bounds.
		if (position_in_dense < 0 || position_in_dense >= (int)dense.size()) {
			sparse[index] = -1;
			return;
		}

		// Swap the index to be removed with the last index in the active indexes dense vec
		int last_index = dense.back(); // We get the last index cached

		// We place the last index (which is valid) in the position of the index we want to remove
		dense[position_in_dense] = last_index;

		// Update the sparse vec to point to the new position of the last index (this keeps the mapping correct)
		sparse[last_index] = position_in_dense;

		// Remove the last index
		dense.pop_back();

		// Mark the index as inactive
		sparse[index] = -1;
	}

	// Checks if an index is currently active (in the dense vector)
	_ALWAYS_INLINE_ bool contains(int index) const {
		return index >= 0 && index < max_size && sparse[index] != -1;
	}

	// Clears all active indexes
	_ALWAYS_INLINE_ void clear() {
		dense.clear();
		sparse.assign(max_size, -1);
	}

	// The active indexes in dense order, BY REFERENCE (hot loops read it
	// without a copy). Never hold this reference across user code: a callback
	// may disable/enable an index and invalidate it. Snapshot it first (the
	// volley tick and interpolation pass use their own scratch vectors).
	_ALWAYS_INLINE_ const std::vector<int> &get_active_indexes() const {
		return dense;
	}

	// Get the size of the dense vector
	_ALWAYS_INLINE_ int size() const {
		return static_cast<int>(dense.size());
	}

	// The index space the set currently covers (the allocated sparse slots; 0
	// before the first resize). For readouts: active <= capacity always.
	_ALWAYS_INLINE_ int capacity() const {
		return max_size;
	}

	// Activates a range of indexes
	// If end is -1, it uses max_size. If end > max_size, it resizes.
	_ALWAYS_INLINE_ void activate_range_data(int index_start, int index_end_inclusive = -1) {
		// Sanitize index_start
		index_start = (index_start < 0) ? 0 : index_start;

		// Sanitize End. INT_MAX is not representable as a loop bound
		// (++i would overflow): the last valid index is INT_MAX - 1.
		int end = index_end_inclusive;
		if (end >= INT_MAX) {
			end = INT_MAX - 1;
		}
		if (end < 0) {
			if (max_size <= 0) {
				return;
			}
			end = max_size - 1;
		}

		if (index_start > end) {
			return;
		}

		// If the range goes beyond current memory, grow once to fit the whole range
		if (end >= max_size) {
			resize(end + 1);
		}

		// Batch Activate
		for (int i = index_start; i <= end; ++i) {
			activate_data(i);
		}
	}

	// Activate all indexes
	_ALWAYS_INLINE_ void activate_all_data() {
		if (max_size <= 0) {
			return;
		}
		activate_range_data(0, max_size - 1);
	}

	// Full consistency check (cold; used by debug_check_invariants after every
	// test): every dense entry maps back to its own sparse slot, no duplicates,
	// no active sparse slot missing from dense. Never mutates.
	bool debug_validate() const {
		std::vector<uint8_t> seen((size_t)max_size, 0);
		for (size_t pos = 0; pos < dense.size(); ++pos) {
			const int value = dense[pos];
			if (value < 0 || value >= max_size) {
				return false;
			}
			if (sparse[(size_t)value] != (int)pos) {
				return false;
			}
			if (seen[(size_t)value] != 0) {
				return false;
			}
			seen[(size_t)value] = 1;
		}
		for (int i = 0; i < max_size; ++i) {
			const int pos = sparse[(size_t)i];
			if (pos < 0) {
				continue;
			}
			if (pos >= (int)dense.size() || dense[(size_t)pos] != i) {
				return false;
			}
		}
		return true;
	}
};
