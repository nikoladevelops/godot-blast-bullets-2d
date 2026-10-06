// Attachments: the BulletAttachment2D node riding on each bullet, its pooling,
// deferred disables and the slot liveness checks.

#include "bullet_volley/bullet_volley2d_internal.hpp"

using namespace godot;

namespace BlastBullets2D {

BulletAttachment2D *BulletVolley2D::bullet_get_attachment(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_get_attachment")) {
		return nullptr;
	}
	if (bullet_index >= (int)attachments.size()) {
		return nullptr;
	}
	return attachments[bullet_index];
}

void BulletVolley2D::clear_attachment_owner_fields(BulletAttachment2D *attachment) {
	if (attachment != nullptr) {
		attachment->owner_volley_id = 0;
		attachment->owner_bullet_index = -1;
	}
}

bool BulletVolley2D::slot_still_holds_attachment(int bullet_index, BulletAttachment2D *expected_attachment, uint64_t expected_attachment_id, uint64_t expected_attachment_epoch) const {
	if (expected_attachment == nullptr || expected_attachment_id == 0) {
		return false;
	}
	if (ObjectDB::get_instance(ObjectID(expected_attachment_id)) != expected_attachment) {
		return false;
	}
	if (bullet_index < 0 || bullet_index >= (int)attachments.size() || attachments[bullet_index] != expected_attachment) {
		return false;
	}
	return attachment_epoch_for(bullet_index) == expected_attachment_epoch;
}

bool BulletVolley2D::slot_still_holds_attachment_id(int bullet_index, uint64_t expected_attachment_id, uint64_t expected_attachment_epoch) const {
	if (expected_attachment_id == 0 || bullet_index < 0 || bullet_index >= (int)attachments.size()) {
		return false;
	}
	BulletAttachment2D *slot = attachments[bullet_index];
	if (slot == nullptr) {
		return false;
	}
	return slot_still_holds_attachment(bullet_index, slot, expected_attachment_id, expected_attachment_epoch);
}

bool BulletVolley2D::is_popped_attachment_from_scene(BulletAttachment2D *candidate, const Ref<PackedScene> &expected_scene, uint32_t expected_pooling_id) {
	if (candidate == nullptr) {
		return false;
	}
	if (expected_scene.is_valid()) {
		return candidate->source_scene == expected_scene ||
				(candidate->source_scene.is_valid() &&
						!candidate->source_scene->get_path().is_empty() && !expected_scene->get_path().is_empty() &&
						candidate->source_scene->get_path() == expected_scene->get_path());
	}
	return candidate->source_scene.is_valid() &&
			BulletAttachmentObjectPool2D::make_pooling_key_for_scene(candidate->source_scene) == expected_pooling_id;
}

BulletAttachment2D *BulletVolley2D::bullet_set_attachment_to_null(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_set_attachment_to_null")) {
		return nullptr;
	}
	if (bullet_index >= (int)attachments.size()) {
		return nullptr;
	}

	auto &curr_attachment = attachments[bullet_index];
	auto temp = curr_attachment;

	// Detached without being disabled: clear owner tracking so its PREDELETE
	// doesn't try to drop this (now stale) slot.
	clear_attachment_owner_fields(temp);

	curr_attachment = nullptr;
	bump_attachment_epoch(bullet_index);
	return temp;
}

void BulletVolley2D::_do_drop_attachment_slot_if_matches(int bullet_index, BulletAttachment2D *attachment) {
	if (bullet_index < 0 || bullet_index >= (int)attachments.size()) {
		return;
	}
	if (attachments[bullet_index] == attachment) {
		attachments[bullet_index] = nullptr;
		bump_attachment_epoch(bullet_index);
	}
}

TypedArray<BulletAttachment2D> BulletVolley2D::all_bullets_get_attachments(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_get_attachments");

	TypedArray<BulletAttachment2D> arr;

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_get_attachment(i));
	}

	return arr;
}

TypedArray<BulletAttachment2D> BulletVolley2D::all_bullets_set_attachment_to_null(int bullet_index_start, int bullet_index_end_inclusive) {
	ensure_indexes_match_amount_bullets_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_attachment_to_null");

	TypedArray<BulletAttachment2D> arr;

	for (int i = bullet_index_start; i <= bullet_index_end_inclusive; ++i) {
		arr.push_back(bullet_set_attachment_to_null(i));
	}

	return arr;
}

void BulletVolley2D::all_bullets_set_attachment(const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet, int bullet_index_start, int bullet_index_end_inclusive) {
	for_range(bullet_index_start, bullet_index_end_inclusive, "all_bullets_set_attachment", [&](int i) { bullet_set_attachment(i, attachment_scene, bullet_attachment_offset, stick_relative_to_bullet); });
}

void BulletVolley2D::bullet_set_attachment(int bullet_index, const Ref<PackedScene> &attachment_scene, const Vector2 &bullet_attachment_offset, bool stick_relative_to_bullet) {
	if (!validate_bullet_index(bullet_index, "bullet_set_attachment")) {
		return;
	}

	attach_bullet_attachment_internal(bullet_index, attachment_scene, bullet_attachment_offset, stick_relative_to_bullet);
}

