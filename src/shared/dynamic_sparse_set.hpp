#pragma once

#include "godot_cpp/core/defs.hpp"
#include <vector>

class DynamicSparseSet {
private:
	int max_size = 0;

	// Contains ONLY the active indexes - Packed active IDs for cache-friendly looping
	std::vector<int> dense;

	// Maps data indexes to their position inside the dense - used for O(1) removal from the active indexes dense vector
	std::vector<int> sparse;

public:
	DynamicSparseSet(int min_size = 1024) {
		max_size = min_size;
		sparse.resize(min_size, -1);
		dense.reserve(min_size);
	}

	// Resizes the sparse vector to accommodate new size
	_ALWAYS_INLINE_ void resize(int new_size) {
		max_size = new_size;
		sparse.resize(new_size, -1);
		dense.reserve(new_size);
	}

	// Marks a specific index as active by adding it to the dense vector. Note that if index is bigger than current size, the structure is resized to accommodate it
	_ALWAYS_INLINE_ void activate_data(int index) {
		if (index < 0) {
			return;
		}

		// Allocate more space if needed
		if (index >= max_size) {
			int new_size = max_size * 2;

			if (index >= new_size) {
				new_size = index + 1;
			}

			resize(new_size);
		}

		// If it's already active, do nothing - this avoids duplicates js in case
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
		if (index >= max_size) {
			return;
		}

		// Get the index's position inside the active indexes dense vec
		int position_in_dense = sparse[index];

		// If the index is not active, do nothing
		if (position_in_dense == -1) {
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

	// Get the active indexes dense vector
	_ALWAYS_INLINE_ const std::vector<int> &get_active_indexes() const {
		return dense;
	}

	// Get the size of the dense vector
	_ALWAYS_INLINE_ int size() const {
		return static_cast<int>(dense.size());
	}

	// Activates a range of indexes
	// If end is -1, it uses max_size. If end > max_size, it resizes.
	_ALWAYS_INLINE_ void activate_range_data(int index_start, int index_end_inclusive) {
		// Sanitize index_start
		index_start = (index_start < 0) ? 0 : index_start;

		// Sanitize End
		int end = index_end_inclusive;
		if (end < 0) {
			end = (max_size > 0) ? max_size - 1 : 0;
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

	// Disables a range of indexes
	_ALWAYS_INLINE_ void disable_range_data(int index_start, int index_end_inclusive) {
		index_start = (index_start < 0) ? 0 : index_start;
		int end = index_end_inclusive;

		if (end < 0 || end >= max_size) {
			end = max_size - 1;
		}

		// if user is disabling the ENTIRE range, just clear it
		if (index_start == 0 && end == max_size - 1) {
			clear();
			return;
		}

		for (int i = index_start; i <= end; ++i) {
			disable_data(i);
		}
	}

	// Activate all indexes
	_ALWAYS_INLINE_ void activate_all_data() {
		activate_range_data(0, max_size - 1);
	}

	// Disable all indexes
	_ALWAYS_INLINE_ void disable_all_data() {
		disable_range_data(0, max_size - 1);
	}
};
