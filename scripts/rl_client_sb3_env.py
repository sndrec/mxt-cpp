import socket
import json
import time
from typing import Tuple, List

try:
    import gymnasium as gym
    from gymnasium import spaces
except Exception:
    gym = None


class RLEnvClient(gym.Env if gym else object):
    metadata = {"render.modes": []}

    def __init__(self, host: str = "127.0.0.1", port: int = 5566, num_cars: int = 1, multi_agent: bool = False):
        assert gym is not None, "Install gymnasium to use this client"
        super().__init__()
        self.host = host
        self.port = port
        self.num_cars = num_cars
        self.multi_agent = multi_agent
        self.sock = None
        if not self.multi_agent:
            self.observation_space = spaces.Box(low=-float("inf"), high=float("inf"), shape=(27,), dtype=float)
            self.action_space = spaces.Box(low=-1.0, high=1.0, shape=(7,), dtype=float)
        else:
            self.observation_space = spaces.Box(low=-float("inf"), high=float("inf"), shape=(27*self.num_cars,), dtype=float)
            self.action_space = spaces.Box(low=-1.0, high=1.0, shape=(7*self.num_cars,), dtype=float)

    def _connect(self):
        if self.sock is not None:
            return
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((self.host, self.port))
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock = s

    def _send(self, obj):
        line = json.dumps(obj) + "
"
        self.sock.sendall(line.encode("utf-8"))

    def _recv(self):
        buf = b""
        while True:
            b = self.sock.recv(4096)
            if not b:
                raise ConnectionError("server disconnected")
            buf += b
            if b"
" in buf:
                line, rest = buf.split(b"
", 1)
                return json.loads(line.decode("utf-8"))

    def reset(self, *, seed=None, options=None):
        super().reset(seed=seed)
        self._connect()
        self._send({"cmd": "reset"})
        resp = self._recv()
        assert resp.get("ok"), f"reset failed: {resp}"
        obs_all = resp.get("obs", [])
        if not self.multi_agent:
            return obs_all[0], {}
        else:
            flat = []
            for i in range(self.num_cars):
                flat.extend(obs_all[i])
            return flat, {}

    def step(self, action):
        if not self.multi_agent:
            acts = [list(map(float, action))]
        else:
            a = list(map(float, action))
            assert len(a) == 7*self.num_cars
            acts = []
            for i in range(self.num_cars):
                start = 7*i
                acts.append(a[start:start+7])
        self._send({"cmd": "step", "actions": acts})
        resp = self._recv()
        assert resp.get("ok"), f"step failed: {resp}"
        obs_all = resp.get("obs", [])
        rew_all = resp.get("rew", [])
        done_all = resp.get("done", [])
                episode_end = bool(resp.get("episode_end", False))
        if not self.multi_agent:
            obs = obs_all[0]
            rew = float(rew_all[0]) if rew_all else 0.0
            done = bool(done_all[0]) if done_all else False
        else:
            flat = []
            for i in range(self.num_cars):
                flat.extend(obs_all[i])
            obs = flat
            rew = float(sum(rew_all)) / max(1, len(rew_all))
            done = all(bool(x) for x in done_all)
                if episode_end:
            done = True
        info = {"episode_end": episode_end}
        return obs, rew, done, False, info


if __name__ == "__main__":
    print("This Gymnasium environment can be used with Stable-Baselines3 PPO.")
    print("Example:")
    print("  from stable_baselines3 import PPO")
    print("  env = RLEnvClient(host='127.0.0.1', port=5566)")
    print("  model = PPO('MlpPolicy', env, verbose=1)")
    print("  model.learn(total_timesteps=200000)")
