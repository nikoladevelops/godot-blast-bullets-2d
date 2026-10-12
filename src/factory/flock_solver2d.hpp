#pragma once

#include "data/bullet_flock2d.hpp"

#include <godot_cpp/variant/vector2.hpp>

#include <cstdint>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletVolley2D;

// WHO flies together for one armed volley: the flock rules plus the scope
// that turns them into a flock key. A spawner owns one (std::shared_ptr) and
// shares it with every volley it armed, so its edits reach bullets in flight
// and orphans keep it; BulletVolley2D.flock_join gives a volley its own.
struct FlockConfig2D {
	Ref<BulletFlock2D> flock;
	int scope = BulletFlock2D::SCOPE_PER_SPAWNER;
	bool enabled = true;
	// The arming spawner (captured: orphans keep their swarm together after
	// owner_spawner_id is cleared); 0 = the factory.
	uint64_t owner_id = 0;
	// Bumped by every edit: a volley whose view carries an older serial does
	// not steer until the next pass (an edit from a handler mid-sweep).
	uint32_t serial = 1;
};

// Where one volley's bullets sit in the last pass (written by the pass,
// read by the volley's own tick). Never a pointer: the volley resolves the
// solver's buffer at move time and checks every field first.
struct FlockView2D {
	uint32_t base = 0;
	uint64_t sweep = 0; // 0 = never gathered
	int life = -1;
	uint32_t serial = 0;
	real_t max_turn_rad = 0.0; // per second, 0 = unlimited
	bool face_heading = false;
};

// The factory's flock pass (boids), once per sweep before any volley ticks:
// gathers every bullet of every armed volley, sorts them into an exact
// spatial hash grid per flock (cell = half the largest range, so a 5x5 cell
// block covers every flockmate in range and the nearest cells come first)
// and computes one steering vector per bullet. No user code runs inside;
// every buffer only grows, so a warm pass allocates nothing.
//
// Steering V (per second, the volley turns its heading toward V's direction
// by 1 - e^(-|V| dt) of the angle):
//   V = alignment * mean flight heading of the flockmates in neighbor_range
//     + cohesion * mean offset to them / neighbor_range
//     + separation * sum over flockmates closer than separation_range of
//       (p - q) / d * (1 - d / separation_range)
// At most max_neighbors flockmates are read per bullet (0 = all): own cell
// first, then the 8-ring, then the 16-ring, each ring started at an offset
// that rotates with the bullet's ordinal in its flock (no drift bias), and
// each cell scanned from a rotated start (stacked bullets see different
// flockmates). Ordinals count members per flock in gather order, so a flock
// computes bit-identical steering whether or not other flocks exist.
class FlockSolver2D {
public:
	struct Stats {
		int flocks = 0;
		int members = 0; // slots: every bullet of every gathered volley
		int grid_members = 0; // live, finite bullets inside the grid
		int64_t neighbor_checks = 0; // candidates distance-tested
		int capped_members = 0; // stopped at max_neighbors
		uint64_t pass_usec = 0;
	};

	// One pass: begin, gather every armed volley, solve.
	void begin(uint64_t sweep);
	// Gathers the volley's bullets into slots base..base+amount and writes
	// its view. volley_id keys Per Volley flocks.
	void add_volley(BulletVolley2D &volley, const FlockConfig2D &config, uint64_t volley_id);
	void solve();

	uint64_t get_sweep() const { return sweep; }
	bool has_results() const { return !steer.empty() || stats.flocks > 0 || stats.pass_usec > 0; }
	bool covers(uint32_t base, int amount) const { return amount >= 0 && (uint64_t)base + (uint64_t)amount <= (uint64_t)steer.size(); }
	const Vector2 *steer_data() const { return steer.data(); }
	Vector2 steer_at(uint32_t slot) const { return slot < steer.size() ? steer[slot] : Vector2(); }
	int neighbors_at(uint32_t slot) const { return slot < neighbors.size() ? (int)neighbors[slot] : 0; }
	const Stats &get_stats() const { return stats; }
	void set_pass_usec(uint64_t usec) { stats.pass_usec = usec; }
	// Drops every buffer (factory reset).
	void clear();

private:
	struct GroupKey {
		uint64_t flock_id = 0;
		uint64_t value = 0;
		uint32_t kind = 0;
	};
	struct Group {
		double separation = 0.0;
		double alignment = 0.0;
		double cohesion = 0.0;
		double neighbor_range_sq = 0.0;
		double separation_range_sq = 0.0;
		double inv_neighbor_range = 0.0;
		double inv_separation_range = 0.0; // 0 when separation_range is 0
		double reach_sq = 0.0; // max range squared
		double inv_cell = 0.0;
		uint32_t max_neighbors = 0; // UINT32_MAX = no limit
		uint32_t next_ordinal = 0;
	};
	struct GroupSlot {
		uint32_t stamp = 0;
		uint32_t group = 0;
		GroupKey key;
	};
	struct CellSlot {
		uint32_t stamp = 0;
		uint32_t group = 0;
		int32_t cx = 0;
		int32_t cy = 0;
		uint32_t cell = 0;
	};

	uint32_t group_for(const GroupKey &key, const BulletFlock2D &flock);
	uint32_t cell_for(uint32_t group, int32_t cx, int32_t cy);
	int64_t find_cell(uint32_t group, int32_t cx, int32_t cy) const;
	static uint32_t hash_cell(uint32_t group, int32_t cx, int32_t cy);
	void grow_group_table();
	void grow_cell_table(size_t want);

	uint64_t sweep = 0;
	uint32_t stamp = 0; // per pass: occupied table slots carry it
	Stats stats;

	std::vector<Group> groups;
	std::vector<GroupSlot> group_table;

	// Per slot (base + bullet index), zeroed every pass.
	std::vector<Vector2> steer;
	std::vector<uint32_t> neighbors;

	// Grid members in gather order.
	std::vector<uint32_t> m_slot;
	std::vector<uint32_t> m_ordinal;
	std::vector<uint32_t> m_cell;
	std::vector<float> m_x, m_y, m_hx, m_hy;

	// Cells (exact: one per occupied (flock, cx, cy)).
	std::vector<CellSlot> cell_table;
	std::vector<uint32_t> cell_group;
	std::vector<int32_t> cell_cx, cell_cy;
	std::vector<uint32_t> cell_start, cell_count, sort_cursor;
	uint32_t cell_total = 0;

	// Grid members sorted by cell (stable), cache-friendly SoA.
	std::vector<uint32_t> s_slot, s_ordinal;
	std::vector<float> s_x, s_y, s_hx, s_hy;
};
} //namespace BlastBullets2D
