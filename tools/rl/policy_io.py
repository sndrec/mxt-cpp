from typing import List
import json

# Helpers to pack/unpack weights to the C++ runtime format

def flatten_params(sizes: List[int], params: List) -> List[float]:
    # params is list of (W, b) for each layer
    flat: List[float] = []
    for (W, b) in params:
        # W shape (out, in)
        out_dim, in_dim = len(W), len(W[0])
        assert out_dim * in_dim == len(sum(W, [])), 'bad W'
        for o in range(out_dim):
            flat.extend(W[o])
        flat.extend(b)
    return flat


def save_weights_json(path: str, layer_sizes: List[int], flat_weights: List[float]):
    with open(path, 'w', encoding='utf-8') as f:
        json.dump({'layer_sizes': layer_sizes, 'weights': flat_weights}, f)
