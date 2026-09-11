#include "./bullet_attachment_object_pool2d.hpp"
#include "./bullet_attachment2d.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

uint32_t BulletAttachmentObjectPool2D::make_pooling_key_for_scene(const Ref<PackedScene> &scene) {
	if (scene.is_null()) {
		return DEFAULT_BUCKET_KEY;
	}
	const String path = scene->get_path();
	if (!path.is_empty()) {
		// FNV-1a over the UTF-8 bytes: deterministic within and across runs,
		// so every loader of the same file lands in the same bucket.
		const PackedByteArray bytes = path.to_utf8_buffer();
		uint32_t hash = 2166136261u;
		for (int i = 0; i < bytes.size(); ++i) {
			hash ^= static_cast<uint8_t>(bytes[i]);
			hash *= 16777619u;
		}
		if (hash == 0 || hash == DEFAULT_BUCKET_KEY) {
			return 1u; // 0 stays reserved for "no key"
		}
		return hash;
	}
	// Pathless (in-memory / duplicated) scene: fold the instance id. Instance
	// ids are unique per Ref within a session (and the pool is runtime-only),
	// so this Ref always maps to its own private bucket - safe, just without
	// cross-Ref sharing.
	const uint64_t instance_id = (uint64_t)scene->get_instance_id();
	const uint32_t folded = (uint32_t)(instance_id ^ (instance_id >> 32));
	if (folded == 0 || folded == DEFAULT_BUCKET_KEY) {
		return 0x9E3779B9u;
	}
	return folded;
}

String BulletAttachmentObjectPool2D::make_key_label_for_scene(const Ref<PackedScene> &scene) {
	if (scene.is_null()) {
		return String("<unknown>");
	}
	const String path = scene->get_path();
	return path.is_empty() ? String("<in-memory>") : path;
}

void BulletAttachmentObjectPool2D::note_key_label(uint32_t pooling_id, const String &label) {
	if (pooling_id == 0 || label.is_empty()) {
		return;
	}
	if (key_labels.find(pooling_id) == key_labels.end()) {
		key_labels[pooling_id] = label;
	}
}

uint32_t BulletAttachmentObjectPool2D::key_for_scene(const Ref<PackedScene> &scene) {
	const uint32_t key = make_pooling_key_for_scene(scene);
	note_key_label(key, make_key_label_for_scene(scene));
	return key;
}

bool BulletAttachmentObjectPool2D::is_key_recognized(uint32_t pooling_id) const {
	return key_labels.find(pooling_id) != key_labels.end();
}

String BulletAttachmentObjectPool2D::get_key_label(uint32_t pooling_id) const {
	auto it = key_labels.find(pooling_id);
	if (it == key_labels.end()) {
		return String();
	}
	return it->second;
}

void BulletAttachmentObjectPool2D::push(BulletAttachment2D *bullet_attachment, uint32_t pooling_id) {
	if (bullet_attachment == nullptr) {
		UtilityFunctions::push_error("BulletAttachmentObjectPool2D::push got a null attachment, ignoring.");
		return;
	}
	if (bullet_attachment->is_pooled) {
		if (bullet_attachment->home_pool == this && bullet_attachment->home_pooling_id == pooling_id) {
			UtilityFunctions::push_error("BulletAttachmentObjectPool2D::push got a duplicate attachment, ignoring to avoid double-free.");
			return;
		}
		// Stale flag (e.g. manually reparented without pop): drop the old queue
		// entry first so the same pointer can't sit in two queues at once.
		if (bullet_attachment->home_pool != nullptr) {
			bullet_attachment->home_pool->remove_instance(bullet_attachment, bullet_attachment->home_pooling_id);
		}
	}
	std::queue<BulletAttachment2D *> &queue = pool[pooling_id];
	bullet_attachment->home_pool = this;
	bullet_attachment->home_pooling_id = pooling_id;
	bullet_attachment->is_pooled = true;
	queue.push(bullet_attachment);
}

