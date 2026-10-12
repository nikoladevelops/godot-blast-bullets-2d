// The factory's flock pass (boids): gather, exact spatial hash grid, one
// steering vector per bullet. See flock_solver2d.hpp for the contract; the
// volley applies the result in step_flock (bullet_volley2d_tick.cpp).

#include "factory/flock_solver2d.hpp"

#include "bullet_volley/bullet_volley2d.hpp"

#include <godot_cpp/core/math.hpp>

#include <cmath>
#include <cstring>
#include <limits>

using namespace godot;

namespace BlastBullets2D {

namespace {

// Cells farther than this from the origin stay out of the grid: a bullet
// there neither steers nor counts (saturated coordinates would otherwise
// pile into one cell and make the pass quadratic).
constexpr double CELL_LIMIT = 1073741824.0; // 2^30
// Below this squared distance two flockmates are stacked: no direction to
// part along, so each pair parts along a fixed direction of its own.
constexpr double COINCIDENT_SQ = 1e-12;
// |V| cap: keeps extreme weights finite (the turn is complete long before).
constexpr double MAX_STEER = 1e6;

// Scan order of the neighbor cells around a member's own cell: the 8-ring,
// then the 16-ring.
constexpr int RING_DX[24] = { 1, 1, 0, -1, -1, -1, 0, 1, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2 };
constexpr int RING_DY[24] = { 0, 1, 1, 1, 0, -1, -1, -1, 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1 };
// A flock whose occupied cells fill a box of at most this many cells per
// occupied cell (plus a small floor) looks neighbors up in a dense array.
constexpr int DENSE_FACTOR = 8;

// 64 unit directions for stacked pairs (no trig per pair).
struct CoincidentDirections {
	float x[64];
	float y[64];
	CoincidentDirections() {
		for (int k = 0; k < 64; ++k) {
			const double angle = ((double)k + 0.5) * Math::TAU / 64.0;
			x[k] = (float)std::cos(angle);
			y[k] = (float)std::sin(angle);
		}
	}
};
const CoincidentDirections &coincident_directions() {
	static const CoincidentDirections directions;
	return directions;
}

inline uint32_t mix32(uint32_t h) {
	h ^= h >> 16;
	h *= 0x7feb352du;
	h ^= h >> 15;
	h *= 0x846ca68bu;
	h ^= h >> 16;
	return h;
}

inline uint64_t mix64(uint64_t h) {
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdull;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ull;
	h ^= h >> 33;
	return h;
}

inline size_t next_pow2(size_t value) {
	size_t out = 16;
	while (out < value) {
		out <<= 1;
	}
	return out;
}

// 1 / sqrt(x) for x > 0, without a division or sqrt instruction: the
// classic bit-level estimate refined by two Newton steps (relative error
// below 5e-6). Plain integer and float operations, so every CPU computes the
// same bits (hardware rsqrt estimates differ between vendors).
inline float inv_sqrt2d(float x) {
	uint32_t bits;
	std::memcpy(&bits, &x, sizeof(bits));
	bits = 0x5f375a86u - (bits >> 1);
	float y;
	std::memcpy(&y, &bits, sizeof(y));
	const float half = 0.5f * x;
	y = y * (1.5f - half * y * y);
	y = y * (1.5f - half * y * y);
	return y;
}

inline bool finite_float(float value) {
	return std::isfinite(value);
}

} // namespace

void FlockSolver2D::begin(uint64_t new_sweep, double now, double previous) {
	sweep = new_sweep;
	clock_now = now;
	clock_previous = previous;
	++stamp;
	if (stamp == 0) {
		// Wrapped: no slot may look occupied by accident.
		for (GroupSlot &slot : group_table) {
			slot.stamp = 0;
		}
		for (CellSlot &slot : cell_table) {
			slot.stamp = 0;
		}
		stamp = 1;
	}
	groups.clear();
	steer.clear();
	neighbors.clear();
	gathered.clear();
	cached_volleys.clear();
	m_slot.clear();
	m_cell.clear();
	m_query.clear();
	m_mate.clear();
	cell_group.clear();
	cell_cx.clear();
	cell_cy.clear();
	cell_total = 0;
	stats = Stats();
}

void FlockSolver2D::clear() {
	*this = FlockSolver2D();
}

void FlockSolver2D::grow_group_table() {
	std::vector<GroupSlot> old;
	old.swap(group_table);
	group_table.assign(next_pow2(old.size() * 2), GroupSlot());
	const size_t mask = group_table.size() - 1;
	for (const GroupSlot &slot : old) {
		if (slot.stamp != stamp) {
			continue;
		}
		size_t at = mix64(slot.key.flock_id ^ mix64(slot.key.value + slot.key.kind)) & mask;
		while (group_table[at].stamp == stamp) {
			at = (at + 1) & mask;
		}
		group_table[at] = slot;
	}
}

uint32_t FlockSolver2D::group_for(const GroupKey &key, const BulletFlock2D &flock) {
	if (group_table.empty() || (groups.size() + 1) * 2 > group_table.size()) {
		grow_group_table();
	}
	const size_t mask = group_table.size() - 1;
	size_t at = mix64(key.flock_id ^ mix64(key.value + key.kind)) & mask;
	while (group_table[at].stamp == stamp) {
		const GroupKey &k = group_table[at].key;
		if (k.flock_id == key.flock_id && k.value == key.value && k.kind == key.kind) {
			return group_table[at].group;
		}
		at = (at + 1) & mask;
	}
	// New flock this pass: its rules, read once.
	Group g;
	const double neighbor_range = (double)flock.neighbor_range;
	const double separation_range = (double)flock.separation_range;
	const double reach = MAX(neighbor_range, separation_range);
	g.separation = (double)flock.separation;
	g.alignment = (double)flock.alignment;
	g.cohesion = (double)flock.cohesion;
	g.neighbor_range_sq = neighbor_range * neighbor_range;
	g.separation_range_sq = separation_range * separation_range;
	g.inv_neighbor_range = neighbor_range > 0.0 ? 1.0 / neighbor_range : 0.0;
	g.inv_separation_range = separation_range > 0.0 ? 1.0 / separation_range : 0.0;
	g.reach_sq = reach * reach;
	g.inv_cell = reach > 0.0 ? 2.0 / reach : 0.0;
	g.max_neighbors = flock.max_neighbors > 0 ? (uint32_t)flock.max_neighbors : std::numeric_limits<uint32_t>::max();
	g.update_interval = (double)flock.update_interval;
	const uint32_t index = (uint32_t)groups.size();
	groups.push_back(g);
	GroupSlot &slot = group_table[at];
	slot.stamp = stamp;
	slot.group = index;
	slot.key = key;
	return index;
}

uint32_t FlockSolver2D::hash_cell(uint32_t group, int32_t cx, int32_t cy) {
	return mix32((uint32_t)cx * 0x8da6b343u ^ (uint32_t)cy * 0xd8163841u ^ group * 0xcb1ab31fu);
}

void FlockSolver2D::grow_cell_table(size_t want) {
	cell_table.assign(next_pow2(want * 2), CellSlot());
	const size_t mask = cell_table.size() - 1;
	for (uint32_t c = 0; c < cell_total; ++c) {
		size_t at = hash_cell(cell_group[c], cell_cx[c], cell_cy[c]) & mask;
		while (cell_table[at].stamp == stamp) {
			at = (at + 1) & mask;
		}
		CellSlot &slot = cell_table[at];
		slot.stamp = stamp;
		slot.group = cell_group[c];
		slot.cx = cell_cx[c];
		slot.cy = cell_cy[c];
		slot.cell = c;
	}
}

uint32_t FlockSolver2D::cell_for(uint32_t group, int32_t cx, int32_t cy) {
	if (cell_table.empty() || ((size_t)cell_total + 1) * 2 > cell_table.size()) {
		grow_cell_table((size_t)cell_total + 1);
	}
	const size_t mask = cell_table.size() - 1;
	size_t at = hash_cell(group, cx, cy) & mask;
	while (cell_table[at].stamp == stamp) {
		const CellSlot &slot = cell_table[at];
		if (slot.group == group && slot.cx == cx && slot.cy == cy) {
			return slot.cell;
		}
		at = (at + 1) & mask;
	}
	CellSlot &slot = cell_table[at];
	slot.stamp = stamp;
	slot.group = group;
	slot.cx = cx;
	slot.cy = cy;
	slot.cell = cell_total;
	cell_group.push_back(group);
	cell_cx.push_back(cx);
	cell_cy.push_back(cy);
	return cell_total++;
}

int64_t FlockSolver2D::find_cell(uint32_t group, int32_t cx, int32_t cy) const {
	const size_t mask = cell_table.size() - 1;
	size_t at = hash_cell(group, cx, cy) & mask;
	while (cell_table[at].stamp == stamp) {
		const CellSlot &slot = cell_table[at];
		if (slot.group == group && slot.cx == cx && slot.cy == cy) {
			return slot.cell;
		}
		at = (at + 1) & mask;
	}
	return -1;
}

void FlockSolver2D::add_volley(BulletVolley2D &volley, const FlockConfig2D &config, uint64_t volley_id) {
	const BulletFlock2D *flock = config.flock.ptr();
	if (flock == nullptr) {
		return;
	}
	GroupKey key;
	key.flock_id = flock->get_instance_id();
	if (config.scope == BulletFlock2D::SCOPE_SHARED) {
		key.kind = 1;
	} else if (config.scope == BulletFlock2D::SCOPE_PER_VOLLEY) {
		key.kind = 2;
		key.value = volley_id;
	} else {
		key.value = config.owner_id;
	}
	const uint32_t group = group_for(key, *flock);
	const double inv_cell = groups[group].inv_cell;
	const double interval = groups[group].update_interval;

	const int amount = MAX(volley.amount_bullets, 0);
	const uint32_t base = (uint32_t)steer.size();
	steer.resize((size_t)base + (size_t)amount);
	neighbors.resize((size_t)base + (size_t)amount);
	gathered.resize((size_t)base + (size_t)amount);
	stats.members += amount;
	// On an interval the volley keeps each bullet's last steering: a bullet
	// not due this pass reuses it (and still counts as a flockmate).
	FlockSteerCache2D *cache = nullptr;
	if (interval > 0.0) {
		if ((int)volley.flock_cache.size() != amount) {
			volley.flock_cache.assign((size_t)amount, FlockSteerCache2D());
		}
		cache = volley.flock_cache.data();
		CachedVolley cached;
		cached.volley = &volley;
		cached.base = base;
		cached.amount = (uint32_t)amount;
		cached_volleys.push_back(cached);
	}

	const int limit = MIN(MIN(amount, (int)volley.all_cached_instance_origin.size()), MIN((int)volley.all_cached_direction.size(), (int)volley.all_cached_speed.size()));
	const Vector2 *origins = volley.all_cached_instance_origin.data();
	const Vector2 *directions = volley.all_cached_direction.data();
	const real_t *speeds = volley.all_cached_speed.data();
	for (int i = 0; i < limit; ++i) {
		if (!volley.all_bullets_enabled_set.contains(i)) {
			continue;
		}
		const float x = (float)origins[i].x;
		const float y = (float)origins[i].y;
		if (!finite_float(x) || !finite_float(y)) {
			continue;
		}
		const double fx = (double)x * inv_cell;
		const double fy = (double)y * inv_cell;
		if (!(std::fabs(fx) < CELL_LIMIT && std::fabs(fy) < CELL_LIMIT)) {
			continue;
		}
		// Flight heading: a negative speed flies against the direction. A
		// zero or broken heading still counts as a position.
		const double sign = speeds[i] < 0.0 ? -1.0 : 1.0;
		double hx = (double)directions[i].x * sign;
		double hy = (double)directions[i].y * sign;
		const double h2 = hx * hx + hy * hy;
		if (std::isfinite(h2) && h2 > 1e-8) {
			const double inv = 1.0 / std::sqrt(h2);
			hx *= inv;
			hy *= inv;
		} else {
			hx = 0.0;
			hy = 0.0;
		}
		const uint32_t ordinal = groups[group].next_ordinal++;
		uint8_t query = 1;
		if (cache != nullptr && cache[i].valid) {
			steer[base + (uint32_t)i] = cache[i].steer;
			neighbors[base + (uint32_t)i] = cache[i].neighbors;
			// Due when the clock crossed this bullet's next refresh: phases
			// spread by the golden ratio, so the refreshes of a flock spread
			// evenly over the interval.
			const double phase = (ordinal * 0.6180339887498949 - std::floor(ordinal * 0.6180339887498949)) * interval;
			query = std::floor((clock_now + phase) / interval) != std::floor((clock_previous + phase) / interval) ? 1 : 0;
		}
		gathered[base + (uint32_t)i] = 1;
		m_slot.push_back(base + (uint32_t)i);
		m_query.push_back(query);
		m_cell.push_back(cell_for(group, (int32_t)std::floor(fx), (int32_t)std::floor(fy)));
		Mate mate;
		mate.x = x;
		mate.y = y;
		mate.hx = (float)hx;
		mate.hy = (float)hy;
		mate.has_heading = (hx != 0.0 || hy != 0.0) ? 1u : 0u;
		mate.ordinal = ordinal;
		m_mate.push_back(mate);
	}

	FlockView2D &view = volley.flock_view;
	view.base = base;
	view.sweep = sweep;
	view.life = volley.volley_generation;
	view.serial = config.serial;
	view.max_turn_rad = (real_t)Math::deg_to_rad((double)flock->max_turn_rate_degrees);
	view.face_heading = flock->face_heading;
}

void FlockSolver2D::build_dense_grids() {
	// Per flock: when its occupied cells fill a small enough box, a dense
	// array maps every cell of the box to its id (neighbor lookups become
	// one indexed read); sparse flocks (far outliers) keep the hash.
	for (Group &g : groups) {
		g.cells = 0;
	}
	for (uint32_t c = 0; c < cell_total; ++c) {
		Group &g = groups[cell_group[c]];
		if (g.cells == 0) {
			g.min_cx = g.max_cx = cell_cx[c];
			g.min_cy = g.max_cy = cell_cy[c];
		} else {
			g.min_cx = MIN(g.min_cx, cell_cx[c]);
			g.max_cx = MAX(g.max_cx, cell_cx[c]);
			g.min_cy = MIN(g.min_cy, cell_cy[c]);
			g.max_cy = MAX(g.max_cy, cell_cy[c]);
		}
		++g.cells;
	}
	size_t total = 0;
	for (Group &g : groups) {
		g.dense_base = -1;
		if (g.cells == 0) {
			continue;
		}
		const int64_t width = (int64_t)g.max_cx - (int64_t)g.min_cx + 1;
		const int64_t height = (int64_t)g.max_cy - (int64_t)g.min_cy + 1;
		if (width * height > (int64_t)DENSE_FACTOR * (int64_t)g.cells + 256) {
			continue;
		}
		g.dense_width = (uint32_t)width;
		g.dense_height = (uint32_t)height;
		g.dense_base = (int64_t)total;
		total += (size_t)(width * height);
	}
	dense_cells.assign(total, -1);
	for (uint32_t c = 0; c < cell_total; ++c) {
		const Group &g = groups[cell_group[c]];
		if (g.dense_base >= 0) {
			dense_cells[(size_t)g.dense_base + (size_t)(cell_cy[c] - g.min_cy) * g.dense_width + (size_t)(cell_cx[c] - g.min_cx)] = (int32_t)c;
		}
	}
}

int64_t FlockSolver2D::neighbor_cell(uint32_t group, int32_t cx, int32_t cy) const {
	const Group &g = groups[group];
	if (g.dense_base < 0) {
		return find_cell(group, cx, cy);
	}
	const int64_t x = (int64_t)cx - (int64_t)g.min_cx;
	const int64_t y = (int64_t)cy - (int64_t)g.min_cy;
	if (x < 0 || y < 0 || x >= (int64_t)g.dense_width || y >= (int64_t)g.dense_height) {
		return -1;
	}
	return dense_cells[(size_t)g.dense_base + (size_t)y * g.dense_width + (size_t)x];
}

void FlockSolver2D::solve() {
	const uint32_t total = (uint32_t)m_slot.size();
	stats.flocks = (int)groups.size();
	stats.grid_members = (int)total;
	if (total == 0) {
		return;
	}

	// Stable counting sort by cell: exact runs, gather order inside a run.
	cell_count.assign(cell_total, 0);
	for (uint32_t m = 0; m < total; ++m) {
		++cell_count[m_cell[m]];
	}
	cell_start.resize(cell_total);
	uint32_t run = 0;
	for (uint32_t c = 0; c < cell_total; ++c) {
		cell_start[c] = run;
		run += cell_count[c];
	}
	s_slot.resize(total);
	s_query.resize(total);
	s_mate.resize(total);
	sort_cursor.assign(cell_start.begin(), cell_start.end());
	for (uint32_t m = 0; m < total; ++m) {
		const uint32_t at = sort_cursor[m_cell[m]]++;
		s_slot[at] = m_slot[m];
		s_query[at] = m_query[m];
		s_mate[at] = m_mate[m];
	}
	build_dense_grids();

	int64_t checks = 0;
	int capped = 0;
	int queried = 0;
	const CoincidentDirections &dirs = coincident_directions();
	const Mate *const mates = s_mate.data();
	// The 24 neighbor cells of the cell being queried, in ring order (8-ring
	// then 16-ring): [start, start + count) runs of the sorted members.
	uint32_t ring_start[24];
	uint32_t ring_count[24];
	for (uint32_t c = 0; c < cell_total; ++c) {
		const uint32_t group = cell_group[c];
		const Group &g = groups[group];
		for (int k = 0; k < 24; ++k) {
			const int64_t id = neighbor_cell(group, cell_cx[c] + RING_DX[k], cell_cy[c] + RING_DY[k]);
			ring_start[k] = id >= 0 ? cell_start[id] : 0;
			ring_count[k] = id >= 0 ? cell_count[id] : 0;
		}
		// The candidate test runs in float (positions are float already);
		// the sums feed the steering in double below. Plain locals: the
		// compiler keeps them in registers across every candidate.
		const float reach_sq = (float)g.reach_sq;
		const float near_sq = (float)g.neighbor_range_sq;
		const float sep_sq = (float)g.separation_range_sq;
		const float inv_sep = (float)g.inv_separation_range;
		const uint32_t cap = g.max_neighbors;
		const uint32_t own_start = cell_start[c];
		const uint32_t own_end = own_start + cell_count[c];
		for (uint32_t self = own_start; self < own_end; ++self) {
			if (s_query[self] == 0) {
				continue; // not due: keeps its cached steering (already in its slot)
			}
			++queried;
			const float px = mates[self].x;
			const float py = mates[self].y;
			const uint32_t ordinal = mates[self].ordinal;
			uint32_t taken = 0;
			uint32_t near_count = 0;
			int64_t visited = 0;
			uint32_t heading_count = 0;
			float ox = 0.0f, oy = 0.0f, hx = 0.0f, hy = 0.0f, sx = 0.0f, sy = 0.0f;
			bool full = true;
			// One flockmate: the rules' sums; jumps to `filled` at the cap.
#define FLOCK_VISIT(INDEX)                                                                                             \
	{                                                                                                                  \
		const Mate &mate = mates[(INDEX)];                                                                             \
		++visited;                                                                                                     \
		const float dx = mate.x - px;                                                                                  \
		const float dy = mate.y - py;                                                                                  \
		const float d2 = dx * dx + dy * dy;                                                                            \
		if (d2 < reach_sq) {                                                                                           \
			++taken;                                                                                                   \
			if (d2 < near_sq) {                                                                                        \
				++near_count;                                                                                          \
				ox += dx;                                                                                              \
				oy += dy;                                                                                              \
				heading_count += mate.has_heading;                                                                     \
				hx += mate.hx;                                                                                         \
				hy += mate.hy;                                                                                         \
			}                                                                                                          \
			if (d2 < sep_sq) {                                                                                         \
				if (d2 > (float)COINCIDENT_SQ) {                                                                       \
					/* (p - q) / d * (1 - d / range) = (p - q) * (1/d - 1/range) */                                    \
					const float w = inv_sqrt2d(d2) - inv_sep;                                                          \
					sx -= dx * w;                                                                                      \
					sy -= dy * w;                                                                                      \
				} else {                                                                                               \
					/* stacked: part along the pair's own direction, opposite for the two */                           \
					const uint32_t other = mate.ordinal;                                                               \
					const uint32_t pick = mix32(MIN(ordinal, other) * 0x9e3779b1u ^ mix32(MAX(ordinal, other))) & 63u; \
					const float side = ordinal < other ? 1.0f : -1.0f;                                                 \
					sx += side * dirs.x[pick];                                                                         \
					sy += side * dirs.y[pick];                                                                         \
				}                                                                                                      \
			}                                                                                                          \
			if (taken >= cap) {                                                                                        \
				goto filled;                                                                                           \
			}                                                                                                          \
		}                                                                                                              \
	}
			// Own cell first, from the next member on, wrapping (skips
			// itself): stacked bullets read different flockmates.
			for (uint32_t j = self + 1; j < own_end; ++j) {
				FLOCK_VISIT(j)
			}
			for (uint32_t j = own_start; j < self; ++j) {
				FLOCK_VISIT(j)
			}
			{
				// Then the 8-ring and the 16-ring: the starting cell and each
				// cell's starting flockmate rotate with the bullet (the offset
				// is a multiply-shift of a per-bullet hash, no division), so a
				// capped scan has no drift bias.
				const uint64_t spin = mix32(ordinal * 0x9e3779b1u + 0x632be5abu);
				for (int t = 0; t < 24; ++t) {
					const int k = t < 8 ? (int)((ordinal + (uint32_t)t) & 7u) : 8 + (int)((ordinal + (uint32_t)t) & 15u);
					const uint32_t count = ring_count[k];
					if (count == 0) {
						continue;
					}
					const uint32_t start = ring_start[k];
					const uint32_t offset = start + (uint32_t)((spin * (uint64_t)count) >> 32);
					const uint32_t end = start + count;
					for (uint32_t j = offset; j < end; ++j) {
						FLOCK_VISIT(j)
					}
					for (uint32_t j = start; j < offset; ++j) {
						FLOCK_VISIT(j)
					}
				}
			}
#undef FLOCK_VISIT
			full = false;
		filled:
			checks += visited;
			if (full) {
				++capped;
			}

			double vx = g.separation * (double)sx;
			double vy = g.separation * (double)sy;
			if (heading_count > 0) {
				const double inv = 1.0 / (double)heading_count;
				vx += g.alignment * (double)hx * inv;
				vy += g.alignment * (double)hy * inv;
			}
			if (near_count > 0) {
				const double inv = g.inv_neighbor_range / (double)near_count;
				vx += g.cohesion * (double)ox * inv;
				vy += g.cohesion * (double)oy * inv;
			}
			const double v2 = vx * vx + vy * vy;
			if (!std::isfinite(v2)) {
				vx = 0.0;
				vy = 0.0;
			} else if (v2 > MAX_STEER * MAX_STEER) {
				const double scale = MAX_STEER / std::sqrt(v2);
				vx *= scale;
				vy *= scale;
			}
			const uint32_t slot = s_slot[self];
			steer[slot] = Vector2((real_t)vx, (real_t)vy);
			neighbors[slot] = taken;
		}
	}
	stats.neighbor_checks = checks;
	stats.capped_members = capped;
	stats.queried_members = queried;
	// Interval flocks: every bullet's steering back into its volley's cache
	// (a bullet outside the grid this pass computes again when it returns).
	for (const CachedVolley &cached : cached_volleys) {
		FlockSteerCache2D *cache = cached.volley->flock_cache.data();
		for (uint32_t i = 0; i < cached.amount; ++i) {
			const uint32_t slot = cached.base + i;
			cache[i].valid = gathered[slot] != 0;
			cache[i].steer = steer[slot];
			cache[i].neighbors = neighbors[slot];
		}
	}
}

} //namespace BlastBullets2D
