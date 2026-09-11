#pragma once

#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace BlastBullets2D {
using namespace godot;

class BulletAttachment2D;

class BulletAttachmentObjectPool2D {
public:
	// Reserved bucket for null scenes. Unreachable through the public API
	// (every entry point rejects null scenes first) - purely defensive so a
	// missing key can never arise; such attachments stay mutually reusable.
	static constexpr uint32_t DEFAULT_BUCKET_KEY = 0xFFFFFFFEu;

	// Derives the pool bucket key for a scene, so callers never pass ids.
	// Same resource path -> same key no matter which Ref/script loaded it.
	// In-memory scenes (empty path) get a stable per-Ref key instead: safe
	// (never shares a bucket across scenes) but without cross-Ref pooling.
	// Null scenes map to DEFAULT_BUCKET_KEY. Never returns 0. Not bound.
	static uint32_t make_pooling_key_for_scene(const Ref<PackedScene> &scene);

	// Same as make_pooling_key_for_scene, but also records the human-readable
	// label used by debug output. Must only be called for scenes that are
	// already validated (or need no validation, e.g. freeing) - validation
	// itself must go through note_key_label() after a successful type check,
	// otherwise an invalid scene would be remembered as recognized. Not bound.
	uint32_t key_for_scene(const Ref<PackedScene> &scene);

	// Records a key as recognized (validated). First label wins. Not bound.
	void note_key_label(uint32_t pooling_id, const String &label);

	// Builds the debug label for a scene without recording anything. Not bound.
	static String make_key_label_for_scene(const Ref<PackedScene> &scene);

	// Whether this key was validated before (label recorded). Survives empty
	// queues and frees on purpose: recognition is about the scene type, not
	// about currently pooled instances. Not bound.
	bool is_key_recognized(uint32_t pooling_id) const;

	// Human-readable label for a key (scene path or "<in-memory>"), "" if unknown. Not bound.
	String get_key_label(uint32_t pooling_id) const;

	// Add a new bullet attachment to the pool
	void push(BulletAttachment2D *bullet_attachment, uint32_t pooling_id);

	// Retrieve a bullet attachment
	BulletAttachment2D *pop(uint32_t pooling_id);

	// Removes one instance without freeing it. Used by the attachment PREDELETE hook
	// when users free pooled attachments by hand. Returns false when not found.
	bool remove_instance(BulletAttachment2D *target, uint32_t pooling_id);

	// Drops pool tracking flags without freeing. Called once from factory teardown so
	// later attachment PREDELETEs never touch this pool object again.
	void detach_all();

	// Free memory by deleting all bullet attachments that are in the pool
	void free_all_bullet_attachments();

	// Free memory by deleting bullet attachments, but only those that have a specific pooling_id
	void free_specific_bullet_attachments(uint32_t pooling_id);

	// Gets the total amount of attachments currently in the object pool
	int get_total_amount_pooled();

	// Gets a map containing the pooling_id as KEY and the attachment amount as VALUE
	std::map<uint32_t, int> get_pool_info();

private:
	// Keeps all pooled BulletAttachment2D pointers. The key is derived from the
	// PackedScene (see key_for_scene), so different instances of the same scene
	// land in the same queue and get reused - this makes re-usability of same
	// scene BulletAttachment2D nodes possible even though they are different instances
	std::unordered_map<uint32_t, std::queue<BulletAttachment2D *>> pool;
	// Human-readable label per key (scene path), for debug output only.
	std::unordered_map<uint32_t, String> key_labels;
};
} //namespace BlastBullets2D
