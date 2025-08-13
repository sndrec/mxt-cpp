import argparse, time
import numpy as np
from rl_client import RLClient
import json

def sigmoid(x):
    return 1.0/(1.0+np.exp(-x))

def tanh(x):
    return np.tanh(x)

class Policy:
    def __init__(self, layer_sizes, weights):
        self.sizes = layer_sizes
        self.layers = []
        off = 0
        for i in range(len(layer_sizes)-1):
            nin = layer_sizes[i]
            nout = layer_sizes[i+1]
            nW = nout*nin
            W = np.array(weights[off:off+nW], dtype=np.float32).reshape(nout, nin); off += nW
            b = np.array(weights[off:off+nout], dtype=np.float32); off += nout
            self.layers.append((W,b))

    def act(self, obs: np.ndarray) -> np.ndarray:
        x = obs.astype(np.float32)
        for i,(W,b) in enumerate(self.layers):
            x = x @ W.T + b
            if i != len(self.layers)-1:
                x = np.maximum(x, 0.0)
        out = np.empty_like(x)
        out[...,0] = tanh(x[...,0])
        out[...,1] = tanh(x[...,1])
        out[...,2] = sigmoid(x[...,2])
        out[...,3] = sigmoid(x[...,3])
        out[...,4:] = x[...,4:]
        return out

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=5566)
    ap.add_argument('--weights', required=True)
    args = ap.parse_args()

    data = json.load(open(args.weights, 'r'))
    pol = Policy(data['layer_sizes'], data['weights'])

    env = RLClient(port=args.port)
    env.connect()
    obs = env.reset()
    print('Connected. Rolling out policy...')
    while True:
        o = np.array(obs, dtype=np.float32)
        raw = pol.act(o)
        obs, rew, done, episode_end = env.step(raw.tolist())
        if episode_end:
            obs = env.reset()
