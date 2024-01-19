#ifndef MULTI_MESH_BULLETS2D
#define MULTI_MESH_BULLETS2D

using namespace godot;
class MultiMeshBullets2D{
    public:
        // The area that holds all collision shapes
        RID area;
        // Stores a collision shape RID for each bullet
        //TypedArray<RID> collision_shapes;
        
        // The amount of bullets being rendered (note that the transparent bullets also keep being rendered)
        int size;
};
#endif