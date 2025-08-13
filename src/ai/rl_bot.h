#pragma once
#include "ai/simple_mlp.h"
#include "ai/observation.h"
#include "mxt_core/player_input.h"
#include <vector>

class RLBot {
public:
    SimpleMLP mlp;

    bool configure(const std::vector<int> &layers, const std::vector<float> &weights) {
        mlp.set_structure(layers);
        return mlp.load_weights(weights);
    }

    // Map network outputs to PlayerInput
    PlayerInput act(const AgentObservation &obs) const {
        PlayerInput pi = PlayerInput::from_neutral();
        if (mlp.output_size() <= 0) return pi;
        std::vector<float> out;
        mlp.evaluate(obs.data, out);
        auto tanh = [](float x){ return std::tanh(x); };
        auto sigmoid = [](float x){ return 1.0f / (1.0f + std::exp(-x)); };
        // Expect at least 5 outputs
        if ((int)out.size() >= 1) pi.steer_horizontal = std::max(-1.0f, std::min(1.0f, tanh(out[0])));
        if ((int)out.size() >= 2) pi.steer_vertical   = std::max(-1.0f, std::min(1.0f, tanh(out[1])));
        if ((int)out.size() >= 3) pi.accelerate       = std::max(0.0f, std::min(1.0f, sigmoid(out[2])));
        if ((int)out.size() >= 4) pi.brake            = std::max(0.0f, std::min(1.0f, sigmoid(out[3])));
        if ((int)out.size() >= 5) pi.boost            = sigmoid(out[4]) > 0.5f;
        if ((int)out.size() >= 6) pi.sideattack       = sigmoid(out[5]) > 0.5f;
        if ((int)out.size() >= 7) pi.spinattack       = sigmoid(out[6]) > 0.5f;
        return pi;
    }
};
