import argparse, os, subprocess, sys, time, json, math
from pathlib import Path
from typing import List
import numpy as np
from rl_client import RLClient
from policy_io import save_weights_json

OBS_SIZE = 25
ACT_SIZE = 7

# Simple MLP forward pass using ReLU hidden, linear output
class MLP:
    def __init__(self, layer_sizes: List[int], seed=123):
        self.sizes = layer_sizes
        self.rng = np.random.default_rng(seed)
        self.params = []  # list of (W, b), W shape (out,in)
        for i in range(len(layer_sizes)-1):
            nin = layer_sizes[i]
            nout = layer_sizes[i+1]
            scale = math.sqrt(2.0/nin)
            W = self.rng.normal(0, scale, size=(nout, nin)).astype(np.float32)
            if i == len(layer_sizes)-2:
                W *= 0.1
            b = np.zeros((nout,), dtype=np.float32)
            self.params.append([W, b])

    def get_theta(self) -> np.ndarray:
        flats = []
        for W,b in self.params:
            flats.append(W.reshape(-1))
            flats.append(b.reshape(-1))
        return np.concatenate(flats)

    def set_theta(self, theta: np.ndarray):
        off = 0
        for i,(W,b) in enumerate(self.params):
            nW = W.size
            nb = b.size
            self.params[i][0] = theta[off:off+nW].reshape(W.shape).astype(np.float32); off += nW
            self.params[i][1] = theta[off:off+nb].reshape(b.shape).astype(np.float32); off += nb
        assert off == theta.size

    def act_batch(self, obs: np.ndarray) -> np.ndarray:
        x = obs.astype(np.float32)
        for i,(W,b) in enumerate(self.params):
            x = x @ W.T + b
            if i != len(self.params)-1:
                x = np.maximum(x, 0.0)
        return x


def tanh(x):
    return np.tanh(x)

def sigmoid(x):
    return 1.0/(1.0+np.exp(-x))


def to_action(raw: np.ndarray) -> np.ndarray:
    out = np.empty_like(raw)
    out[:,0] = tanh(raw[:,0])
    out[:,1] = tanh(raw[:,1])
    out[:,2] = tanh(raw[:,2])
    out[:,3] = tanh(raw[:,3])
    out[:,4] = raw[:,4]
    out[:,5] = raw[:,5]
    out[:,6] = raw[:,6]
    return out


def launch_godot(godot_bin: str, project_path: str, port: int, bots: int, show: bool) -> subprocess.Popen:
    cmd = [godot_bin]
    if not show:
        cmd.append('--headless')
    cmd += ['--path', project_path, '--', '--rl-port', str(port), '--rl-bots', str(bots)]
    print('Launching Godot:', ' '.join(cmd))
    if show:
        proc = subprocess.Popen(cmd)
    else:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(0.5)
    return proc


