#include "simple_mlp.h"
#include <algorithm>

void SimpleMLP::clear() {
    sizes_.clear();
    layers_.clear();
}

void SimpleMLP::set_structure(const std::vector<int> &sizes) {
    clear();
    sizes_ = sizes;
    if (sizes_.size() < 2) return;
    layers_.resize(sizes_.size() - 1);
    for (size_t i = 0; i + 1 < sizes_.size(); ++i) {
        auto &L = layers_[i];
        L.in_size = sizes_[i];
        L.out_size = sizes_[i+1];
        L.W.assign(L.out_size * L.in_size, 0.0f);
        L.b.assign(L.out_size, 0.0f);
    }
}

bool SimpleMLP::load_weights(const std::vector<float> &weights) {
    size_t off = 0;
    for (auto &L : layers_) {
        size_t w_count = static_cast<size_t>(L.out_size) * static_cast<size_t>(L.in_size);
        size_t b_count = static_cast<size_t>(L.out_size);
        if (off + w_count + b_count > weights.size())
            return false;
        std::copy(weights.begin() + off, weights.begin() + off + w_count, L.W.begin());
        off += w_count;
        std::copy(weights.begin() + off, weights.begin() + off + b_count, L.b.begin());
        off += b_count;
    }
    return off == weights.size();
}

static inline float relu(float x) { return x > 0.0f ? x : 0.0f; }

void SimpleMLP::evaluate(const float *x, std::vector<float> &out_tmp) const {
    if (layers_.empty()) { out_tmp.clear(); return; }

    // two buffers to ping-pong without reallocating too often
    std::vector<float> a, b;
    a.assign(layers_.front().in_size, 0.0f);
    std::copy(x, x + layers_.front().in_size, a.begin());

    for (size_t li = 0; li < layers_.size(); ++li) {
        const auto &L = layers_[li];
        b.assign(L.out_size, 0.0f);
        // b = W*a + b
        const float *W = L.W.data();
        for (int o = 0; o < L.out_size; ++o) {
            float acc = L.b[o];
            const float *wrow = W + o * L.in_size;
            for (int i = 0; i < L.in_size; ++i) {
                acc += wrow[i] * a[i];
            }
            // Hidden layers: ReLU, Output: linear
            if (li + 1 != layers_.size()) acc = relu(acc);
            b[o] = acc;
        }
        a.swap(b);
    }
    out_tmp.swap(a);
}
