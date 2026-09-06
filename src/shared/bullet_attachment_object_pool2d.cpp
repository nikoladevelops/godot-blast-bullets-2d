#include "./bullet_attachment_object_pool2d.hpp"
#include "./bullet_attachment2d.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;
namespace BlastBullets2D {

void BulletAttachmentObjectPool2D::push(BulletAttachment2D *bullet_attachment, uint32_t pooling_id) {
	if (bullet_attachment == nullptr) {
		UtilityFunctions::push_error("BulletAttachmentObjectPool2D::push got a null attachment, ignoring.");
		return;
	}
	bullet_attachment->home_pool = this;
	bullet_attachment->home_pooling_id = pooling_id;
	bullet_attachment->is_pooled = true;
	pool[pooling_id].push(bullet_attachment);
}

BulletAttachment2D *BulletAttachmentObjectPool2D::pop(uint32_t pooling_id) {
	auto result = pool.find(pooling_id);

	// If the pool doesn't contain a queue with that key or if it does but the queue is empty return a nullptr
	if (result == pool.end() || result->second.size() == 0) {
		return nullptr;
	}

	// Get the first BulletAttachment2D pointer in the queue
	BulletAttachment2D *found_attachment = result->second.front();

	// Remove it from the queue
	result->second.pop();

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