def train_es(args):
    bots = args.bots
    layer_sizes = [OBS_SIZE] + args.hidden + [ACT_SIZE]
    mlp = MLP(layer_sizes, seed=args.seed)
    theta = mlp.get_theta()
    dim = theta.size
    print(f'Param dim: {dim}')

    godot_bin = args.godot or os.environ.get('GODOT_BIN') or 'godot4'
    gd = launch_godot(godot_bin, str(Path(args.project).resolve()), args.port, bots, args.show)

    client = RLClient(port=args.port)
    client.connect()
    t0 = time.time(); obs = []
    while True:
        try:
            obs = client.get_obs()
        except Exception:
            pass
        if len(obs) == bots:
            break
        if time.time() - t0 > 5.0:
            try:
                obs = client.reset()
                if len(obs) == bots:
                    break
            except Exception:
                pass
        time.sleep(0.2)
    assert len(obs) == bots, f'expected {bots} obs, got {len(obs)}'

    sigma = args.sigma
    alpha = args.alpha
    rng = np.random.default_rng(args.seed)

    best_ret = -1e9
    best_theta = theta.copy()

    if bots % 2 == 1:
        print('Warning: bots should be even for mirrored sampling; reducing by 1')
        bots -= 1

    for gen in range(args.generations):
        obs = client.reset()
        half = bots//2
        eps = rng.standard_normal((half, dim), dtype=np.float32)
        pop = np.vstack([eps, -eps])
        returns = np.zeros((bots,), dtype=np.float32)
        alive = np.ones((bots,), dtype=bool)
        thetas = np.array([theta + sigma*pop[i] for i in range(bots)], dtype=np.float32)
        step = 0
        o = np.array(obs, dtype=np.float32)
        while True:
            acts = np.zeros((bots, ACT_SIZE), dtype=np.float32)
            for i in range(bots):
                if not alive[i]:
                    continue
                mlp.set_theta(thetas[i])
                raw = mlp.act_batch(o[i:i+1])
                a = to_action(raw)[0]
                acts[i] = a
            obs, rew, done, episode_end = client.step(acts.tolist())
            if rew:
                r = np.array(rew, dtype=np.float32)
                returns[:len(r)] += r
            if done:
                d = np.array(done, dtype=bool)
                alive[:len(d)] &= ~d
            o = np.array(obs, dtype=np.float32)
            step += 1
            if episode_end or not alive.any() or step >= args.max_steps:
                break

        ranks = returns.argsort().argsort().astype(np.float32)
        shaped = (ranks - ranks.mean()) / (ranks.std() + 1e-8)
        grad = (pop.T @ shaped[:bots]) / (bots)
        theta = theta + alpha/(sigma) * grad

        gen_best = returns.max(); gen_mean = returns.mean()
        if gen_best > best_ret:
            best_ret = gen_best
            best_theta = theta.copy()
        print(f'Gen {gen}: mean={gen_mean:.4f} best={gen_best:.4f} best_all={best_ret:.4f}')

        if (gen+1) % args.checkpoint_every == 0:
            mlp.set_theta(best_theta)
            flat = []
            for W,b in mlp.params:
                flat.extend(W.astype(np.float32).reshape(-1).tolist())
                flat.extend(b.astype(np.float32).tolist())
            out_dir = Path(args.outdir); out_dir.mkdir(parents=True, exist_ok=True)
            out_path = out_dir / f'mlp_{len(args.hidden)}x{args.hidden[0]}_gen{gen+1}.json'
            save_weights_json(str(out_path), layer_sizes, flat)
            print('Saved', out_path)

    mlp.set_theta(best_theta)
    flat = []
    for W,b in mlp.params:
        flat.extend(W.astype(np.float32).reshape(-1).tolist())
        flat.extend(b.astype(np.float32).tolist())
    out_dir = Path(args.outdir); out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / f'mlp_{len(args.hidden)}x{args.hidden[0]}_final.json'
    save_weights_json(str(out_path), layer_sizes, flat)
    print('Saved final', out_path)

    try:
        client.close()
    finally:
        try:
            gd.terminate()
        except Exception:
            pass


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--project', default='mxto', help='Path to Godot project folder')
    ap.add_argument('--godot', default=None, help='Path to godot4 executable (or set GODOT_BIN)')
    ap.add_argument('--port', type=int, default=5566)
    ap.add_argument('--bots', type=int, default=8)
    ap.add_argument('--hidden', type=int, nargs='+', default=[64,64])
    ap.add_argument('--sigma', type=float, default=0.1)
    ap.add_argument('--alpha', type=float, default=0.02)
    ap.add_argument('--generations', type=int, default=50)
    ap.add_argument('--max_steps', type=int, default=2000)
    ap.add_argument('--checkpoint_every', type=int, default=10)
    ap.add_argument('--seed', type=int, default=123)
    ap.add_argument('--outdir', default='export-bin/rl_weights')
    ap.add_argument('--show', action='store_true', help='Run Godot with UI (not headless) to watch training')
    args = ap.parse_args()
    train_es(args)
