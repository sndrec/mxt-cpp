from __future__ import annotations

import os
import queue
import threading
import traceback
import webbrowser
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import messagebox, ttk

from deployment_manager_core import DeploymentError, DeploymentManager


class DeploymentManagerWindow:
    COLORS = {
        "background": "#171a21",
        "panel": "#202532",
        "panel_alt": "#292f3d",
        "text": "#f2f5f8",
        "muted": "#a7b0be",
        "accent": "#66c0f4",
        "accent_active": "#8dd3f8",
        "good": "#74d99f",
        "warn": "#f2c66d",
        "bad": "#f07474",
    }

    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.manager = DeploymentManager()
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.busy = False
        self.action_buttons: list[tk.Widget] = []
        self.status_vars = {
            "verifier": tk.StringVar(value="Checking..."),
            "tunnel": tk.StringVar(value="Checking..."),
            "local": tk.StringVar(value="Checking..."),
            "public": tk.StringVar(value="Checking..."),
            "source": tk.StringVar(value="Source: checking..."),
            "deployed": tk.StringVar(value="Deployed: checking..."),
            "boards": tk.StringVar(value="Leaderboards: checking..."),
        }
        self.status_value_labels: dict[str, tk.Label] = {}

        self.root.title("MaxX Throttle Steam Backend Manager")
        self.root.geometry("1120x760")
        self.root.minsize(920, 660)
        self.root.configure(bg=self.COLORS["background"])
        self._configure_styles()
        self._build_ui()
        self.root.after(75, self._drain_events)
        self.refresh_status()

    def _configure_styles(self) -> None:
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("App.TFrame", background=self.COLORS["background"])
        style.configure("Panel.TFrame", background=self.COLORS["panel"])
        style.configure(
            "App.TNotebook",
            background=self.COLORS["background"],
            borderwidth=0,
            tabmargins=(0, 8, 0, 0),
        )
        style.configure(
            "App.TNotebook.Tab",
            background=self.COLORS["panel_alt"],
            foreground=self.COLORS["muted"],
            padding=(18, 9),
            borderwidth=0,
        )
        style.map(
            "App.TNotebook.Tab",
            background=[("selected", self.COLORS["accent"])],
            foreground=[("selected", "#111820")],
        )
        style.configure(
            "Boards.Treeview",
            background="#151923",
            fieldbackground="#151923",
            foreground=self.COLORS["text"],
            rowheight=28,
            borderwidth=0,
        )
        style.configure(
            "Boards.Treeview.Heading",
            background=self.COLORS["panel_alt"],
            foreground=self.COLORS["text"],
            relief="flat",
            padding=(8, 8),
        )
        style.map("Boards.Treeview", background=[("selected", "#31546c")])
        style.configure(
            "Horizontal.TProgressbar",
            troughcolor=self.COLORS["panel_alt"],
            background=self.COLORS["accent"],
            borderwidth=0,
        )

    def _build_ui(self) -> None:
        outer = tk.Frame(self.root, bg=self.COLORS["background"], padx=22, pady=18)
        outer.pack(fill="both", expand=True)

        tk.Label(
            outer,
            text="MAXX THROTTLE",
            font=("Segoe UI Semibold", 11),
            fg=self.COLORS["accent"],
            bg=self.COLORS["background"],
        ).pack(anchor="w")
        tk.Label(
            outer,
            text="Steam Backend Manager",
            font=("Segoe UI Semibold", 26),
            fg=self.COLORS["text"],
            bg=self.COLORS["background"],
        ).pack(anchor="w", pady=(0, 2))
        tk.Label(
            outer,
            text="One button updates the trusted leaderboard verifier. Your normal Steam game upload stays exactly the same.",
            font=("Segoe UI", 10),
            fg=self.COLORS["muted"],
            bg=self.COLORS["background"],
        ).pack(anchor="w", pady=(0, 10))

        notebook = ttk.Notebook(outer, style="App.TNotebook")
        notebook.pack(fill="both", expand=True)
        deploy_tab = ttk.Frame(notebook, style="App.TFrame")
        boards_tab = ttk.Frame(notebook, style="App.TFrame")
        notebook.add(deploy_tab, text="Deploy & Status")
        notebook.add(boards_tab, text="Leaderboards")
        self._build_deploy_tab(deploy_tab)
        self._build_boards_tab(boards_tab)

    def _make_button(
        self,
        parent: tk.Widget,
        text: str,
        command,
        *,
        primary: bool = False,
        width: int | None = None,
    ) -> tk.Button:
        button = tk.Button(
            parent,
            text=text,
            command=command,
            font=("Segoe UI Semibold", 11 if primary else 9),
            fg="#111820" if primary else self.COLORS["text"],
            bg=self.COLORS["accent"] if primary else self.COLORS["panel_alt"],
            activeforeground="#111820" if primary else self.COLORS["text"],
            activebackground=self.COLORS["accent_active"] if primary else "#394254",
            disabledforeground="#68717f",
            relief="flat",
            cursor="hand2",
            padx=20 if primary else 12,
            pady=13 if primary else 8,
            width=width,
        )
        self.action_buttons.append(button)
        return button

    def _build_deploy_tab(self, parent: ttk.Frame) -> None:
        top = tk.Frame(parent, bg=self.COLORS["background"])
        top.pack(fill="x", pady=(14, 12))
        primary = self._make_button(top, "DEPLOY VERIFIER", self.deploy, primary=True, width=24)
        primary.pack(side="left")
        tk.Label(
            top,
            text="Use this after exporting/uploading a new game build.\nIt builds, swaps, restarts, checks, and rolls back on failure.",
            justify="left",
            font=("Segoe UI", 10),
            fg=self.COLORS["muted"],
            bg=self.COLORS["background"],
        ).pack(side="left", padx=18)

        status_panel = tk.Frame(parent, bg=self.COLORS["panel"], padx=16, pady=14)
        status_panel.pack(fill="x", pady=(0, 12))
        status_panel.columnconfigure((0, 1, 2, 3), weight=1, uniform="status")
        self._status_tile(status_panel, 0, "Verifier Task", "verifier")
        self._status_tile(status_panel, 1, "Cloudflare Tunnel", "tunnel")
        self._status_tile(status_panel, 2, "Local Health", "local")
        self._status_tile(status_panel, 3, "Public Route", "public")

        detail_panel = tk.Frame(parent, bg=self.COLORS["panel"], padx=16, pady=12)
        detail_panel.pack(fill="x", pady=(0, 12))
        for key in ("source", "deployed", "boards"):
            tk.Label(
                detail_panel,
                textvariable=self.status_vars[key],
                anchor="w",
                font=("Consolas", 9),
                fg=self.COLORS["muted"],
                bg=self.COLORS["panel"],
            ).pack(fill="x", pady=2)

        tools = tk.Frame(parent, bg=self.COLORS["background"])
        tools.pack(fill="x", pady=(0, 10))
        self._make_button(tools, "Refresh Status", self.refresh_status).pack(side="left", padx=(0, 8))
        self._make_button(tools, "Restart Services", self.restart_services).pack(side="left", padx=(0, 8))
        self._make_button(tools, "Repair Background Tasks", self.repair_tasks).pack(side="left", padx=(0, 8))
        self._make_button(tools, "Open Private Server Folder", lambda: self._open_path(self.manager.paths.server_root)).pack(side="left")

        progress_row = tk.Frame(parent, bg=self.COLORS["background"])
        progress_row.pack(fill="x", pady=(0, 6))
        self.activity_label = tk.Label(
            progress_row,
            text="Ready.",
            font=("Segoe UI Semibold", 9),
            fg=self.COLORS["muted"],
            bg=self.COLORS["background"],
        )
        self.activity_label.pack(side="left")
        self.progress = ttk.Progressbar(progress_row, mode="indeterminate", length=180)
        self.progress.pack(side="right")

        self.log_text = tk.Text(
            parent,
            height=14,
            bg="#10141c",
            fg="#dbe3ec",
            insertbackground=self.COLORS["text"],
            selectbackground="#31546c",
            relief="flat",
            font=("Consolas", 9),
            padx=10,
            pady=8,
            wrap="word",
            state="disabled",
        )
        self.log_text.pack(fill="both", expand=True)

    def _status_tile(self, parent: tk.Frame, column: int, title: str, key: str) -> None:
        frame = tk.Frame(parent, bg=self.COLORS["panel_alt"], padx=12, pady=10)
        frame.grid(row=0, column=column, sticky="nsew", padx=(0 if column == 0 else 5, 0))
        tk.Label(
            frame,
            text=title,
            font=("Segoe UI", 9),
            fg=self.COLORS["muted"],
            bg=self.COLORS["panel_alt"],
        ).pack(anchor="w")
        value = tk.Label(
            frame,
            textvariable=self.status_vars[key],
            font=("Segoe UI Semibold", 13),
            fg=self.COLORS["text"],
            bg=self.COLORS["panel_alt"],
        )
        value.pack(anchor="w", pady=(3, 0))
        self.status_value_labels[key] = value

    def _build_boards_tab(self, parent: ttk.Frame) -> None:
        controls = tk.Frame(parent, bg=self.COLORS["background"])
        controls.pack(fill="x", pady=(14, 10))
        self._make_button(
            controls,
            "SYNC BOARDS TO STEAM + DEPLOY",
            self.sync_boards,
            primary=True,
            width=31,
        ).pack(side="left")
        tk.Label(
            controls,
            text="Use only after adding/changing boards in leaderboards.json.\nExisting boards are found; missing boards are created; then the verifier is deployed.",
            justify="left",
            font=("Segoe UI", 10),
            fg=self.COLORS["muted"],
            bg=self.COLORS["background"],
        ).pack(side="left", padx=18)

        tools = tk.Frame(parent, bg=self.COLORS["background"])
        tools.pack(fill="x", pady=(0, 10))
        self._make_button(tools, "Refresh Board List", self.refresh_status).pack(side="left", padx=(0, 8))
        self._make_button(tools, "Open Board Manifest", lambda: self._open_path(self.manager.paths.manifest)).pack(side="left", padx=(0, 8))
        self._make_button(tools, "Open Private ID Map", lambda: self._open_path(self.manager.paths.leaderboard_ids)).pack(side="left", padx=(0, 8))
        self._make_button(
            tools,
            "Open Steamworks Leaderboards",
            lambda: webbrowser.open(f"https://partner.steamgames.com/apps/leaderboards/{self.manager.APP_ID}"),
        ).pack(side="left")

        summary = tk.Frame(parent, bg=self.COLORS["panel"], padx=14, pady=10)
        summary.pack(fill="x", pady=(0, 8))
        tk.Label(
            summary,
            textvariable=self.status_vars["boards"],
            font=("Segoe UI Semibold", 10),
            fg=self.COLORS["text"],
            bg=self.COLORS["panel"],
        ).pack(side="left")
        tk.Label(
            summary,
            text="Boards are immutable identities. Editing a track's gameplay creates a new board; old boards remain on Steam.",
            font=("Segoe UI", 9),
            fg=self.COLORS["muted"],
            bg=self.COLORS["panel"],
        ).pack(side="right")

        columns = ("title", "source", "status", "steam_id", "steam_name", "digest")
        tree_frame = tk.Frame(parent, bg=self.COLORS["background"])
        tree_frame.pack(fill="both", expand=True)
        self.board_tree = ttk.Treeview(tree_frame, columns=columns, show="headings", style="Boards.Treeview")
        headings = {
            "title": "Track",
            "source": "Source",
            "status": "Status",
            "steam_id": "Steam ID",
            "steam_name": "Steam Name",
            "digest": "Gameplay Digest",
        }
        widths = {"title": 150, "source": 120, "status": 120, "steam_id": 90, "steam_name": 310, "digest": 190}
        for column in columns:
            self.board_tree.heading(column, text=headings[column])
            self.board_tree.column(column, width=widths[column], minwidth=70, stretch=column in {"steam_name", "digest"})
        scrollbar = ttk.Scrollbar(tree_frame, orient="vertical", command=self.board_tree.yview)
        self.board_tree.configure(yscrollcommand=scrollbar.set)
        self.board_tree.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        self.board_tree.tag_configure("missing", foreground=self.COLORS["bad"])
        self.board_tree.tag_configure("synced", foreground=self.COLORS["text"])

    def _open_path(self, path: Path) -> None:
        if not path.exists():
            messagebox.showerror("File not found", f"This path does not exist yet:\n\n{path}")
            return
        os.startfile(str(path))

    def _append_log(self, message: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.configure(state="normal")
        self.log_text.insert("end", f"[{stamp}] {message}\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _set_busy(self, busy: bool, label: str = "") -> None:
        self.busy = busy
        for button in self.action_buttons:
            button.configure(state="disabled" if busy else "normal")
        if busy:
            self.progress.start(12)
            self.activity_label.configure(text=label or "Working...", fg=self.COLORS["accent"])
        else:
            self.progress.stop()
            self.activity_label.configure(text=label or "Ready.", fg=self.COLORS["muted"])

    def _run_action(self, name: str, action, *, refresh_after: bool = True) -> None:
        if self.busy:
            return
        self._set_busy(True, name)
        self._append_log(f"--- {name} ---")

        def worker() -> None:
            try:
                action(lambda line: self.events.put(("log", line)))
            except (DeploymentError, OSError, ValueError) as error:
                self.events.put(("error", str(error)))
            except Exception:
                self.events.put(("error", traceback.format_exc()))
            else:
                self.events.put(("success", name))
            finally:
                self.events.put(("done", refresh_after))

        threading.Thread(target=worker, daemon=True).start()

    def refresh_status(self) -> None:
        if self.busy:
            return
        self._set_busy(True, "Checking deployment status...")

        def worker() -> None:
            try:
                self.events.put(("status", self.manager.status()))
            except Exception:
                self.events.put(("error", traceback.format_exc()))
            finally:
                self.events.put(("done", False))

        threading.Thread(target=worker, daemon=True).start()

    def deploy(self) -> None:
        if not messagebox.askokcancel(
            "Deploy trusted verifier?",
            "This will build a fresh verifier, briefly restart the backend, and run health checks.\n\n"
            "If the new deployment fails, the previous working bundle is restored automatically.",
        ):
            return
        self._run_action("Deploying trusted verifier", self.manager.deploy)

    def sync_boards(self) -> None:
        if not messagebox.askyesno(
            "Sync leaderboard boards to Steam?",
            "Use this only when leaderboards.json changed.\n\n"
            "Steam leaderboard identities cannot be deleted. Existing names will be reused and missing names will be created. "
            "Afterward, the verifier will be deployed automatically.\n\nContinue?",
            icon="warning",
        ):
            return
        self._run_action(
            "Synchronizing Steam leaderboards and deploying",
            lambda log: self.manager.sync_leaderboards(log, deploy_after=True),
        )

    def restart_services(self) -> None:
        self._run_action("Restarting verifier and tunnel", self.manager.restart_services)

    def repair_tasks(self) -> None:
        if not messagebox.askokcancel(
            "Repair background tasks?",
            "This recreates the verifier and Cloudflare Tunnel tasks and starts them. It does not change Steam data.",
        ):
            return
        self._run_action("Repairing Windows background tasks", self.manager.install_or_repair_tasks)

    def _apply_status(self, status: dict[str, object]) -> None:
        verifier = str(status.get("verifier_task", "Unknown"))
        tunnel = str(status.get("tunnel_task", "Unknown"))
        local_ok = bool(status.get("local_health", False))
        public_ok = bool(status.get("public_route_health", False))
        self.status_vars["verifier"].set(verifier)
        self.status_vars["tunnel"].set(tunnel)
        self.status_vars["local"].set("Healthy" if local_ok else "Offline")
        self.status_vars["public"].set("Healthy" if public_ok else "Problem")
        self.status_value_labels["verifier"].configure(fg=self.COLORS["good"] if verifier == "Running" else self.COLORS["bad"])
        self.status_value_labels["tunnel"].configure(fg=self.COLORS["good"] if tunnel == "Running" else self.COLORS["bad"])
        self.status_value_labels["local"].configure(fg=self.COLORS["good"] if local_ok else self.COLORS["bad"])
        self.status_value_labels["public"].configure(fg=self.COLORS["good"] if public_ok else self.COLORS["bad"])

        source_commit = str(status.get("source_commit", ""))
        deployed_commit = str(status.get("deployed_commit", ""))
        source_dirty = bool(status.get("source_tracked_dirty", False))
        deployed_dirty = bool(status.get("deployed_tracked_dirty", False))
        self.status_vars["source"].set(
            f"Source:   {source_commit[:12] or 'unknown'}" + ("  (tracked changes present)" if source_dirty else "  (clean)")
        )
        self.status_vars["deployed"].set(
            f"Deployed: {deployed_commit[:12] or 'none'}" + ("  (built from tracked changes)" if deployed_dirty else "")
        )
        board_count = int(status.get("manifest_board_count", 0))
        synced_count = int(status.get("synced_board_count", 0))
        self.status_vars["boards"].set(f"Leaderboards: {synced_count} of {board_count} synchronized with Steam")

        for item in self.board_tree.get_children():
            self.board_tree.delete(item)
        for row in status.get("boards", []):
            if not isinstance(row, dict):
                continue
            digest = str(row.get("digest", ""))
            self.board_tree.insert(
                "",
                "end",
                values=(
                    row.get("title", ""),
                    row.get("source", ""),
                    row.get("status", ""),
                    row.get("steam_id", ""),
                    row.get("steam_name", ""),
                    digest.replace("sha256:", "")[:16] + "..." if digest else "",
                ),
                tags=("synced" if row.get("status") == "Synced" else "missing",),
            )

    def _drain_events(self) -> None:
        refresh_after_done = False
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "log":
                    self._append_log(str(payload))
                elif kind == "status" and isinstance(payload, dict):
                    self._apply_status(payload)
                elif kind == "error":
                    self._append_log("ERROR: " + str(payload))
                    messagebox.showerror("Operation failed", str(payload))
                elif kind == "success":
                    self._append_log("SUCCESS: " + str(payload))
                    messagebox.showinfo("Success", str(payload) + " completed successfully.")
                elif kind == "done":
                    refresh_after_done = refresh_after_done or bool(payload)
                    self._set_busy(False)
        except queue.Empty:
            pass
        if refresh_after_done and not self.busy:
            self.root.after(150, self.refresh_status)
        self.root.after(75, self._drain_events)


def main() -> None:
    root = tk.Tk()
    DeploymentManagerWindow(root)
    root.mainloop()


if __name__ == "__main__":
    main()
