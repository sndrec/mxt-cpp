#pragma once
#include <cstdint>
#include <vector>

// Minimal, dependency-free MLP for fast runtime inference.
// Weights are stored row-major per layer: [out][in]
// Hidden activation: ReLU. Output: Linear (caller maps to action ranges).
class SimpleMLP {
public:
    struct Layer {
        int in_size = 0;
        int out_size = 0;
        // weights size = out_size * in_size
        std::vector<float> W;
        // bias size = out_size
        std::vector<float> b;
    };

    void clear();
    void set_structure(const std::vector<int> &sizes);
    // weights must contain sum(out_i*in_i + out_i) floats in order per layer: [W then b]
    bool load_weights(const std::vector<float> &weights);

    int input_size() const { return sizes_.empty() ? 0 : sizes_.front(); }
    int output_size() const { return sizes_.empty() ? 0 : sizes_.back(); }

    // Evaluates y = f(x). x.size() must equal input_size(). Returns output vector.
    void evaluate(const float *x, std::vector<float> &out_tmp) const;

private:
    std::vector<int> sizes_;
    std::vector<Layer> layers_;
};
