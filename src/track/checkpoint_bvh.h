#ifndef CHECKPOINT_BVH_H
#define CHECKPOINT_BVH_H

#include "godot_cpp/variant/aabb.hpp"
#include <vector>

struct CheckpointBVHNode {
    godot::AABB bounds;
    int left = -1;
    int right = -1;
    int checkpoint_index = -1; // leaf index
};

class CheckpointBVH {
public:
    std::vector<CheckpointBVHNode> nodes;

    void build(const std::vector<godot::AABB> &aabbs);
    void query(int node_idx, const godot::Vector3 &point, std::vector<int> &results) const;
    void query_segment(int node_idx, const godot::Vector3 &a, const godot::Vector3 &b, std::vector<int> &results) const;
};

#endif // CHECKPOINT_BVH_H
