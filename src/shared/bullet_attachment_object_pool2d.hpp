#pragma once

#include <godot_cpp/core/class_db.hpp>
#include <queue>
#include <unordered_map>
#include <map>

namespace BlastBullets2D {
using namespace godot;

class BulletAttachment2D;

class BulletAttachmentObjectPool2D {
public:
    // Add a new bullet attachment to the pool
    void push(BulletAttachment2D *bullet_attachment);

    // Retrieve a bullet attachment
    BulletAttachment2D *pop(int attachment_id);

    // Free memory by deleting all bullet attachments that are in the pool
    void free_all_bullet_attachments();

    // Free memory by deleting bullet attachments, but only those that have a specific attachment_id
    void free_specific_bullet_attachments(int attachment_id);

    // Gets the total amount of attachments currently in the object pool
    int get_total_amount_pooled();

    // Gets a map containing the attachment_id as KEY and the attachment amount as VALUE
    std::map<int, int> get_pool_info();
    
private:
    // Keeps all pooled BulletAttachment2D pointers. They key is the attachment_id and the value is a queue of a bunch of BulletAttachment2D pointers. The idea is that as long as the attachment_id of the BulletAttachment2D is the same, then different instances will be saved in the same queue - this makes re-usability of same scene BulletAttachment2D nodes possible even though they are different instances
    std::unordered_map<int, std::queue<BulletAttachment2D *>> pool;
};
}
