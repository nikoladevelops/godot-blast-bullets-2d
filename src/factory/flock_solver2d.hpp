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

// One bullet's last steering, kept by its volley while its flock refreshes
// steering on an interval (BulletFlock2D.update_interval > 0).
struct FlockSteerCache2D {
	Vector2 steer;
	uint32_t neighbors = 0;
	bool valid = false; // false = compute at the next pass
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
	real_t blend_in_sec = 0.0; // steering fades in over the volley's first seconds
	bool face_heading = false;
};

// The factory's flock pass (boids), once per sweep before any volley ticks:
// gathers every bullet of every armed volley, sorts them into an exact
// spatial hash grid per flock (cell = half the largest range, so a 5x5 cell
// block covers every flockmate in range and the nearest cells come first)
// and computes one steering vector per bullet. No user code runs inside;
// every buffer only grows, so a warm pass allocates nothing.
//
// Steering V (radians per second; the volley turns its heading at the
// sideways part of V, |V| sin(angle to V), never past V's direction):
//   V = alignment * mean flight heading of the flockmates in neighbor_range
//     + cohesion * mean offset to them / neighbor_range
//     + separation * MEAN over flockmates closer than separation_range of
//       the push (1 - gap / separation_range, at most 2)
// where gap = d (count_bullet_size off) or the distance between the two
// bullets' edges, d - r_p - r_q (on; ranges then compare against the gap,
// and overlapping bullets push harder than touching ones). The separation
// pair is measured separation_lookahead_sec ahead (each along its own flight), so
// flockmates already parting push less. A bullet with a heading is pushed
// SIDEWAYS (perpendicular to its heading, at the full push) away from the
// side the flockmate is on: a flockmate dead ahead turns it aside instead of
// pushing it back (a constant-speed bullet cannot back off). Within a
// quarter of the separation reach of its heading line the side blends into
// the pair's rank (opposite for the two), so the side never flips with a
// hair's change; stacked bullets fan out by a per-bullet golden-ratio side.
// A bullet without a heading is pushed along (p - q) / d.
// At most max_neighbors flockmates are read per bullet (0 = all): own cell
// first (from the bullet's own position on: stacked bullets read different
// flockmates), then the 8-ring, then the 16-ring, each ring started at a
// cell and each cell at a flockmate that rotate with the bullet's ordinal in
// its flock (no drift bias when the cap stops the scan). Ordinals count
// members per flock in gather order, so a flock computes bit-identical
// steering whether or not other flocks exist. Neighbor cells come from a
// dense per-flock array when the flock's cells fill a small box, else from
// the hash. Cost: about 2-5 ns per candidate (one float distance test, one
// inverse square root inside the separation range) plus the gather.
class FlockSolver2D {
public:
	struct Stats {
		int flocks = 0;
		int members = 0; // slots: every bullet of every gathered volley
		int grid_members = 0; // live, finite bullets inside the grid
		int64_t neighbor_checks = 0; // candidates distance-tested
		int capped_members = 0; // stopped at max_neighbors
		int queried_members = 0; // steering recomputed this pass
		uint64_t pass_usec = 0;
	};