BulletAttachment2D *BulletAttachmentObjectPool2D::pop(uint32_t pooling_id) {
	auto result = pool.find(pooling_id);

	// If the pool doesn't contain a queue with that key or if it does but the queue is empty return a nullptr
	if (result == pool.end() || result->second.size() == 0) {
		return nullptr;
	}

	// Get the first live BulletAttachment2D pointer in the queue, skipping entries
	// whose nodes were freed without PREDELETE cleanup (e.g. scene reload).
	BulletAttachment2D *found_attachment = nullptr;
	while (!result->second.empty()) {
		BulletAttachment2D *candidate = result->second.front();
		result->second.pop();
		if (candidate == nullptr || candidate->is_queued_for_deletion()) {
			if (candidate != nullptr) {
				candidate->home_pool = nullptr;
				candidate->is_pooled = false;
			}
			continue;
		}
		found_attachment = candidate;
		break;
	}

	if (result->second.empty()) {
		pool.erase(result);
	}

	if (found_attachment == nullptr) {
		return nullptr;
	}

	found_attachment->home_pool = nullptr;
	found_attachment->is_pooled = false;

	return found_attachment;
}

bool BulletAttachmentObjectPool2D::remove_instance(BulletAttachment2D *target, uint32_t pooling_id) {
	if (target == nullptr) {
		return false;
	}
	auto it = pool.find(pooling_id);
	if (it == pool.end() || it->second.empty()) {
		return false;
	}
	std::queue<BulletAttachment2D *> survivors;
	bool removed = false;
	while (!it->second.empty()) {
		BulletAttachment2D *front = it->second.front();
		it->second.pop();
		if (!removed && front == target) {
			removed = true;
			continue;
		}
		survivors.push(front);
	}
	if (removed) {
		target->home_pool = nullptr;
		target->is_pooled = false;
	}
	it->second.swap(survivors);
	if (it->second.empty()) {
		pool.erase(it);
	}
	return removed;
}

void BulletAttachmentObjectPool2D::detach_all() {
	for (auto &[pooling_id, queue] : pool) {
		(void)pooling_id;
		std::vector<BulletAttachment2D *> order;
		order.reserve(queue.size());
		while (!queue.empty()) {
			order.push_back(queue.front());
			queue.pop();
		}
		for (BulletAttachment2D *attachment : order) {
			if (attachment != nullptr) {
				attachment->home_pool = nullptr;
				attachment->is_pooled = false;
			}
			queue.push(attachment);
		}
	}
}

void BulletAttachmentObjectPool2D::free_all_bullet_attachments() {
	for (auto &[pooling_id, queue] : pool) {
		while (queue.empty() == false) {
			BulletAttachment2D *attachment = queue.front();
			if (attachment != nullptr) {
				attachment->home_pool = nullptr;
				attachment->is_pooled = false;
				attachment->queue_free(); // delete the attachment
			}
			queue.pop(); // remove it from the queue
		}
	}

	pool.clear();
	// Labels intentionally survive: recognition is about the scene type, not
	// about currently pooled instances, so a re-pooled scene skips re-validation.
}

void BulletAttachmentObjectPool2D::free_specific_bullet_attachments(uint32_t pooling_id) {
	// Try to find a queue that exists and holds bullet attachments with a specific pooling_id
	auto it = pool.find(pooling_id);

	// If the queue doesn't exist or if the queue is empty, then it means there's no attachments to free
	if (it == pool.end() || it->second.empty()) {
		return;
	}

	auto &queue = it->second;

	// We know the queue contains at least 1 bullet attachment, so we use a do-while loop to ensure the operation happens at least once
	do {
		BulletAttachment2D *attachment = queue.front();
		if (attachment != nullptr) {
			attachment->home_pool = nullptr;
			attachment->is_pooled = false;
			attachment->queue_free(); // free the bullet attachment
		}
		queue.pop(); // remove it from the queue
	} while (queue.empty() == false);

	pool.erase(pooling_id); // delete the queue itself since it's basically empty right now
	// Label intentionally survives (see free_all_bullet_attachments).
}

int BulletAttachmentObjectPool2D::get_total_amount_pooled() {
	int amount_attachments = 0;
	for (auto &[pooling_id, queue] : pool) {
		amount_attachments += static_cast<int>(queue.size());
	}

	return amount_attachments;
}

std::map<uint32_t, int> BulletAttachmentObjectPool2D::get_pool_info() {
	std::map<uint32_t, int> pool_info;

	for (auto &[pooling_id, queue] : pool) {
		if (!queue.empty()) {
			pool_info.emplace(pooling_id, static_cast<int>(queue.size()));
		}
	}

	return pool_info;
}
} //namespace BlastBullets2D
