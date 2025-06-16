#include "track/checkpoint_bvh.h"
#include <algorithm>
#include <cmath>

using godot::AABB;
using godot::Vector3;

static int build_recursive(CheckpointBVH &bvh, const std::vector<AABB> &aabbs, std::vector<int> &indices, int start, int end)
{
    int node_idx = (int)bvh.nodes.size();
    bvh.nodes.push_back(CheckpointBVHNode{});
    CheckpointBVHNode &node = bvh.nodes.back();

    // compute bounds
    AABB bounds = aabbs[indices[start]];
    for (int i = start + 1; i < end; ++i)
        bounds = bounds.merge(aabbs[indices[i]]);
    node.bounds = bounds;

    if (end - start == 1) {
        node.checkpoint_index = indices[start];
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

    node.left = build_recursive(bvh, aabbs, indices, start, mid);
    node.right = build_recursive(bvh, aabbs, indices, mid, end);
    return node_idx;
}

void CheckpointBVH::build(const std::vector<AABB> &aabbs)
{
    nodes.clear();
    if (aabbs.empty())
        return;
    std::vector<int> indices(aabbs.size());
    for (size_t i = 0; i < aabbs.size(); ++i)
        indices[i] = (int)i;
    build_recursive(*this, aabbs, indices, 0, (int)aabbs.size());
}

void CheckpointBVH::query(int node_idx, const Vector3 &point, std::vector<int> &results) const
{
    if (node_idx < 0)
        return;
    const CheckpointBVHNode &node = nodes[node_idx];
    if (!node.bounds.has_point(point))
        return;
    if (node.checkpoint_index != -1) {
        results.push_back(node.checkpoint_index);
        return;
    }
    if (node.left != -1)
        query(node.left, point, results);
    if (node.right != -1)
        query(node.right, point, results);
}

static bool segment_intersects_aabb(const Vector3 &a, const Vector3 &b, const AABB &box)
{
    Vector3 dir = b - a;
    float tmin = 0.0f;
    float tmax = 1.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float start = a[axis];
        float end = b[axis];
        float min_v = box.position[axis];
        float max_v = box.position[axis] + box.size[axis];
        float d = dir[axis];
        if (fabsf(d) < 1e-6f) {
            if (start < min_v || start > max_v)
                return false;
            continue;
        }
        float inv_d = 1.0f / d;
        float t1 = (min_v - start) * inv_d;
        float t2 = (max_v - start) * inv_d;
        if (t1 > t2)
            std::swap(t1, t2);
        tmin = fmaxf(tmin, t1);
        tmax = fminf(tmax, t2);
        if (tmin > tmax)
            return false;
    }
    return true;
}

void CheckpointBVH::query_segment(int node_idx, const Vector3 &a, const Vector3 &b, std::vector<int> &results) const
{
    if (node_idx < 0)
        return;
    const CheckpointBVHNode &node = nodes[node_idx];
    if (!segment_intersects_aabb(a, b, node.bounds))
        return;
    if (node.checkpoint_index != -1) {
        results.push_back(node.checkpoint_index);
        return;
    }
    if (node.left != -1)
        query_segment(node.left, a, b, results);
    if (node.right != -1)
        query_segment(node.right, a, b, results);
}

