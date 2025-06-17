#include "track/checkpoint_bvh.h"
#include <algorithm>
#include <cmath>

using godot::AABB;
using godot::Vector3;

static int build_recursive(CheckpointBVH &bvh, const std::vector<AABB> &aabbs, std::vector<int> &indices, int start, int end)
{
    // Index of the node we are about to create
    int node_idx = static_cast<int>(bvh.nodes.size());
    bvh.nodes.emplace_back();

    // Compute bounds for this node
    AABB bounds = aabbs[indices[start]];
    for (int i = start + 1; i < end; ++i)
        bounds = bounds.merge(aabbs[indices[i]]);
    bvh.nodes[node_idx].bounds = bounds;

    if (end - start == 1) {
        bvh.nodes[node_idx].checkpoint_index = indices[start];
        return node_idx;
    }

    // choose axis
    Vector3 min = bounds.position;
    Vector3 max = bounds.position + bounds.size;
    Vector3 extent = max - min;
    int axis = 0;
    if (extent.y > extent.x && extent.y > extent.z)
        axis = 1;
    else if (extent.z > extent.x && extent.z > extent.y)
        axis = 2;

    int mid = (start + end) / 2;
    std::nth_element(indices.begin() + start, indices.begin() + mid, indices.begin() + end, [&](int a, int b){
        float ca = aabbs[a].position[axis] + aabbs[a].size[axis] * 0.5f;
        float cb = aabbs[b].position[axis] + aabbs[b].size[axis] * 0.5f;
        return ca < cb;
    });

    int left_child = build_recursive(bvh, aabbs, indices, start, mid);
    int right_child = build_recursive(bvh, aabbs, indices, mid, end);

    bvh.nodes[node_idx].left = left_child;
    bvh.nodes[node_idx].right = right_child;
    return node_idx;
}

void CheckpointBVH::build(const std::vector<AABB> &aabbs)
{
    nodes.clear();
    if (aabbs.empty())
        return;

    // Reserve enough nodes to avoid costly reallocations while building. A BVH
    // with N leaves has at most (2 * N - 1) nodes.
    nodes.reserve(aabbs.size() * 2);

    std::vector<int> indices(aabbs.size());
    for (size_t i = 0; i < aabbs.size(); ++i)
        indices[i] = static_cast<int>(i);

    build_recursive(*this, aabbs, indices, 0, static_cast<int>(aabbs.size()));
}

void CheckpointBVH::query(int node_idx, const Vector3 &point, std::vector<int> &results) const
{
    if (node_idx < 0)
        return;

    // Use an explicit stack to avoid recursion and reduce function call
    // overhead. This greatly improves performance when querying many points.
    std::vector<int> stack;
    stack.reserve(64);
    stack.push_back(node_idx);

    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();

        const CheckpointBVHNode &node = nodes[idx];
        if (!node.bounds.has_point(point))
            continue;

        if (node.checkpoint_index != -1) {
            results.push_back(node.checkpoint_index);
            continue;
        }

        if (node.left != -1)
            stack.push_back(node.left);
        if (node.right != -1)
            stack.push_back(node.right);
    }
}

static bool segment_intersects_aabb(const Vector3 &a, const Vector3 &b, const AABB &box)
{
    // Optimised slab method. Works directly on each axis without constructing
    // temporary vectors and minimises branching for better performance.
    float tmin = 0.0f;
    float tmax = 1.0f;

    const float box_min[3] = { box.position.x, box.position.y, box.position.z };
    const float box_max[3] = { box.position.x + box.size.x,
                               box.position.y + box.size.y,
                               box.position.z + box.size.z };

    for (int axis = 0; axis < 3; ++axis) {
        float start = a[axis];
        float end   = b[axis];
        float d     = end - start;

        if (std::abs(d) < 1e-6f) {
            if (start < box_min[axis] || start > box_max[axis])
                return false;
            continue;
        }

        float inv_d = 1.0f / d;
        float t1    = (box_min[axis] - start) * inv_d;
        float t2    = (box_max[axis] - start) * inv_d;
        if (t1 > t2) {
            float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }

        if (t1 > tmin)
            tmin = t1;
        if (t2 < tmax)
            tmax = t2;
        if (tmin > tmax)
            return false;
    }

    return true;
}

void CheckpointBVH::query_segment(int node_idx, const Vector3 &a, const Vector3 &b, std::vector<int> &results) const
{
    if (node_idx < 0)
        return;

    std::vector<int> stack;
    stack.reserve(64);
    stack.push_back(node_idx);

    while (!stack.empty()) {
        int idx = stack.back();
        stack.pop_back();

        const CheckpointBVHNode &node = nodes[idx];
        if (!segment_intersects_aabb(a, b, node.bounds))
            continue;

        if (node.checkpoint_index != -1) {
            results.push_back(node.checkpoint_index);
            continue;
        }

        if (node.left != -1)
            stack.push_back(node.left);
        if (node.right != -1)
            stack.push_back(node.right);
    }
}

