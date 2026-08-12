from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Sequence


LogFunction = Callable[[str], None]
ANSI_ESCAPE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|$))")


@dataclass(frozen=True)
class DeploymentPaths:
    repository_root: Path
    service_root: Path
    server_root: Path
    bundle: Path
    publisher_key: Path
    leaderboard_ids: Path
    curated_packages: Path
    tunnel_token: Path
    manifest: Path
    client_service_config: Path
    bundle_builder: Path
    task_installer: Path
    provisioner: Path

    @staticmethod
    def discover() -> "DeploymentPaths":
        service_root = Path(__file__).resolve().parent
        repository_root = service_root.parents[1]
        local_app_data = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
        server_root = local_app_data / "MaxXThrottle" / "server"
        return DeploymentPaths(
            repository_root=repository_root,
            service_root=service_root,
            server_root=server_root,
            bundle=server_root / "verifier-bundle",
            publisher_key=server_root / "steam_publisher_key.txt",
            leaderboard_ids=server_root / "mxt-leaderboard-ids-5001310.json",
            curated_packages=server_root / "curated-workshop-packages.json",
            tunnel_token=server_root / "cloudflare-tunnel-token.txt",
            manifest=repository_root / "mxto" / "steam" / "leaderboards.json",
            client_service_config=repository_root / "mxto" / "steam" / "leaderboard_service.json",
            bundle_builder=service_root / "build_windows_bundle.ps1",
            task_installer=service_root / "install_windows_tasks.ps1",
            provisioner=service_root / "provision_leaderboards.py",
        )


class DeploymentError(RuntimeError):
    pass


