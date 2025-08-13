"""
Utility to export a simple MLP's weights into the flat format expected by GameSim.set_bot_model.

Usage (example):
    import torch
    import torch.nn as nn
    from rl_export_pytorch import export_mlp

    model = nn.Sequential(
        nn.Linear(24, 64), nn.ReLU(),
        nn.Linear(64, 64), nn.ReLU(),
        nn.Linear(64, 7),  # output layer (no activation)
    )

    layer_sizes, weights = export_mlp(model)
    # layer_sizes is a Python list of ints
    # weights is a Python list of floats (row-major W then b per layer)
    # In Godot, pass them as PackedInt32Array and PackedFloat32Array to set_bot_model
"""
from typing import List, Tuple

try:
    import torch
    import torch.nn as nn
except Exception as e:
    torch = None
    nn = None


def export_mlp(model) -> Tuple[List[int], List[float]]:
    assert torch is not None, "PyTorch not available"
    assert isinstance(model, nn.Sequential), "Expected nn.Sequential"

    # Extract linear layers in order
    linears = [m for m in model if isinstance(m, nn.Linear)]
    assert len(linears) >= 2, "Need at least input and output linear layers"

    sizes: List[int] = [linears[0].in_features]
    for lin in linears:
        sizes.append(lin.out_features)

    flat: List[float] = []
    for lin in linears:
        W = lin.weight.detach().cpu().numpy()  # shape [out, in]
        b = lin.bias.detach().cpu().numpy()    # shape [out]
        flat.extend(W.flatten(order='C').tolist())  # row-major
        flat.extend(b.tolist())

    return sizes, flat

if __name__ == '__main__':
    print("This module provides export_mlp(model) for use in your training scripts.")
