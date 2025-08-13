import json, socket, time
from typing import List, Tuple

class RLClient:
    def __init__(self, host: str = '127.0.0.1', port: int = 5566, timeout: float = 30.0):
        self.host = host
        self.port = port
        self.sock = None
        self.timeout = timeout
        self._buf = b''

    def connect(self):
        t0 = time.time()
        while True:
            try:
                self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
                self.sock.settimeout(self.timeout)
                try:
                    self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                except Exception:
                    pass
                break
            except OSError:
                if time.time() - t0 > 30:
                    raise
                time.sleep(0.2)

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def _recv_line(self) -> dict:
        while True:
            if b'\n' in self._buf:
                line, self._buf = self._buf.split(b'\n', 1)
                if not line:
                    continue
                return json.loads(line.decode('utf-8'))
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError('Server closed')
            self._buf += chunk

    def _send(self, obj: dict) -> dict:
        data = (json.dumps(obj) + '\n').encode('utf-8')
        self.sock.sendall(data)
        return self._recv_line()

    def reset(self) -> List[List[float]]:
        r = self._send({'cmd': 'reset'})
        assert r.get('ok'), r
        return r['obs']


    def get_obs(self) -> list:
        r = self._send({'cmd': 'get_obs'})
        assert r.get('ok'), r
        return r['obs']

    def step(self, actions: List[List[float]]) -> Tuple[List[List[float]], List[float], List[bool], bool]:
        r = self._send({'cmd': 'step', 'actions': actions})
        assert r.get('ok'), r
        obs = r['obs']
        rew = r.get('rew', [])
        done = r.get('done', [])
        episode_end = bool(r.get('episode_end', False))
        return obs, rew, done, episode_end