	// One pass: begin, gather every armed volley, solve. now / previous:
	// the flock clock (bullet time) now and at the last pass, for
	// update_interval.
	void begin(uint64_t sweep, double now = 0.0, double previous = 0.0);
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
		double reach_sq = 0.0; // max range squared (+ the largest bullet pair when sized)
		double inv_cell = 0.0; // set in solve(): the cell covers the largest bullets gathered
		// count_bullet_size: ranges between bullet edges. Raw ranges for the
		// per-pair tests, and the largest bullet radius gathered this pass.
		bool sized = false;
		float neighbor_range = 0.0f;
		float separation_range = 0.0f;
		double max_radius = 0.0;
		// Separation looks this far ahead (seconds): flockmates already
		// parting push less, so a crowd parts without zigzagging.
		float lookahead = 0.0f;
		uint32_t max_neighbors = 0; // UINT32_MAX = no limit
		uint32_t next_ordinal = 0;
		double update_interval = 0.0; // > 0: refresh each bullet once per interval
		// Field of view: cos and cos^2 of half the view angle; limited = < 360.
		bool view_limited = false;
		float view_cos = -1.0f;
		float view_cos2 = 1.0f;
		// Occupied cells and their box (build_dense_grids); dense_base >= 0
		// = neighbor cells come from dense_cells.
		uint32_t cells = 0;
		int32_t min_cx = 0, max_cx = 0, min_cy = 0, max_cy = 0;
		int64_t dense_base = -1;
		uint32_t dense_width = 0, dense_height = 0;
	};
	struct GroupSlot {
		uint32_t stamp = 0;
		uint32_t group = 0;
		GroupKey key;
	};
	// One flockmate as the query reads it.
	struct Mate {
		float x = 0.0f;
		float y = 0.0f;
		float hx = 0.0f; // flight heading (0, 0 = position only)
		float hy = 0.0f;
		// Bits 0-30: per flock, gather order (rotations, stacked pairs);
		// bit 31: has a heading (counts for alignment). Packed: the candidate
		// loop streams these, and every byte per flockmate costs.
		uint32_t ordinal = 0;
		float r = 0.0f; // bullet radius (sized flocks only)
		// Where the bullet will be separation_lookahead_sec ahead (its own
		// flight: heading x speed), worked out once at gather: a pair's
		// offset ahead is then one subtraction, like its offset now.
		float ax = 0.0f;
		float ay = 0.0f;
	};
	static constexpr uint32_t MATE_HEADING_BIT = 0x80000000u;
	static constexpr uint32_t MATE_ORDINAL_MASK = 0x7fffffffu;
	struct CellSlot {
		uint32_t stamp = 0;
		uint32_t group = 0;
		int32_t cx = 0;
		int32_t cy = 0;
		uint32_t cell = 0;
	};

	void build_dense_grids();
	int64_t neighbor_cell(uint32_t group, int32_t cx, int32_t cy) const;
	uint32_t group_for(const GroupKey &key, const BulletFlock2D &flock);
	uint32_t cell_for(uint32_t group, int32_t cx, int32_t cy);
	int64_t find_cell(uint32_t group, int32_t cx, int32_t cy) const;
	static uint32_t hash_cell(uint32_t group, int32_t cx, int32_t cy);
	void grow_group_table();
	void grow_cell_table(size_t want);

	uint64_t sweep = 0;
	uint32_t stamp = 0; // per pass: occupied table slots carry it
	double clock_now = 0.0;
	double clock_previous = 0.0;
	Stats stats;

	std::vector<Group> groups;
	std::vector<GroupSlot> group_table;

	// Per slot (base + bullet index), zeroed every pass.
	std::vector<Vector2> steer;
	std::vector<uint32_t> neighbors;
	std::vector<uint8_t> gathered; // 1 = in the grid this pass

	// Volleys whose flock refreshes on an interval: their caches are
	// written back after the solve (raw pointers: no user code runs in a
	// pass).
	struct CachedVolley {
		BulletVolley2D *volley = nullptr;
		uint32_t base = 0;
		uint32_t amount = 0;
	};
	std::vector<CachedVolley> cached_volleys;

	// Grid members in gather order.
	std::vector<uint32_t> m_slot;
	std::vector<uint32_t> m_group;
	std::vector<uint32_t> m_cell;
	std::vector<uint8_t> m_query; // 1 = recompute this pass
	std::vector<Mate> m_mate;

	// Cells (exact: one per occupied (flock, cx, cy)).
	std::vector<CellSlot> cell_table;
	std::vector<uint32_t> cell_group;
	std::vector<int32_t> cell_cx, cell_cy;
	std::vector<uint32_t> cell_start, cell_count, sort_cursor;
	std::vector<int32_t> dense_cells;
	uint32_t cell_total = 0;

	// Grid members sorted by cell (stable): a cell's flockmates are one
	// contiguous run.
	std::vector<uint32_t> s_slot;
	std::vector<uint8_t> s_query;
	std::vector<Mate> s_mate;
};
} //namespace BlastBullets2D
