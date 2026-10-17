// BulletSpawner2D (split from bullet_spawner2d.cpp, same class): the
// feature explainer. One plain sentence per steering group (what it will
// do with the current settings, why it would do nothing, which features
// fight), then the setup warnings. Debug only: nothing here runs per frame.

#include "bullet_spawner/bullet_spawner2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

namespace {

const char *homing_source_name2d(int source) {
	static const char *names[] = { "Node Group", "Mouse", "Global Position", "Node Path", "Node Name", "Node Children" };
	return source >= 0 && source < 6 ? names[source] : "?";
}

const char *graze_source_name2d(int source) {
	static const char *names[] = { "Node Group", "Node Path", "Node Name", "Node Children", "Mouse", "Global Positions" };
	return source >= 0 && source < 6 ? names[source] : "?";
}

const char *flock_preset_name2d(int preset) {
	static const char *names[] = { "Custom", "Swarm", "Fly Together", "Keep Distance", "Tight Group" };
	return preset >= 0 && preset < 5 ? names[preset] : "?";
}

const char *flock_scope_name2d(int scope) {
	static const char *names[] = { "Per Spawner", "Shared", "Per Volley" };
	return scope >= 0 && scope < 3 ? names[scope] : "?";
}

} // namespace

PackedStringArray BulletSpawner2D::debug_explain_features() const {
	PackedStringArray out;
	const bool rotation_data = spawn_data.is_valid() && (spawn_data->shared_bullet_rotation_data.is_valid() || !spawn_data->all_bullet_rotation_data.is_empty());
	const bool random_size = bullet_scale_mode != BULLET_SCALE_OFF;
	int homing_targets = 0;

	// Homing.
	if (!homing_enabled) {
		out.push_back("Homing: off.");
	} else {
		homing_targets = resolve_homing_targets(true, false).size();
		String line = String("Homing: on, source ") + homing_source_name2d(homing_target_source) + ", " + itos(homing_targets) + " target(s) found now. ";
		if (homing_targets == 0) {
			line += "Bullets fly straight until a target appears (check the source's setting: an empty group, name or path finds nothing). ";
		}
		if (homing_smoothing <= 0.0) {
			line += "homing_smoothing 0: bullets snap onto the target every tick. ";
		} else {
			line += "Turns at " + String::num(homing_smoothing, 2) + " rad/s (homing_smoothing). ";
		}
		if (!homing_take_control_of_texture_rotation && !rotation_data) {
			line += "homing_take_control_of_texture_rotation is off: homing cannot steer these bullets. ";
		}
		line += "Reached within " + String::num(homing_distance_before_reached, 1) + " px" + (homing_reach_counts_bullet_size ? " of each bullet's edge." : " of each bullet's center.");
		out.push_back(line.strip_edges());
	}

	// Orbiting.
	if (!orbiting_enabled) {
		out.push_back("Orbiting: off.");
	} else if (!homing_enabled) {
		out.push_back("Orbiting: on but homing is off: bullets orbit their homing target, so nothing orbits. Turn on homing.");
	} else {
		out.push_back(String("Orbiting: bullets circle their homing target at ") + String::num(orbiting_radius, 1) + " px once they reach it" + (homing_targets == 0 ? " (no target found now)." : "."));
	}

	// Flocking.
	if (!flock_enabled) {
		out.push_back("Flocking: off.");
	} else {
		const Ref<BulletFlock2D> f = effective_flock();
		if (f.is_null()) {
			out.push_back("Flocking: on, empty flock slot and no BulletFactory2D to lend its default flock: nothing flocks.");
		} else {
			String line = String("Flocking: on, ") + (flock.is_valid() ? "its own BulletFlock2D" : "the factory's default flock") + " (" + flock_preset_name2d(f->get_matching_preset()) + "), scope " + flock_scope_name2d(flock_scope) + ". Turns at most " + String::num(f->max_turn_rate_degrees, 0) + " deg/s, eases in over " + String::num(f->blend_in_sec, 2) + " s.";
			if (homing_enabled) {
				line += String(" Homing turns first and the flock bends it: separation (") + String::num(f->separation, 1) + " rad/s) " + (f->separation > homing_smoothing ? "outpaces" : "is weaker than") + " homing (" + String::num(homing_smoothing, 1) + " rad/s)" + (f->separation > homing_smoothing ? ", so the swarm keeps its gaps on the way." : ": bullets bunch up at the target.");
			}
			if (orbiting_enabled) {
				line += " A bullet locked in an orbit is never turned by the flock.";
			}
			if (rotation_data && f->face_heading) {
				line += " Rotation data spins the sprites: face_heading has no effect.";
			}
			out.push_back(line);
		}
	}

	// Splitting.
	if (!split_enabled) {
		out.push_back("Splitting: off.");
	} else if (split_data.is_null() || split_data->get_generation_count() == 0) {
		out.push_back("Splitting: on, no stages: nothing splits.");
	} else {
		const Ref<BulletSplitStage2D> first = split_data->get_stage_for_depth(0);
		String line = String("Splitting: on, ") + String::num_int64(split_data->get_generation_count()) + " generation(s) split.";
		if (first.is_valid()) {
			PackedStringArray when;
			if (first->split_on_hit) {
				when.push_back("is killed by a hit");
			}
			if (first->split_on_lifetime_over) {
				when.push_back("expires");
			}
			if (first->split_after_sec > 0.0) {
				when.push_back(String("reaches ") + String::num(first->split_after_sec, 2) + " s");
			}
			if (first->split_on_despawn) {
				when.push_back("leaves the despawn bounds");
			}
			if (first->split_on_clear) {
				when.push_back("is cleared");
			}
			if (when.is_empty()) {
				line += " No trigger is on in the first stage: bullets split only through split_bullet().";
			} else {
				line += String(" A fired bullet bursts into ") + String::num_int64(first->count) + " when it " + String(", ").join(when) + ".";
			}
		}
		out.push_back(line);
	}

	// Graze.
	if (!graze_enabled) {
		out.push_back("Graze: off.");
	} else {
		int zones = 0;
		bool sized = false;
		for (int i = 0; i < graze_zones.size(); ++i) {
			const Variant entry = graze_zones[i];
			const BulletGrazeZone2D *zone = entry.get_type() == Variant::OBJECT ? Object::cast_to<BulletGrazeZone2D>((Object *)entry) : nullptr;
			if (zone != nullptr) {
				++zones;
				sized = sized || zone->count_bullet_size;
			}
		}
		if (zones == 0) {
			out.push_back("Graze: on but graze_zones is empty: nothing grazes. Add a BulletGrazeZone2D.");
		} else {
			const int targets = resolve_graze_targets().size();
			out.push_back(String("Graze: on, ") + itos(zones) + " zone(s), source " + graze_source_name2d(graze_target_source) + ", " + itos(targets) + " target(s) found now" + (targets == 0 ? " (nothing grazes until one appears)" : "") + (sized ? "; rings grow by each bullet's own size." : "; rings measured from bullet centers."));
		}
	}

	if (random_size) {
		out.push_back(String("Randomization: bullet sizes vary (") + String::num(bullet_scale_min, 2) + " to " + String::num(bullet_scale_max, 2) + "): the flock (count_bullet_size), graze zones (count_bullet_size) and homing reach (homing_reach_counts_bullet_size) count each bullet's own size.");
	}
	for (const String &warning : get_setup_warnings()) {
		out.push_back("Warning: " + warning);
	}
	return out;
}

} // namespace BlastBullets2D
