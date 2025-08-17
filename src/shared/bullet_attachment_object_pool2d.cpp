#include "./bullet_attachment_object_pool2d.hpp"
#include "./bullet_attachment2d.hpp"

using namespace godot;
namespace BlastBullets2D{


void BulletAttachmentObjectPool2D::push(BulletAttachment2D* bullet_attachment){
    pool[bullet_attachment->attachment_id].push(bullet_attachment);
}

BulletAttachment2D* BulletAttachmentObjectPool2D::pop(int attachment_id){
    auto result = pool.find(attachment_id);

    // If the pool doesn't contain a queue with that key or if it does but the queue is empty return a nullptr
    if (result == pool.end() || result->second.size() == 0) {
        return nullptr;
    }

    // Get the first BulletAttachment2D pointer in the queue
    BulletAttachment2D *found_attachment = result->second.front();

    // Remove it from the queue
    result->second.pop();

    return found_attachment;
}

void BulletAttachmentObjectPool2D::free_all_bullet_attachments(){
    for(auto &[attachment_id, queue] : pool){  
        while (queue.empty() == false) {
            queue.front()->queue_free(); // delete the attachment
            queue.pop(); // remove it from the queue
        }
    }

    pool.clear();
}

void BulletAttachmentObjectPool2D::free_specific_bullet_attachments(int attachment_id){
    // Try to find a queue that exists and holds bullet attachments with a specific attachment_id
    auto it = pool.find(attachment_id);
    
    // If the queue doesn't exist or if the queue is empty, then it means there's no attachments to free
    if(it == pool.end() || it->second.empty()){
        return;
    }

    auto &queue = it->second;

    // We know the queue contains at least 1 bullet attachment, so we use a do-while loop to ensure the operation happens at least once
    do
    {
        queue.front()->queue_free(); // free the bullet attachment
        queue.pop(); // remove it from the queue
    } while (queue.empty() == false);
    
    pool.erase(attachment_id); // delete the queue itself since it's basically empty right now
}

int BulletAttachmentObjectPool2D::get_total_amount_pooled()
{
    int amount_attachments = 0;
    for (auto& [attachment_id, queue] : pool) {
        amount_attachments += static_cast<int>(queue.size());
    }

    return amount_attachments;
}

std::map<int, int> BulletAttachmentObjectPool2D::get_pool_info()
{
    std::map<int, int> pool_info;

    for (auto& [attachment_id, queue] : pool) {
        if (!queue.empty())
        {
            pool_info.emplace(attachment_id, static_cast<int>(queue.size()));
        }
    }

    return pool_info;
}
}