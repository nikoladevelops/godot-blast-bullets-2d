// The factory's flock pass (boids): gather, exact spatial hash grid, one
// steering vector per bullet. See flock_solver2d.hpp for the contract; the
// volley applies the result in step_flock (bullet_volley2d_tick.cpp).

#include "factory/flock_solver2d.hpp"

#include "bullet_volley/bullet_volley2d.hpp"

#include <godot_cpp/core/math.hpp>

#include <cmath>
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

// Scan order of the neighbor cells around a member's own cell.
constexpr int RING1_DX[8] = { 1, 1, 0, -1, -1, -1, 0, 1 };
constexpr int RING1_DY[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
constexpr int RING2_DX[16] = { 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1, 0, 1, 2, 2 };
constexpr int RING2_DY[16] = { 0, 1, 2, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -2, -1 };

// 64 unit directions for stacked pairs (no trig per pair).
struct CoincidentDirections {
	double x[64];
	double y[64];
	CoincidentDirections() {
		for (int k = 0; k < 64; ++k) {
			const double angle = ((double)k + 0.5) * Math::TAU / 64.0;
			x[k] = std::cos(angle);
			y[k] = std::sin(angle);
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

inline bool finite_float(float value) {
	return std::isfinite(value);
}

} // namespace

void FlockSolver2D::begin(uint64_t new_sweep) {
	sweep = new_sweep;
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
	m_slot.clear();
	m_ordinal.clear();
	m_cell.clear();
	m_x.clear();
	m_y.clear();
	m_hx.clear();
	m_hy.clear();
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

	const int amount = MAX(volley.amount_bullets, 0);
	const uint32_t base = (uint32_t)steer.size();
	steer.resize((size_t)base + (size_t)amount);
	neighbors.resize((size_t)base + (size_t)amount);
	stats.members += amount;

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
		m_slot.push_back(base + (uint32_t)i);
		m_ordinal.push_back(groups[group].next_ordinal++);
		m_cell.push_back(cell_for(group, (int32_t)std::floor(fx), (int32_t)std::floor(fy)));
		m_x.push_back(x);
		m_y.push_back(y);
		m_hx.push_back((float)hx);
		m_hy.push_back((float)hy);
	}

	FlockView2D &view = volley.flock_view;
	view.base = base;
	view.sweep = sweep;
	view.life = volley.volley_generation;
	view.serial = config.serial;
	view.max_turn_rad = (real_t)Math::deg_to_rad((double)flock->max_turn_rate_degrees);
	view.face_heading = flock->face_heading;
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
	s_ordinal.resize(total);
	s_x.resize(total);
	s_y.resize(total);
	s_hx.resize(total);
	s_hy.resize(total);
	sort_cursor.assign(cell_start.begin(), cell_start.end());
	for (uint32_t m = 0; m < total; ++m) {
		const uint32_t at = sort_cursor[m_cell[m]]++;
		s_slot[at] = m_slot[m];
		s_ordinal[at] = m_ordinal[m];
		s_x[at] = m_x[m];
		s_y[at] = m_y[m];
		s_hx[at] = m_hx[m];
		s_hy[at] = m_hy[m];
	}

	const CoincidentDirections &dirs = coincident_directions();
	int64_t checks = 0;
	int capped = 0;
	uint32_t ring1_start[8], ring1_count[8], ring2_start[16], ring2_count[16];
	for (uint32_t c = 0; c < cell_total; ++c) {
		const uint32_t group = cell_group[c];
		const Group &g = groups[group];
		const int32_t cx = cell_cx[c];
		const int32_t cy = cell_cy[c];
		for (int k = 0; k < 8; ++k) {
			const int64_t id = find_cell(group, cx + RING1_DX[k], cy + RING1_DY[k]);
			ring1_start[k] = id >= 0 ? cell_start[id] : 0;
			ring1_count[k] = id >= 0 ? cell_count[id] : 0;
		}
		for (int k = 0; k < 16; ++k) {
			const int64_t id = find_cell(group, cx + RING2_DX[k], cy + RING2_DY[k]);
			ring2_start[k] = id >= 0 ? cell_start[id] : 0;
			ring2_count[k] = id >= 0 ? cell_count[id] : 0;
		}
		const uint32_t own_start = cell_start[c];
		const uint32_t own_count = cell_count[c];
		for (uint32_t q = 0; q < own_count; ++q) {
			const uint32_t self = own_start + q;
			const double px = (double)s_x[self];
			const double py = (double)s_y[self];
			const uint32_t ordinal = s_ordinal[self];
			uint32_t accepted = 0;
			uint32_t near_count = 0;
			uint32_t heading_count = 0;
			double sum_hx = 0.0, sum_hy = 0.0;
			double sum_ox = 0.0, sum_oy = 0.0;
			double sep_x = 0.0, sep_y = 0.0;
			// One flockmate; true = the cap is reached (stop scanning).
			auto consider = [&](uint32_t j) -> bool {
				++checks;
				const double dx = (double)s_x[j] - px;
				const double dy = (double)s_y[j] - py;
				const double d2 = dx * dx + dy * dy;
				if (!(d2 < g.reach_sq)) {
					return false;
				}
				++accepted;
				if (d2 < g.neighbor_range_sq) {
					++near_count;
					sum_ox += dx;
					sum_oy += dy;
					const float hx = s_hx[j];
					const float hy = s_hy[j];
					if (hx != 0.0f || hy != 0.0f) {
						++heading_count;
						sum_hx += (double)hx;
						sum_hy += (double)hy;
					}
				}
				if (d2 < g.separation_range_sq) {
					if (d2 > COINCIDENT_SQ) {
						const double d = std::sqrt(d2);
						const double w = (1.0 - d * g.inv_separation_range) / d;
						sep_x -= dx * w;
						sep_y -= dy * w;
					} else {
						// Stacked: part along the pair's own direction,
						// opposite for its two bullets.
						const uint32_t other = s_ordinal[j];
						const uint32_t lo = MIN(ordinal, other);
						const uint32_t hi = MAX(ordinal, other);
						const uint32_t pick = mix32(lo * 0x9e3779b1u ^ mix32(hi)) & 63u;
						const double side = ordinal < other ? 1.0 : -1.0;
						sep_x += side * dirs.x[pick];
						sep_y += side * dirs.y[pick];
					}
				}
				return accepted >= g.max_neighbors;
			};
			// Scans [start, start + count) from a rotated offset.
			auto scan = [&](uint32_t start, uint32_t count, uint32_t offset) -> bool {
				for (uint32_t u = offset; u < count; ++u) {
					if (consider(start + u)) {
						return true;
					}
				}
				for (uint32_t u = 0; u < offset; ++u) {
					if (consider(start + u)) {
						return true;
					}
				}
				return false;
			};
			bool full = false;
			// Own cell: from the next member on, wrapping (skips itself).
			for (uint32_t u = q + 1; u < own_count && !full; ++u) {
				full = consider(own_start + u);
			}
			for (uint32_t u = 0; u < q && !full; ++u) {
				full = consider(own_start + u);
			}
			for (int t = 0; t < 8 && !full; ++t) {
				const int k = (int)((ordinal + (uint32_t)t) & 7u);
				if (ring1_count[k] > 0) {
					full = scan(ring1_start[k], ring1_count[k], ordinal % ring1_count[k]);
				}
			}
			for (int t = 0; t < 16 && !full; ++t) {
				const int k = (int)((ordinal + (uint32_t)t) & 15u);
				if (ring2_count[k] > 0) {
					full = scan(ring2_start[k], ring2_count[k], ordinal % ring2_count[k]);
				}
			}
			if (full) {
				++capped;
			}

			double vx = g.separation * sep_x;
			double vy = g.separation * sep_y;
			if (heading_count > 0) {
				const double inv = 1.0 / (double)heading_count;
				vx += g.alignment * sum_hx * inv;
				vy += g.alignment * sum_hy * inv;
			}
			if (near_count > 0) {
				const double inv = g.inv_neighbor_range / (double)near_count;
				vx += g.cohesion * sum_ox * inv;
				vy += g.cohesion * sum_oy * inv;
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
			neighbors[slot] = accepted;
		}
	}
	stats.neighbor_checks = checks;
	stats.capped_members = capped;
}

} //namespace BlastBullets2D