void BulletVolley2D::bullet_free_attachment(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_free_attachment")) {
		return;
	}

	BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

	if (attachment_ptr == nullptr) {
		return;
	}
	auto temp = attachment_ptr;
	attachment_ptr = nullptr;
	bump_attachment_epoch(bullet_index);

	// Being freed outright: clear owner tracking first so its PREDELETE skip
	// path can't race with this deletion.
	clear_attachment_owner_fields(temp);

	// queue_free, never memdelete: this is script-callable, and an
	// immediate delete from inside the attachment's own _process or a
	// signal it emitted would free the node under the caller's feet.
	// The slot is already empty and the owner fields cleared, so the
	// node is fully detached from this volley; hide it so the frame
	// until the flush shows nothing.
	if (!temp->is_queued_for_deletion()) {
		temp->set_visible(false);
		temp->queue_free();
	}
}

void BulletVolley2D::bullet_disable_attachment(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_disable_attachment")) {
		return;
	}

	BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

	if (attachment_ptr == nullptr) {
		return;
	}

	// Detach FIRST, then run the script callback: on_bullet_disable runs user
	// code, and with the slot still holding the pointer a handler that re-enters
	// (bullet_set_attachment / bullet_disable_attachment / disable_bullet on the
	// same index) would recurse infinitely or double-pool the attachment. After
	// the null, any re-entrant call on this slot sees a clean, empty slot.
	BulletAttachment2D *detaching = attachment_ptr;
	attachment_ptr = nullptr;
	bump_attachment_epoch(bullet_index);

	// Owner tracking cleared before the callback: if the handler frees the
	// attachment itself, its PREDELETE hook then finds nothing left to do.
	clear_attachment_owner_fields(detaching);

	// A suspended attachment already heard on_bullet_disable when its bullet
	// froze: releasing it now must not tell it twice.
	const bool was_suspended = bullet_index < (int)attachment_suspended.size() && attachment_suspended[bullet_index];
	if (bullet_index < (int)attachment_suspended.size()) {
		attachment_suspended[bullet_index] = 0;
	}
	const uint64_t detaching_id = detaching->get_instance_id();
	if (!was_suspended) {
		detaching->call_on_bullet_disable();
	}

	// The callback may have freed the attachment itself (immediate free()).
	// Everything below touches detaching, so revalidate first; the slot is
	// already null, so there is nothing left to clean up when it is gone.
	if (ObjectDB::get_instance(ObjectID(detaching_id)) == nullptr) {
		return;
	}

	if (bullet_factory == nullptr) {
		UtilityFunctions::push_error("bullet_disable_attachment: multimesh was never spawned through BulletFactory2D.");
		return;
	}

	if (is_attachments_auto_pooling_enabled) {
		bullet_factory->bullet_attachments_pool.push(detaching, attachment_pooling_ids[bullet_index]);
	} else {
		// If the user has selected to not use auto pooling,
		// he most likely expects for the attachments to get freed by themselves when necessary
		// so do that, otherwise the scene will be spammed with hundreds of disabled attachments that never get freed (and user might not even notice this)

		detaching->queue_free();
	}
}

void BulletVolley2D::suspend_bullet_attachment(int bullet_index) {
	if (bullet_index < 0 || bullet_index >= (int)attachments.size()) {
		return;
	}
	BulletAttachment2D *attachment = attachments[bullet_index];
	if (attachment == nullptr) {
		return;
	}
	if (bullet_index < (int)attachment_suspended.size()) {
		if (attachment_suspended[bullet_index]) {
			return; // already suspended
		}
		attachment_suspended[bullet_index] = 1;
	}
	// The slot keeps the node: a callback that frees it drops the slot via
	// its own PREDELETE (owner tracking), so nothing dangles here.
	attachment->call_on_bullet_disable();
}

void BulletVolley2D::bullet_enable_attachment(int bullet_index) {
	if (!validate_bullet_index(bullet_index, "bullet_enable_attachment")) {
		return;
	}

	BulletAttachment2D *&attachment_ptr = attachments[bullet_index];

	if (attachment_ptr != nullptr) {
		if (bullet_index < (int)attachment_suspended.size()) {
			attachment_suspended[bullet_index] = 0;
		}
		attachment_ptr->call_on_bullet_enable();
	}
}

int BulletVolley2D::get_amount_active_attachments() const {
	int amount_active_attachments = 0;

	// min(): the vector is sized to amount_bullets by spawn(), but this can be
	// called on a not-yet-spawned instance through debug helpers.
	const int count = Math::min((int)attachments.size(), amount_bullets);
	for (int i = 0; i < count; ++i) {
		if (attachments[i] != nullptr) {
			++amount_active_attachments;
		}
	}

	return amount_active_attachments;
}

void BulletVolley2D::reset_attachment_state_for_reuse() {
	// Force-disable any surviving slot first. Deferred attachment disables can be
	// dropped by a generation bump (e.g. lifetime expiry pooled this instance and a
	// spawn re-enabled it before the deferred flush ran), so a new owner must never
	// be able to observe, disable or re-pool a previous owner's attachment.
	for (int i = 0; i < (int)attachments.size(); ++i) {
		if (attachments[i] != nullptr) {
			bullet_disable_attachment(i);
		}
	}

	const int count = amount_bullets;
	attachment_pooling_ids.assign(count, 0);
	attachments.assign(count, nullptr);
	// New life, new assignment history: stale deferred disables (which carry
	// the old epoch) can never match the fresh slots.
	attachment_assignment_epochs.assign(count, 0);
	attachment_transforms.assign(count, Transform2D());
	attachment_offsets.assign(count, Vector2());
	attachment_local_transforms.assign(count, Transform2D());
	attachment_stick_relative_to_bullet.assign(count, 1);
	attachment_suspended.assign(count, 0);
	all_previous_attachment_transf.assign(count, Transform2D());
}

} // namespace BlastBullets2D
