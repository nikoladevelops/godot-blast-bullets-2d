#include "data/bullet_split_data2d.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

TypedArray<BulletSplitStage2D> BulletSplitData2D::get_stages() const {
	return stages;
}

void BulletSplitData2D::set_stages(const TypedArray<BulletSplitStage2D> &value) {
	// Empty entries are allowed (the inspector adds one before the user picks
	// a resource): that generation simply does not split.
	stages = value;
	++BulletSplitStage2D::rules_epoch;
	emit_changed();
}

bool BulletSplitData2D::get_repeat_last_stage() const {
	return repeat_last_stage;
}

void BulletSplitData2D::set_repeat_last_stage(bool value) {
	if (repeat_last_stage == value) {
		return;
	}
	repeat_last_stage = value;
	notify_property_list_changed();
	++BulletSplitStage2D::rules_epoch;
	emit_changed();
}

int BulletSplitData2D::get_max_depth() const {
	return max_depth;
}

void BulletSplitData2D::set_max_depth(int value) {
	if (value < 1 || value > MAX_DEPTH_LIMIT) {
		UtilityFunctions::push_error("BulletSplitData2D: max_depth must be between 1 and 64, keeping the old value.");
		return;
	}
	if (max_depth == value) {
		return;
	}
	max_depth = value;
	++BulletSplitStage2D::rules_epoch;
	emit_changed();
}

Ref<BulletSplitStage2D> BulletSplitData2D::get_stage_for_depth(int depth) const {
	const int64_t size = stages.size();
	if (depth < 0 || size == 0) {
		return Ref<BulletSplitStage2D>();
	}
	int64_t index = depth;
	if (index >= size) {
		if (!repeat_last_stage || depth >= max_depth) {
			return Ref<BulletSplitStage2D>();
		}
		index = size - 1;
	}
	// cast_to: an untyped script array may hold anything.
	return Ref<BulletSplitStage2D>(Object::cast_to<BulletSplitStage2D>((Object *)stages[index]));
}

int BulletSplitData2D::get_generation_count() const {
	const int size = (int)stages.size();
	if (repeat_last_stage && size > 0) {
		return Math::max(size, max_depth);
	}
	return size;
}

namespace {

Ref<BulletSplitStage2D> make_stage(int count, BulletSplitStage2D::DirectionMode mode, real_t spread, real_t speed_multiplier, real_t scale) {
	Ref<BulletSplitStage2D> stage;
	stage.instantiate();
	stage->count = count;
	stage->direction_mode = mode;
	stage->spread_degrees = spread;
	stage->speed_multiplier_min = speed_multiplier;
	stage->speed_multiplier_max = speed_multiplier;
	stage->scale_multiplier = scale;
	return stage;
}

} // namespace

void BulletSplitData2D::apply_preset(Preset preset) {
	TypedArray<BulletSplitStage2D> chain;
	switch (preset) {
		case PRESET_BURST_RING:
			chain.push_back(make_stage(8, BulletSplitStage2D::DIRECTION_RING, 60.0, 1.0, 1.0));
			break;
		case PRESET_FAN_FORWARD:
			chain.push_back(make_stage(3, BulletSplitStage2D::DIRECTION_FAN, 40.0, 1.2, 1.0));
			break;
		case PRESET_CASCADE:
			chain.push_back(make_stage(6, BulletSplitStage2D::DIRECTION_RING, 60.0, 1.0, 0.8));
			chain.push_back(make_stage(3, BulletSplitStage2D::DIRECTION_FAN, 50.0, 0.8, 0.7));
			break;
		default:
			UtilityFunctions::push_error("BulletSplitData2D: preset must be 0 (Burst Ring), 1 (Fan Forward) or 2 (Cascade), keeping the old values.");
			return;
	}
	stages = chain;
	repeat_last_stage = false;
	notify_property_list_changed();
	++BulletSplitStage2D::rules_epoch;
	emit_changed();
}

Ref<BulletSplitData2D> BulletSplitData2D::create_preset(Preset preset) {
	if (preset < PRESET_BURST_RING || preset > PRESET_CASCADE) {
		UtilityFunctions::push_error("BulletSplitData2D.create_preset: preset must be 0 (Burst Ring), 1 (Fan Forward) or 2 (Cascade), nothing created.");
		return Ref<BulletSplitData2D>();
	}
	Ref<BulletSplitData2D> data;
	data.instantiate();
	data->apply_preset(preset);
	return data;
}

void BulletSplitData2D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == StringName("max_depth") && !repeat_last_stage) {
		p_property.usage &= ~PROPERTY_USAGE_EDITOR;
	}
}

void BulletSplitData2D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_stages"), &BulletSplitData2D::get_stages);
	ClassDB::bind_method(D_METHOD("set_stages", "value"), &BulletSplitData2D::set_stages);
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "stages", PROPERTY_HINT_TYPE_STRING, String::num_int64(Variant::OBJECT) + "/" + String::num_int64(PROPERTY_HINT_RESOURCE_TYPE) + ":BulletSplitStage2D"), "set_stages", "get_stages");

	ClassDB::bind_method(D_METHOD("get_repeat_last_stage"), &BulletSplitData2D::get_repeat_last_stage);
	ClassDB::bind_method(D_METHOD("set_repeat_last_stage", "value"), &BulletSplitData2D::set_repeat_last_stage);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "repeat_last_stage"), "set_repeat_last_stage", "get_repeat_last_stage");

	ClassDB::bind_method(D_METHOD("get_max_depth"), &BulletSplitData2D::get_max_depth);
	ClassDB::bind_method(D_METHOD("set_max_depth", "value"), &BulletSplitData2D::set_max_depth);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_depth", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_depth", "get_max_depth");

	ClassDB::bind_method(D_METHOD("get_stage_for_depth", "depth"), &BulletSplitData2D::get_stage_for_depth);
	ClassDB::bind_method(D_METHOD("get_generation_count"), &BulletSplitData2D::get_generation_count);
	ClassDB::bind_method(D_METHOD("apply_preset", "preset"), &BulletSplitData2D::apply_preset);
	ClassDB::bind_static_method("BulletSplitData2D", D_METHOD("create_preset", "preset"), &BulletSplitData2D::create_preset);

	BIND_CONSTANT(MAX_DEPTH_LIMIT);
	BIND_ENUM_CONSTANT(PRESET_BURST_RING);
	BIND_ENUM_CONSTANT(PRESET_FAN_FORWARD);
	BIND_ENUM_CONSTANT(PRESET_CASCADE);
}
} //namespace BlastBullets2D
