#ifndef NORMAL_BULLETS2D
#define NORMAL_BULLETS2D

#include "multi_mesh_bullets2d.hpp"

class NormalBullets2D : public MultiMeshBullets2D{
    GDCLASS(NormalBullets2D, MultiMeshBullets2D)
    
    public:
    protected:
        static void _bind_methods();
    private:
        void move_bullets(float delta) override;
};

#endif