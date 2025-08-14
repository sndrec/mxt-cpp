import argparse
import os
import time
from pathlib import Path
from typing import List, Tuple

import numpy as np

from rl_client import RLClient
from policy_io import save_weights_json


def launch_godot(godot_bin: str, project_path: str, port: int, bots: int, show: bool,
                 render: bool = False, render_skip: int = 0, render_cars: int = 0):
    import subprocess
    cmd = [godot_bin]
    if not show:
        cmd.append('--headless')
    cmd += ['--path', project_path, '--', '--rl-port', str(port), '--rl-bots', str(bots)]
    if render:
        cmd += ['--rl-render']
    if render_skip and render_skip > 1:
        cmd += ['--rl-render-skip', str(render_skip)]
    if render_cars and render_cars > 0:
        cmd += ['--rl-render-cars', str(render_cars)]
    print('Launching Godot:', ' '.join(cmd))
    if show:
        proc = subprocess.Popen(cmd)
    else:
        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(0.5)
    return proc


# ──────────────────────────────────────────────────────────────────────────────
# Custom VecEnv that treats one Godot instance with N cars as N parallel envs
# ──────────────────────────────────────────────────────────────────────────────
from stable_baselines3.common.vec_env.base_vec_env import VecEnv
try:
    from stable_baselines3.common.vec_env.base_vec_env import VecEnvStepReturn, VecEnvObs
except Exception:
    from stable_baselines3.common.vec_env.vec_env import VecEnvStepReturn, VecEnvObs
try:
    import gymnasium as _gym
    spaces = _gym.spaces
except Exception:
    import gym as _gym
    spaces = _gym.spaces


OBS_SIZE = 22
ACT_SIZE = 5


class GodotMultiCarVecEnv(VecEnv):
    def __init__(self, host: str, port: int, num_cars: int):
        self.client = RLClient(host=host, port=port)
        self.client.connect()
        self.num_envs = num_cars
        obs_space = spaces.Box(low=-np.inf, high=np.inf, shape=(OBS_SIZE,), dtype=np.float32)
        act_space = spaces.Box(low=-1.0, high=1.0, shape=(ACT_SIZE,), dtype=np.float32)
        super().__init__(num_envs=self.num_envs, observation_space=obs_space, action_space=act_space)
        # Initial reset
        self._last_obs = None
        self.reset()

    def reset(self) -> VecEnvObs:
        obs = self.client.reset()  # List[List[float]] length = num_cars
        assert len(obs) == self.num_envs
        arr = np.asarray(obs, dtype=np.float32)
        self._last_obs = arr
        return arr

    def step_async(self, actions: np.ndarray) -> None:
        # Store actions to be sent on step_wait
        self._pending_actions = np.asarray(actions, dtype=np.float32)

    @staticmethod
    def _map_action_row(a: np.ndarray) -> List[float]:
        # Input in [-1,1]; server mirrors mapping internally, but we keep convention consistent
        sh = float(np.tanh(a[0]))
        sv = float(np.tanh(a[1]))
        accel = float((a[2] + 1.0) * 0.5)
        brake = float((a[3] + 1.0) * 0.5)
        # mutual exclusion mirrors server-side logic will resolve conflicts too
        boost = float(1.0 if a[4] > 0.0 else 0.0)
        return [sh, sv, accel, brake, boost]

    def step_wait(self) -> VecEnvStepReturn:
        acts = [[0.0]*ACT_SIZE for _ in range(self.num_envs)]
        pa = np.asarray(self._pending_actions, dtype=np.float32)
        assert pa.shape == (self.num_envs, ACT_SIZE)
        for i in range(self.num_envs):
            acts[i] = self._map_action_row(pa[i])
        obs, rew, done, episode_end = self.client.step(acts)
        # Use only the episode_end as terminal for all cars; individual 'done' is treated as retired but continuing
        term = bool(episode_end)
        if term:
            # Return terminal step and immediately reset to next obs
            last_obs = np.asarray(obs, dtype=np.float32)
            dones = np.ones((self.num_envs,), dtype=bool)
            infos = [{} for _ in range(self.num_envs)]
            # SB3 expects that obs returned after done is the first obs of the next episode
            next_obs = self.reset()
            rewards = np.asarray(rew, dtype=np.float32)
            return next_obs, rewards, dones, infos
        else:
            arr_obs = np.asarray(obs, dtype=np.float32)
            rewards = np.asarray(rew, dtype=np.float32)
            dones = np.zeros((self.num_envs,), dtype=bool)
            infos = [{} for _ in range(self.num_envs)]
            self._last_obs = arr_obs
            return arr_obs, rewards, dones, infos

    def close(self) -> None:
        try:
            self.client.close()
        except Exception:
            pass

    def render(self, mode="human"):
        return None

    def _indices(self, indices=None):
        if indices is None:
            return list(range(self.num_envs))
        if isinstance(indices, int):
            return [indices]
        return list(indices)

    def env_method(self, method_name: str, *method_args, indices=None, **method_kwargs):
        idxs = self._indices(indices)
        if hasattr(self, method_name):
            m = getattr(self, method_name)
            res = m(*method_args, **method_kwargs)
            return [res for _ in idxs]
        return [None for _ in idxs]

    def get_attr(self, attr_name: str, indices=None):
        idxs = self._indices(indices)
        if hasattr(self, attr_name):
            v = getattr(self, attr_name)
            return [v for _ in idxs]
        return [None for _ in idxs]

    def set_attr(self, attr_name: str, value, indices=None) -> None:
        idxs = self._indices(indices)
        for _ in idxs:
            setattr(self, attr_name, value)

    def env_is_wrapped(self, wrapper_class, indices=None):
        idxs = self._indices(indices)
        return [False for _ in idxs]