class DeploymentManager:
    APP_ID = 5001310
    VERIFIER_TASK = "MaxXThrottleLeaderboardVerifier"
    TUNNEL_TASK = "MaxXThrottleLeaderboardTunnel"

    def __init__(self, paths: DeploymentPaths | None = None) -> None:
        self.paths = paths or DeploymentPaths.discover()

    @staticmethod
    def _quiet_process_flags() -> int:
        return subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0

    @staticmethod
    def _clean_output(line: str) -> str:
        return ANSI_ESCAPE.sub("", line).replace("\r", "").rstrip()

    def _run_capture(
        self,
        command: Sequence[str],
        *,
        cwd: Path | None = None,
        env: dict[str, str] | None = None,
        check: bool = True,
    ) -> subprocess.CompletedProcess[str]:
        completed = subprocess.run(
            list(command),
            cwd=str(cwd) if cwd else None,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=self._quiet_process_flags(),
            check=False,
        )
        if check and completed.returncode != 0:
            output = self._clean_output(completed.stdout)
            raise DeploymentError(output or f"Command failed with exit code {completed.returncode}.")
        return completed

    def _run_streaming(
        self,
        command: Sequence[str],
        log: LogFunction,
        *,
        cwd: Path | None = None,
        env: dict[str, str] | None = None,
    ) -> None:
        process = subprocess.Popen(
            list(command),
            cwd=str(cwd) if cwd else None,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            creationflags=self._quiet_process_flags(),
        )
        assert process.stdout is not None
        for raw_line in process.stdout:
            line = self._clean_output(raw_line)
            if line:
                log(line)
        return_code = process.wait()
        if return_code != 0:
            raise DeploymentError(f"Command failed with exit code {return_code}.")

    def _powershell(self, script: str, *, check: bool = True) -> subprocess.CompletedProcess[str]:
        return self._run_capture(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
            check=check,
        )

    def task_state(self, task_name: str) -> str:
        escaped_name = task_name.replace("'", "''")
        result = self._powershell(
            f"$task = Get-ScheduledTask -TaskName '{escaped_name}' -ErrorAction SilentlyContinue; "
            "if ($null -eq $task) { 'Missing' } else { [string]$task.State }",
            check=False,
        )
        state = self._clean_output(result.stdout).splitlines()
        return state[-1].strip() if state else "Unknown"

    def _task_command(self, action: str, task_name: str) -> None:
        if action not in {"Start", "Stop"}:
            raise ValueError("Unsupported task action")
        escaped_name = task_name.replace("'", "''")
        self._powershell(
            f"{action}-ScheduledTask -TaskName '{escaped_name}' -ErrorAction Stop"
        )

    def _wait_for_task_not_running(self, task_name: str, timeout: float = 15.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.task_state(task_name) != "Running":
                return
            time.sleep(0.25)
        raise DeploymentError(f"Windows task {task_name} did not stop in time.")

    def source_state(self) -> dict[str, object]:
        commit = self._run_capture(
            ["git", "rev-parse", "HEAD"], cwd=self.paths.repository_root
        ).stdout.strip()
        dirty_output = self._run_capture(
            ["git", "status", "--porcelain", "--untracked-files=no"],
            cwd=self.paths.repository_root,
        ).stdout
        return {"commit": commit, "tracked_dirty": bool(dirty_output.strip())}

    def public_origin(self) -> str:
        try:
            value = json.loads(self.paths.client_service_config.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return ""
        return str(value.get("base_url", "")).rstrip("/") if isinstance(value, dict) else ""

    @staticmethod
    def _request_status(url: str, timeout: float = 4.0) -> int:
        request = urllib.request.Request(url, method="GET", headers={"User-Agent": "MXT-Deployment-Manager/1"})
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                return int(response.status)
        except urllib.error.HTTPError as error:
            return int(error.code)
        except (urllib.error.URLError, TimeoutError, OSError):
            return 0

    def local_health(self, timeout: float = 3.0) -> bool:
        request = urllib.request.Request("http://127.0.0.1:8787/healthz", method="GET")
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                payload = json.loads(response.read().decode("utf-8"))
                return response.status == 200 and isinstance(payload, dict) and payload.get("ok") is True
        except (urllib.error.URLError, TimeoutError, OSError, json.JSONDecodeError):
            return False

    def public_route_health(self, timeout: float = 4.0) -> bool:
        origin = self.public_origin()
        if not origin.startswith("https://"):
            return False
        health_status = self._request_status(origin + "/healthz", timeout)
        submit_get_status = self._request_status(origin + "/v1/time-attack/submit", timeout)
        return health_status == 404 and submit_get_status == 404

    def bundle_identity(self) -> dict[str, object]:
        path = self.paths.bundle / "bundle_identity.json"
        try:
            value = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, json.JSONDecodeError):
            return {}
        return value if isinstance(value, dict) else {}

    def board_rows(self) -> tuple[list[dict[str, object]], dict[str, int]]:
        try:
            manifest = json.loads(self.paths.manifest.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise DeploymentError(f"Could not read leaderboard manifest: {error}") from error
        boards = manifest.get("boards", []) if isinstance(manifest, dict) else []
        if not isinstance(boards, list):
            raise DeploymentError("Leaderboard manifest has no board list.")
        id_map: dict[str, int] = {}
        try:
            deployed_ids = json.loads(self.paths.leaderboard_ids.read_text(encoding="utf-8"))
            raw_ids = deployed_ids.get("boards", {}) if isinstance(deployed_ids, dict) else {}
            if isinstance(raw_ids, dict):
                id_map = {str(name): int(value) for name, value in raw_ids.items() if int(value) > 0}
        except (OSError, json.JSONDecodeError, TypeError, ValueError):
            id_map = {}
        rows: list[dict[str, object]] = []
        for value in boards:
            if not isinstance(value, dict):
                continue
            steam_name = str(value.get("steam_name", ""))
            steam_id = id_map.get(steam_name, 0)
            rows.append(
                {
                    "title": str(value.get("track_title", value.get("track_slug", "Unknown Track"))),
                    "source": "Curated Workshop" if value.get("track_source") == "curated_workshop" else "Official",
                    "digest": str(value.get("track_gameplay_digest", "")),
                    "steam_name": steam_name,
                    "steam_id": steam_id,
                    "status": "Synced" if steam_id > 0 else "Missing Steam ID",
                }
            )
        return rows, id_map

    def status(self) -> dict[str, object]:
        source = self.source_state()
        identity = self.bundle_identity()
        boards, ids = self.board_rows()
        return {
            "source_commit": source["commit"],
            "source_tracked_dirty": source["tracked_dirty"],
            "deployed_commit": str(identity.get("git_commit", "")),
            "deployed_built_utc": str(identity.get("built_utc", "")),
            "deployed_tracked_dirty": bool(identity.get("tracked_worktree_dirty", False)),
            "verifier_task": self.task_state(self.VERIFIER_TASK),
            "tunnel_task": self.task_state(self.TUNNEL_TASK),
            "local_health": self.local_health(),
            "public_route_health": self.public_route_health(),
            "public_origin": self.public_origin(),
            "manifest_board_count": len(boards),
            "synced_board_count": sum(1 for row in boards if row["status"] == "Synced"),
            "id_map_count": len(ids),
            "boards": boards,
        }

    def _validate_private_files(self, include_tunnel: bool, include_leaderboard_ids: bool = True) -> None:
        required = [self.paths.publisher_key]
        if include_leaderboard_ids:
            required.append(self.paths.leaderboard_ids)
        if include_tunnel:
            required.append(self.paths.tunnel_token)
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            raise DeploymentError("Missing private deployment file(s):\n" + "\n".join(missing))
        key = self.paths.publisher_key.read_text(encoding="utf-8").strip()
        if not re.fullmatch(r"[0-9A-Fa-f]{32}", key):
            raise DeploymentError("The private Steam publisher key file is not valid.")

    def install_or_repair_tasks(self, log: LogFunction) -> None:
        self._validate_private_files(include_tunnel=True)
        log("Installing or repairing the two Windows background tasks...")
        command = [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(self.paths.task_installer),
            "-ServerRoot",
            str(self.paths.server_root),
            "-StartTasks",
            "-Force",
        ]
        self._run_streaming(command, log, cwd=self.paths.repository_root)

    def restart_services(self, log: LogFunction) -> None:
        for task_name in (self.VERIFIER_TASK, self.TUNNEL_TASK):
            state = self.task_state(task_name)
            if state == "Missing":
                raise DeploymentError(f"Windows task {task_name} is missing. Use Repair Background Tasks first.")
            if state == "Running":
                log(f"Stopping {task_name}...")
                self._task_command("Stop", task_name)
                self._wait_for_task_not_running(task_name)
            log(f"Starting {task_name}...")
            self._task_command("Start", task_name)
        self._wait_for_health(log)

    def _wait_for_health(self, log: LogFunction, timeout: float = 45.0) -> None:
        log("Waiting for the local verifier...")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.local_health(timeout=1.0):
                log("Local verifier: healthy.")
                break
            time.sleep(0.5)
        else:
            raise DeploymentError("The local verifier did not become healthy in time.")
        log("Checking the public HTTPS tunnel...")
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            if self.public_route_health(timeout=2.0):
                log("Public HTTPS route: healthy and correctly locked down.")
                return
            time.sleep(1.0)
        raise DeploymentError("The public HTTPS route did not become healthy in time.")

    @staticmethod
    def _remove_bundle_directory(path: Path) -> None:
        if not path.exists():
            return
        if not (path / ".mxt-verifier-bundle").is_file():
            raise DeploymentError(f"Refusing to remove an unrecognized directory: {path}")
        shutil.rmtree(path)

    def _remove_candidate_directory(self, path: Path) -> None:
        if not path.exists():
            return
        if (
            path.parent.resolve() != self.paths.server_root.resolve()
            or not path.name.startswith("verifier-bundle-candidate-")
        ):
            raise DeploymentError(f"Refusing to remove an unrecognized staging directory: {path}")
        shutil.rmtree(path)

    def deploy(self, log: LogFunction) -> None:
        if os.name != "nt":
            raise DeploymentError("The one-click verifier deployment currently supports Windows only.")
        self._validate_private_files(include_tunnel=True)
        for required in (self.paths.bundle_builder, self.paths.task_installer, self.paths.manifest):
            if not required.is_file():
                raise DeploymentError(f"Required deployment file is missing: {required}")

        scons = shutil.which("scons.cmd") or shutil.which("scons.exe") or shutil.which("scons")
        if not scons:
            raise DeploymentError(
                "SCons was not found. Install SCons or add its command directory to PATH, then try again."
            )

        log("Step 1/5: Building the release native library...")
        self._run_streaming(
            [scons, "target=template_release", "-j4"],
            log,
            cwd=self.paths.repository_root,
        )

        candidate = self.paths.server_root / f"verifier-bundle-candidate-{os.getpid()}"
        backup = self.paths.server_root / f"verifier-bundle-backup-{os.getpid()}"
        self._remove_candidate_directory(candidate)
        self._remove_bundle_directory(backup)
        log("Step 2/5: Building a fresh verifier bundle beside the live one...")
        self._run_streaming(
            [
                "powershell.exe",
                "-NoProfile",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(self.paths.bundle_builder),
                "-Destination",
                str(candidate),
            ],
            log,
            cwd=self.paths.repository_root,
        )

        old_bundle_saved = False
        task_was_running = self.task_state(self.VERIFIER_TASK) == "Running"
        try:
            log("Step 3/5: Briefly stopping the live verifier...")
            if task_was_running:
                self._task_command("Stop", self.VERIFIER_TASK)
                self._wait_for_task_not_running(self.VERIFIER_TASK)

            if self.paths.bundle.exists():
                if not (self.paths.bundle / ".mxt-verifier-bundle").is_file():
                    raise DeploymentError(f"Live bundle directory is not recognized: {self.paths.bundle}")
                os.replace(self.paths.bundle, backup)
                old_bundle_saved = True
            os.replace(candidate, self.paths.bundle)

            log("Step 4/5: Starting the new verifier...")
            if self.task_state(self.VERIFIER_TASK) == "Missing" or self.task_state(self.TUNNEL_TASK) == "Missing":
                self.install_or_repair_tasks(log)
            else:
                self._task_command("Start", self.VERIFIER_TASK)
                if self.task_state(self.TUNNEL_TASK) != "Running":
                    self._task_command("Start", self.TUNNEL_TASK)

            log("Step 5/5: Running health checks...")
            self._wait_for_health(log)
        except Exception:
            log("Deployment failed. Restoring the previous working bundle...")
            if self.task_state(self.VERIFIER_TASK) == "Running":
                self._task_command("Stop", self.VERIFIER_TASK)
                self._wait_for_task_not_running(self.VERIFIER_TASK)
            self._remove_bundle_directory(self.paths.bundle)
            if old_bundle_saved and backup.exists():
                os.replace(backup, self.paths.bundle)
                if self.task_state(self.VERIFIER_TASK) != "Missing":
                    self._task_command("Start", self.VERIFIER_TASK)
            raise
        finally:
            self._remove_candidate_directory(candidate)

        self._remove_bundle_directory(backup)
        log("DONE: The trusted leaderboard verifier is deployed and healthy.")

    def sync_leaderboards(self, log: LogFunction, deploy_after: bool = True) -> None:
        self._validate_private_files(include_tunnel=deploy_after, include_leaderboard_ids=False)
        log("Syncing the checked-in leaderboard manifest to Steam...")
        key = self.paths.publisher_key.read_text(encoding="utf-8").strip()
        environment = os.environ.copy()
        environment["MXT_STEAM_APP_ID"] = str(self.APP_ID)
        environment["MXT_STEAM_PUBLISHER_KEY"] = key
        candidate_ids = self.paths.server_root / f"mxt-leaderboard-ids-candidate-{os.getpid()}.json"
        backup_ids = self.paths.server_root / f"mxt-leaderboard-ids-backup-{os.getpid()}.json"
        candidate_ids.unlink(missing_ok=True)
        backup_ids.unlink(missing_ok=True)
        old_ids_saved = False
        ids_replaced = False
        try:
            self._run_streaming(
                [
                    sys.executable,
                    str(self.paths.provisioner),
                    "--manifest",
                    str(self.paths.manifest),
                    "--output",
                    str(candidate_ids),
                ],
                log,
                cwd=self.paths.repository_root,
                env=environment,
            )
            if not candidate_ids.is_file():
                raise DeploymentError("Steam synchronization did not produce a leaderboard ID map.")
            if self.paths.leaderboard_ids.exists():
                os.replace(self.paths.leaderboard_ids, backup_ids)
                old_ids_saved = True
            os.replace(candidate_ids, self.paths.leaderboard_ids)
            ids_replaced = True
            log("Steam leaderboard IDs are synchronized.")
            if deploy_after:
                self.deploy(log)
        except Exception:
            log("Leaderboard synchronization or deployment failed. Restoring the previous ID map...")
            if ids_replaced:
                self.paths.leaderboard_ids.unlink(missing_ok=True)
            if ids_replaced and old_ids_saved and backup_ids.exists():
                os.replace(backup_ids, self.paths.leaderboard_ids)
                if self.task_state(self.VERIFIER_TASK) != "Missing":
                    if self.task_state(self.VERIFIER_TASK) == "Running":
                        self._task_command("Stop", self.VERIFIER_TASK)
                        self._wait_for_task_not_running(self.VERIFIER_TASK)
                    self._task_command("Start", self.VERIFIER_TASK)
                    self._wait_for_health(log)
            raise
        finally:
            candidate_ids.unlink(missing_ok=True)
            backup_ids.unlink(missing_ok=True)