def export_policy_to_json(model, path: str, obs_size: int, act_size: int):
    """
    Export SB3 PPO policy (actor mean network) to our simple MLP JSON format
    understood by the C++ runtime (SimpleMLP/rl_bot).
    """
    import torch as th
    pi_net = model.policy.mlp_extractor.policy_net
    act_head = model.policy.action_net
    # Collect linear layers in order
    linears: List[th.nn.Linear] = [m for m in pi_net if isinstance(m, th.nn.Linear)]
    linears.append(act_head)
    # Build layer sizes
    layer_sizes = [obs_size]
    for lin in linears[:-1]:
        layer_sizes.append(lin.out_features)
    layer_sizes.append(act_size)
    # Flatten weights row-major (out, in) and biases
    flats: List[float] = []
    for lin in linears:
        W = lin.weight.detach().cpu().numpy()  # shape (out, in)
        b = lin.bias.detach().cpu().numpy()    # shape (out,)
        flats.extend(W.reshape(-1).astype(np.float32).tolist())
        flats.extend(b.astype(np.float32).tolist())
    save_weights_json(path, layer_sizes, flats)


def train_ppo(args):
    # Launch Godot
    godot_bin = args.godot or os.environ.get('GODOT_BIN') or 'godot4'
    gd = launch_godot(godot_bin, str(Path(args.project).resolve()), args.port, args.bots,
                      args.show, render=args.render, render_skip=args.render_skip, render_cars=args.render_cars)

    # VecEnv connecting to one server with N cars
    env = GodotMultiCarVecEnv(host='127.0.0.1', port=args.port, num_cars=args.bots)

    # Optional normalization
    from stable_baselines3.common.vec_env import VecNormalize
    if args.normalize:
        env = VecNormalize(env, norm_obs=True, norm_reward=args.norm_reward, clip_obs=10.0)

    # PPO
    from stable_baselines3 import PPO
    from stable_baselines3.common.callbacks import CheckpointCallback
    policy_kwargs = dict(net_arch=dict(pi=args.hidden, vf=args.hidden))
    model = PPO(
        'MlpPolicy', env,
        learning_rate=args.learning_rate,
        n_steps=args.n_steps,
        batch_size=args.batch_size,
        n_epochs=args.n_epochs,
        gamma=args.gamma,
        gae_lambda=args.gae_lambda,
        clip_range=args.clip_range,
        ent_coef=args.ent_coef,
        vf_coef=args.vf_coef,
        max_grad_norm=args.max_grad_norm,
        policy_kwargs=policy_kwargs,
        verbose=1
    )

    # Checkpointing
    out_dir = Path(args.outdir)
    out_dir.mkdir(parents=True, exist_ok=True)
    ckpt_cb = CheckpointCallback(save_freq=args.checkpoint_every, save_path=str(out_dir), name_prefix='ppo_model')

    model.learn(total_timesteps=args.total_timesteps, callback=ckpt_cb)

    # Save SB3 model
    model_path = out_dir / 'ppo_final.zip'
    model.save(str(model_path))
    print('Saved SB3 model to', model_path)

    # Export actor network to JSON for C++ runtime
    export_path = out_dir / 'ppo_actor_final.json'
    export_policy_to_json(model, str(export_path), obs_size=OBS_SIZE, act_size=ACT_SIZE)
    print('Exported actor weights to', export_path)

    try:
        env.close()
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
    ap.add_argument('--bots', type=int, default=256)
    ap.add_argument('--hidden', type=int, nargs='+', default=[64, 64])
    ap.add_argument('--total-timesteps', dest='total_timesteps', type=int, default=2_000_000)
    ap.add_argument('--n-steps', dest='n_steps', type=int, default=256)
    ap.add_argument('--batch-size', dest='batch_size', type=int, default=8192)
    ap.add_argument('--n-epochs', dest='n_epochs', type=int, default=8)
    ap.add_argument('--learning-rate', dest='learning_rate', type=float, default=3e-4)
    ap.add_argument('--gamma', type=float, default=0.995)
    ap.add_argument('--gae-lambda', dest='gae_lambda', type=float, default=0.95)
    ap.add_argument('--clip-range', dest='clip_range', type=float, default=0.2)
    ap.add_argument('--ent-coef', dest='ent_coef', type=float, default=0.01)
    ap.add_argument('--vf-coef', dest='vf_coef', type=float, default=0.5)
    ap.add_argument('--max-grad-norm', dest='max_grad_norm', type=float, default=0.5)
    ap.add_argument('--checkpoint-every', dest='checkpoint_every', type=int, default=100_000,
                    help='Checkpoint frequency in environment steps')
    ap.add_argument('--outdir', default='export-bin/rl_weights')
    ap.add_argument('--normalize', action='store_true', help='Use VecNormalize for observations (and rewards if --norm-reward)')
    ap.add_argument('--norm-reward', action='store_true', help='Also normalize rewards in VecNormalize')
    ap.add_argument('--show', action='store_true', help='Run Godot with UI (not headless) to watch training')
    ap.add_argument('--render', action='store_true', help='Render during training (server-side toggle)')
    ap.add_argument('--render-skip', dest='render_skip', type=int, default=3, help='Render every N frames (server-side)')
    ap.add_argument('--render-cars', dest='render_cars', type=int, default=4, help='Number of cars to instantiate visually (server-side)')
    args = ap.parse_args()
    train_ppo(args)
