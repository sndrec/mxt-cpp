from __future__ import annotations

from copy import deepcopy
import math
from pathlib import Path
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from mxt_car_props_format import (  # noqa: E402
    CarProperties,
    Curve,
    CurveKey,
    LIVE_MODIFIER_STATS,
    MODIFIER_LAYER_NAMES,
    STAT_NAMES,
    default_properties,
    properties_from_json,
    properties_to_json,
    property_warnings,
    read_binary,
    validate_properties,
    write_binary,
)


TECHNIQUE_LAYERS = ("mts", "quickturn")
BOOST_LAYERS = ("no_boost", "manual_boost", "dashplate_boost", "stacked_boost")
COMPOSED_BOOST_STATES = ("none", "manual", "dashplate", "stacked", "s_boost", "s_boost_dashplate")


class CarPropsEditor:
    def __init__(self, master: tk.Tk):
        self.master = master
        self.master.title("MXT Car Properties Curve Editor")
        self.master.geometry("1800x920")
        self.properties: CarProperties = default_properties()
        self.current_path: Path | None = None
        self.current_layer = "base"
        self.current_stat = STAT_NAMES[0]
        self.curve_clipboard: Curve | None = None
        self.corner_vars: list[list[tk.StringVar]] = []
        self.key_entries: dict[str, ttk.Entry] = {}
        self.curve_drag_kind: str | None = None

        self._build_toolbar()
        self._build_sampling_controls()
        self._build_workspace()
        self._build_status()
        self._load_properties_into_ui()

    def _build_toolbar(self) -> None:
        bar = ttk.Frame(self.master)
        bar.pack(fill=tk.X, padx=6, pady=6)
        for text, command in (
            ("New", self.new_file), ("Load Binary", self.load_binary),
            ("Save", self.save_binary), ("Save As", lambda: self.save_binary(True)),
            ("Import JSON", self.import_json), ("Export JSON", self.export_json),
            ("Validate", self.show_validation),
        ):
            ttk.Button(bar, text=text, command=command).pack(side=tk.LEFT, padx=3)

    def _build_sampling_controls(self) -> None:
        frame = ttk.LabelFrame(self.master, text="Sample and composed effective value")
        frame.pack(fill=tk.X, padx=6, pady=(0, 6))
        self.sample_var = tk.DoubleVar(value=0.5)
        ttk.Label(frame, text="Machine setting").grid(row=0, column=0, padx=4)
        ttk.Scale(frame, from_=0.0, to=1.0, variable=self.sample_var,
                  command=lambda _v: self.refresh_curve()).grid(row=0, column=1, sticky="ew", padx=4)
        self.sample_entry = ttk.Entry(frame, width=7, textvariable=self.sample_var)
        self.sample_entry.grid(row=0, column=2, padx=4)
        self.sample_entry.bind("<Return>", lambda _e: self.refresh_curve())

        self.compose_technique = tk.StringVar(value="none")
        self.technique_intensity = tk.DoubleVar(value=1.0)
        self.compose_boost = tk.StringVar(value="none")
        ttk.Label(frame, text="Technique").grid(row=0, column=3, padx=(16, 4))
        ttk.Combobox(frame, textvariable=self.compose_technique,
                     values=("none", *TECHNIQUE_LAYERS), state="readonly", width=11).grid(row=0, column=4)
        ttk.Label(frame, text="Intensity").grid(row=0, column=5, padx=4)
        ttk.Scale(frame, from_=0.0, to=1.0, variable=self.technique_intensity,
                  command=lambda _v: self.refresh_curve()).grid(row=0, column=6, sticky="ew")
        ttk.Label(frame, text="Boost state").grid(row=0, column=7, padx=(16, 4))
        ttk.Combobox(frame, textvariable=self.compose_boost, values=COMPOSED_BOOST_STATES,
                     state="readonly", width=18).grid(row=0, column=8)
        self.compose_technique.trace_add("write", lambda *_: self.refresh_curve())
        self.compose_boost.trace_add("write", lambda *_: self.refresh_curve())
        self.composed_label = ttk.Label(frame, text="")
        self.composed_label.grid(row=0, column=9, padx=12, sticky="w")
        frame.columnconfigure(1, weight=1)
        frame.columnconfigure(6, weight=1)

    def _build_workspace(self) -> None:
        split = ttk.Panedwindow(self.master, orient=tk.HORIZONTAL)
        split.pack(fill=tk.BOTH, expand=True, padx=6)
        left = ttk.Frame(split)
        curve_frame = ttk.Frame(split)
        acceleration_frame = ttk.Frame(split)
        split.add(left, weight=1)
        split.add(curve_frame, weight=3)
        split.add(acceleration_frame, weight=3)

        self.notebook = ttk.Notebook(left)
        self.notebook.pack(fill=tk.BOTH, expand=True)
        self.base_list = self._stat_tab("Base stats", STAT_NAMES, self._select_base)
        self.technique_list = self._layer_stat_tab(
            "Technique modifiers", TECHNIQUE_LAYERS, self._select_technique)
        self.boost_list = self._layer_stat_tab(
            "Boost modifiers", BOOST_LAYERS, self._select_boost)
        self._build_s_boost_tab()
        self._build_corners_tab()

        curve_title = ttk.Frame(curve_frame)
        curve_title.pack(fill=tk.X)
        self.curve_title = ttk.Label(curve_title, text="", font=("TkDefaultFont", 12, "bold"))
        self.curve_title.pack(side=tk.LEFT)
        for text, command in (
            ("Copy curve", self.copy_curve), ("Paste curve", self.paste_curve),
            ("Reset to 1", self.reset_selected_modifier),
            ("Copy layer...", self.copy_current_layer), ("Reset layer", self.reset_current_layer),
        ):
            ttk.Button(curve_title, text=text, command=command).pack(side=tk.RIGHT, padx=2)

        self.figure, self.axes = plt.subplots(figsize=(8, 4))
        self.canvas = FigureCanvasTkAgg(self.figure, master=curve_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        self.canvas.mpl_connect("button_press_event", self._curve_mouse_pressed)
        self.canvas.mpl_connect("motion_notify_event", self._curve_mouse_moved)
        self.canvas.mpl_connect("button_release_event", self._curve_mouse_released)

        key_frame = ttk.LabelFrame(curve_frame, text="Cubic keys")
        key_frame.pack(fill=tk.X, pady=5)
        columns = ("time", "value", "tangent_in", "tangent_out")
        self.key_tree = ttk.Treeview(key_frame, columns=columns, show="headings", height=6)
        for name in columns:
            self.key_tree.heading(name, text=name)
            self.key_tree.column(name, width=115, anchor=tk.E)
        self.key_tree.grid(row=0, column=0, rowspan=4, sticky="nsew", padx=4, pady=4)
        self.key_tree.bind("<<TreeviewSelect>>", self._key_selected)
        self.key_vars = {name: tk.StringVar() for name in columns}
        for row, name in enumerate(columns):
            ttk.Label(key_frame, text=name).grid(row=row, column=1, sticky="e", padx=4)
            entry = ttk.Entry(key_frame, textvariable=self.key_vars[name], width=18)
            entry.grid(row=row, column=2, padx=4)
            entry.bind("<Return>", lambda _event: self.apply_key())
            entry.bind("<KP_Enter>", lambda _event: self.apply_key())
            entry.bind("<FocusOut>", lambda _event: self.apply_key(False))
            self.key_entries[name] = entry
        button_row = ttk.Frame(key_frame)
        button_row.grid(row=4, column=0, columnspan=3, sticky="w", padx=4, pady=4)
        ttk.Button(button_row, text="Apply key", command=self.apply_key).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_row, text="Add key at sample", command=self.add_key).pack(side=tk.LEFT, padx=2)
        ttk.Button(button_row, text="Remove key", command=self.remove_key).pack(side=tk.LEFT, padx=2)
        self.key_hint = ttk.Label(key_frame, text="", foreground="#666666")
        self.key_hint.grid(row=5, column=0, columnspan=3, sticky="w", padx=6, pady=(0, 4))
        key_frame.columnconfigure(0, weight=1)

        self._build_acceleration_preview(acceleration_frame)

    def _build_acceleration_preview(self, parent: ttk.Frame) -> None:
        controls = ttk.LabelFrame(
            parent, text="Straight-line speed preview (boost chain overrides Boost state)")
        controls.pack(fill=tk.X, padx=5, pady=5)
        self.preview_start_speed = tk.StringVar(value="0.0")
        self.preview_frame_perfect_boosting = tk.BooleanVar(value=False)
        ttk.Label(controls, text="Start km/h").grid(row=0, column=0, padx=4)
        start_entry = ttk.Entry(controls, textvariable=self.preview_start_speed, width=10)
        start_entry.grid(row=0, column=1, sticky="ew", padx=4)
        start_entry.bind("<Return>", lambda _event: self.refresh_acceleration_preview())
        start_entry.bind("<KP_Enter>", lambda _event: self.refresh_acceleration_preview())
        start_entry.bind("<FocusOut>", lambda _event: self.refresh_acceleration_preview())
        ttk.Checkbutton(
            controls, text="Frame-perfect boosting",
            variable=self.preview_frame_perfect_boosting,
            command=self.refresh_acceleration_preview,
        ).grid(row=1, column=0, columnspan=2, sticky="w", padx=4, pady=(4, 0))
        controls.columnconfigure(1, weight=1)

        self.acceleration_figure, self.acceleration_axes = plt.subplots(figsize=(8, 5))
        self.acceleration_canvas = FigureCanvasTkAgg(self.acceleration_figure, master=parent)
        self.acceleration_canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)
        self.acceleration_result = ttk.Label(parent, text="", anchor="center")
        self.acceleration_result.pack(fill=tk.X, padx=6, pady=6)

    def _stat_tab(self, title: str, names: tuple[str, ...], callback):
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text=title)
        listing = tk.Listbox(frame, exportselection=False)
        for name in names:
            listing.insert(tk.END, name)
        listing.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        listing.bind("<<ListboxSelect>>", callback)
        return listing

    def _layer_stat_tab(self, title: str, layers: tuple[str, ...], callback):
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text=title)
        layer_var = tk.StringVar(value=layers[0])
        combo = ttk.Combobox(frame, textvariable=layer_var, values=layers, state="readonly")
        combo.pack(fill=tk.X, padx=4, pady=4)
        listing = tk.Listbox(frame, exportselection=False)
        for name in LIVE_MODIFIER_STATS:
            listing.insert(tk.END, name)
        listing.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        listing.layer_var = layer_var
        listing.bind("<<ListboxSelect>>", callback)
        combo.bind("<<ComboboxSelected>>", lambda _e: callback(None))
        return listing

    def _build_s_boost_tab(self) -> None:
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="S-BOOST overrides")
        self.sboost_tree = ttk.Treeview(frame, columns=("stat", "value"), show="headings")
        self.sboost_tree.heading("stat", text="stat")
        self.sboost_tree.heading("value", text="absolute value")
        self.sboost_tree.column("stat", width=220)
        self.sboost_tree.column("value", width=110, anchor=tk.E)
        self.sboost_tree.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        self.sboost_tree.bind("<<TreeviewSelect>>", self._sboost_selected)
        row = ttk.Frame(frame)
        row.pack(fill=tk.X, padx=4, pady=4)
        self.sboost_value = tk.StringVar()
        ttk.Entry(row, textvariable=self.sboost_value).pack(side=tk.LEFT, fill=tk.X, expand=True)
        ttk.Button(row, text="Apply", command=self.apply_sboost).pack(side=tk.LEFT, padx=4)

    def _build_corners_tab(self) -> None:
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="Corners and flags")
        grid = ttk.Frame(frame)
        grid.pack(fill=tk.BOTH, expand=True, padx=4, pady=4)
        for col, name in enumerate(("x", "y", "z"), start=2):
            ttk.Label(grid, text=name).grid(row=0, column=col)
        for index in range(8):
            group = "tilt" if index < 4 else "wall"
            corner = index if index < 4 else index - 4
            ttk.Label(grid, text=f"{group} {corner}").grid(row=index + 1, column=0, sticky="e")
            vars_for_corner = [tk.StringVar() for _ in range(3)]
            self.corner_vars.append(vars_for_corner)
            for axis, var in enumerate(vars_for_corner):
                ttk.Entry(grid, textvariable=var, width=11).grid(row=index + 1, column=axis + 2, padx=2)
        ttk.Label(grid, text="state_flags").grid(row=10, column=0, sticky="e")
        self.state_flags_var = tk.StringVar()
        ttk.Entry(grid, textvariable=self.state_flags_var).grid(row=10, column=2, columnspan=3, sticky="ew")
        ttk.Button(grid, text="Apply corners/flags", command=self.apply_fixed_data).grid(row=11, column=0, columnspan=3, pady=6)
        ttk.Button(grid, text="Auto wall from tilt", command=self.auto_wall_from_tilt).grid(row=11, column=3, columnspan=2, pady=6)

    def _build_status(self) -> None:
        self.status_var = tk.StringVar(value="")
        ttk.Label(self.master, textvariable=self.status_var, anchor="w").pack(fill=tk.X, padx=6, pady=4)

    def _selected_name(self, listing: tk.Listbox) -> str | None:
        selection = listing.curselection()
        return str(listing.get(selection[0])) if selection else None

    def _select_base(self, _event) -> None:
        name = self._selected_name(self.base_list)
        if name:
            self.select_curve("base", name)

    def _select_technique(self, _event) -> None:
        name = self._selected_name(self.technique_list)
        if name:
            self.select_curve(self.technique_list.layer_var.get(), name)

    def _select_boost(self, _event) -> None:
        name = self._selected_name(self.boost_list)
        if name:
            self.select_curve(self.boost_list.layer_var.get(), name)

    def select_curve(self, layer: str, stat: str) -> None:
        self.current_layer = layer
        self.current_stat = stat
        self.refresh_curve(0)

    def current_curve(self) -> Curve:
        return self.properties.curves[self.current_layer][self.current_stat]

    def refresh_curve(self, select_index: int | None = None) -> None:
        if not hasattr(self, "key_tree"):
            return
        curve = self.current_curve()
        if select_index is None:
            selected = self.key_tree.selection()
            select_index = int(selected[0]) if selected else None
        self.curve_title.config(text=f"{self.current_layer} / {self.current_stat}")
        self.key_tree.delete(*self.key_tree.get_children())
        for index, key in enumerate(curve.keys):
            self.key_tree.insert("", tk.END, iid=str(index), values=(
                f"{key.time:.9g}", f"{key.value:.9g}",
                f"{key.tangent_in:.9g}", f"{key.tangent_out:.9g}"))

        xs = [index / 256.0 for index in range(257)]
        ys = [curve.sample(x) for x in xs]
        sample = min(max(float(self.sample_var.get()), 0.0), 1.0)
        self.axes.clear()
        self.axes.plot(xs, ys, label="cubic spline")
        for index, (left, right) in enumerate(zip(curve.keys, curve.keys[1:])):
            dt = right.time - left.time
            outgoing = (
                left.time + dt / 3.0,
                left.value + dt * left.tangent_out / 3.0,
            )
            incoming = (
                right.time - dt / 3.0,
                right.value - dt * right.tangent_in / 3.0,
            )
            self.axes.plot(
                [left.time, outgoing[0]], [left.value, outgoing[1]],
                color="#8e44ad", linestyle="--", linewidth=1.0, alpha=0.85,
            )
            self.axes.plot(
                [right.time, incoming[0]], [right.value, incoming[1]],
                color="#008b8b", linestyle="--", linewidth=1.0, alpha=0.85,
            )
            self.axes.scatter(
                [outgoing[0]], [outgoing[1]], color="#8e44ad", marker=">", zorder=4,
                label="outgoing handle" if index == 0 else None,
            )
            self.axes.scatter(
                [incoming[0]], [incoming[1]], color="#008b8b", marker="<", zorder=4,
                label="incoming handle" if index == 0 else None,
            )
        self.axes.scatter([key.time for key in curve.keys], [key.value for key in curve.keys], color="red", zorder=3)
        self.selected_key_artist = self.axes.scatter(
            [], [], s=150, facecolors="none", edgecolors="#ffbf00",
            linewidths=2.5, zorder=5,
        )
        self.selected_in_handle_artist = self.axes.scatter(
            [], [], s=150, facecolors="none", edgecolors="#ffbf00",
            marker="<", linewidths=2.0, zorder=6,
        )
        self.selected_out_handle_artist = self.axes.scatter(
            [], [], s=150, facecolors="none", edgecolors="#ffbf00",
            marker=">", linewidths=2.0, zorder=6,
        )
        self.axes.axvline(sample, color="orange", alpha=0.7)
        self.axes.grid(True)
        self.axes.set_xlim(0.0, 1.0)
        self.axes.set_title(f"sample={curve.sample(sample):.7g}")
        if len(curve.keys) > 1:
            self.axes.legend(loc="best")
        self.canvas.draw_idle()
        self._update_composed_label(sample)
        self.refresh_acceleration_preview()
        if select_index is not None and 0 <= select_index < len(curve.keys):
            iid = str(select_index)
            self.key_tree.selection_set(iid)
            self.key_tree.focus(iid)
            self.key_tree.see(iid)
            self._show_key(select_index)
        else:
            self._clear_key_fields()

    def _update_composed_label(self, setting: float) -> None:
        name = self.current_stat
        base = self.properties.curves["base"][name].sample(setting)
        value = self._sample_composed_stats(setting)[name]
        self.composed_label.config(text=f"base {base:.7g}   composed {value:.7g}")

    def _sample_composed_stats(
            self, setting: float, boost_override: str | None = None) -> dict[str, float]:
        setting = min(max(setting, 0.0), 1.0)
        boost = self.compose_boost.get() if boost_override is None else boost_override
        technique = self.compose_technique.get()
        intensity = min(max(float(self.technique_intensity.get()), 0.0), 1.0)
        layer = None
        if boost == "none":
            layer = "no_boost"
        elif boost == "manual":
            layer = "manual_boost"
        elif boost == "dashplate":
            layer = "dashplate_boost"
        elif boost == "stacked":
            layer = "stacked_boost"
        elif boost == "s_boost_dashplate":
            layer = "dashplate_boost"

        result: dict[str, float] = {}
        for name in STAT_NAMES:
            supports = name in LIVE_MODIFIER_STATS
            base = self.properties.curves["base"][name].sample(setting)
            value = (self.properties.s_boost_overrides[name]
                     if supports and boost.startswith("s_boost") else base)
            if supports and technique in TECHNIQUE_LAYERS:
                authored = self.properties.curves[technique][name].sample(setting)
                value *= 1.0 + (authored - 1.0) * intensity
            if supports and layer is not None:
                value *= self.properties.curves[layer][name].sample(setting)
            result[name] = value
        return result

    @staticmethod
    def _simulate_straight_line_speed(
            no_boost_stats: dict[str, float], manual_boost_stats: dict[str, float],
            starting_speed_kmh: float, frame_perfect_boosting: bool, s_boost_active: bool,
            duration_seconds: float = 30.0,
            ) -> tuple[list[float], list[float], list[float], int]:
        tick_rate = 60.0
        weight = no_boost_stats["weight_kg"]
        speed = starting_speed_kmh * weight / 216.0
        base_speed = speed / weight
        turbo = 0.0
        manual_frames = 0
        boost_count = 0
        times: list[float] = []
        speeds: list[float] = []
        turbos: list[float] = []

        for frame in range(int(duration_seconds * tick_rate)):
            started_manual_boost = False
            if frame_perfect_boosting and manual_frames == 0:
                duration = max(manual_boost_stats["manual_boost_duration_seconds"], 0.0)
                manual_frames = max(int(duration * tick_rate + 0.5), 0)
                if manual_frames > 0:
                    turbo += manual_boost_stats["manual_turbo_gain"]
                    boost_count += 1
                    started_manual_boost = True

            manual_active = manual_frames > 0
            stats = manual_boost_stats if manual_active else no_boost_stats
            turbo -= (stats["turbo_flat_loss_per_second"]
                      + turbo * stats["turbo_percent_loss_per_second"]) / tick_rate
            turbo = max(turbo, 0.0)

            target_speed = (40.0 * stats["acceleration"]) / 348.0
            target_speed *= stats["drive_target_speed_multiplier"]
            target_speed += base_speed
            normalized_speed = speed / weight
            speed_difference = target_speed - normalized_speed

            denominator = (36.0 + 40.0 * stats["max_speed"]
                           + turbo * stats["turbo_top_speed_effect"])
            speed_factor = max(target_speed / denominator, 0.0) if abs(denominator) > 0.0001 else 0.0
            accel_magnitude = (speed_factor * 4.0 * stats["acceleration"]
                               * (0.6 + stats["acceleration"])
                               * stats["acceleration_response_multiplier"])
            if started_manual_boost:
                accel_magnitude = 0.0
            final_accel = speed_difference * accel_magnitude
            new_base_speed = target_speed - final_accel
            released_target_speed = base_speed
            released_speed_difference = released_target_speed - normalized_speed
            released_speed_factor = (
                max(released_target_speed / denominator, 0.0)
                if abs(denominator) > 0.0001 else 0.0)
            released_accel_magnitude = (
                released_speed_factor * 4.0 * stats["acceleration"]
                * (0.6 + stats["acceleration"])
                * stats["acceleration_response_multiplier"])
            if started_manual_boost:
                released_accel_magnitude = 0.0
            released_final_accel = (
                released_speed_difference * released_accel_magnitude * 0.05)
            released_new_base_speed = (
                released_target_speed - released_final_accel)
            if released_new_base_speed > new_base_speed:
                target_speed = released_target_speed
                speed_difference = released_speed_difference
                speed_factor = released_speed_factor
                accel_magnitude = released_accel_magnitude
                final_accel = released_final_accel
                new_base_speed = released_new_base_speed
            base_speed = max(new_base_speed - stats["drag"], 0.0)
            if s_boost_active:
                base_speed += stats["s_boost_base_speed_add_per_second"] / tick_rate

            thrust = (1000.0 * stats["forward_thrust_multiplier"]
                      * speed_difference)
            if normalized_speed < 0.0 or speed_difference < 0.0:
                thrust *= 0.15
            speed += thrust

            speed_weight_ratio = speed / weight
            speed_kmh = 216.0 * speed_weight_ratio
            if speed_kmh < 2.0:
                speed = 0.0
                speed_kmh = 0.0
            else:
                speed -= speed_weight_ratio * speed_weight_ratio * 8.0
                speed_kmh = 216.0 * speed / weight

            times.append(frame / tick_rate)
            speeds.append(speed_kmh)
            turbos.append(turbo)

            if manual_active:
                manual_frames -= 1

        return times, speeds, turbos, boost_count

    def refresh_acceleration_preview(self) -> None:
        if not hasattr(self, "acceleration_axes"):
            return
        try:
            setting = min(max(float(self.sample_var.get()), 0.0), 1.0)
            starting_speed = float(self.preview_start_speed.get())
            frame_perfect_boosting = bool(self.preview_frame_perfect_boosting.get())
            if frame_perfect_boosting:
                no_boost_stats = self._sample_composed_stats(setting, "none")
                manual_boost_stats = self._sample_composed_stats(setting, "manual")
                s_boost_active = False
            else:
                no_boost_stats = self._sample_composed_stats(setting)
                manual_boost_stats = no_boost_stats
                s_boost_active = self.compose_boost.get().startswith("s_boost")
            times, speeds, turbos, boost_count = self._simulate_straight_line_speed(
                no_boost_stats, manual_boost_stats, starting_speed,
                frame_perfect_boosting, s_boost_active)
            terminal_speed = speeds[-1]
            peak_speed = max(speeds)
            peak_turbo = max(turbos)
            tolerance = max(abs(terminal_speed) * 0.001, 0.01)
            settle_index = len(speeds) - 1
            for index in range(len(speeds) - 1, -1, -1):
                if abs(speeds[index] - terminal_speed) > tolerance:
                    settle_index = min(index + 1, len(speeds) - 1)
                    break
                settle_index = index
            settle_time = times[settle_index]

            self.acceleration_axes.clear()
            self.acceleration_axes.plot(times, speeds, color="#2878b5", linewidth=2.0)
            self.acceleration_axes.axhline(
                terminal_speed, color="#d35400", linestyle="--", linewidth=1.0,
                label=f"terminal {terminal_speed:.2f} km/h")
            if frame_perfect_boosting:
                self.acceleration_axes.axhline(
                    peak_speed, color="#8e44ad", linestyle=":", linewidth=1.0,
                    label=f"peak {peak_speed:.2f} km/h")
            self.acceleration_axes.set_ylim(bottom=0.0)
            self.acceleration_axes.set_xlabel("Time (s)")
            self.acceleration_axes.set_ylabel("Speed (km/h)")
            self.acceleration_axes.set_title(
                f"Machine setting {setting:.3f}"
                + (" — frame-perfect boosts" if frame_perfect_boosting else ""))
            self.acceleration_axes.grid(True)
            self.acceleration_axes.legend(loc="best")
            self.acceleration_canvas.draw_idle()
            if frame_perfect_boosting:
                result_text = (
                    f"Peak speed: {peak_speed:.3f} km/h    "
                    f"terminal: {terminal_speed:.3f} km/h    "
                    f"boosts: {boost_count}    peak turbo: {peak_turbo:.3f}    "
                    "energy: infinite")
            else:
                result_text = (
                    f"Top speed: {terminal_speed:.3f} km/h    "
                    f"99.9% settled: {settle_time:.2f} s    "
                    f"acceleration: {no_boost_stats['acceleration']:.6g}    "
                    f"max speed: {no_boost_stats['max_speed']:.6g}")
            self.acceleration_result.config(text=result_text)
        except Exception as exc:
            self.acceleration_result.config(text=f"Preview error: {exc}")

    def _key_selected(self, _event) -> None:
        selected = self.key_tree.selection()
        if not selected:
            return
        self._show_key(int(selected[0]))

    def _show_key(self, index: int) -> None:
        curve = self.current_curve()
        key = curve.keys[index]
        for name in self.key_vars:
            self.key_vars[name].set(f"{getattr(key, name):.9g}")

        has_incoming = len(curve.keys) > 1 and index > 0
        has_outgoing = len(curve.keys) > 1 and index + 1 < len(curve.keys)
        self.key_entries["time"].state(["!disabled"])
        self.key_entries["value"].state(["!disabled"])
        self.key_entries["tangent_in"].state(["!disabled"] if has_incoming else ["disabled"])
        self.key_entries["tangent_out"].state(["!disabled"] if has_outgoing else ["disabled"])
        if len(curve.keys) == 1:
            hint = "Constant one-key curve: add another key before tangents can affect it."
        elif not has_incoming:
            hint = "First key: outgoing tangent controls the segment to the next key."
        elif not has_outgoing:
            hint = "Last key: incoming tangent controls the segment from the previous key."
        else:
            hint = "Incoming controls the previous segment; outgoing controls the next segment."
        self.key_hint.config(text=hint)
        self.selected_key_artist.set_offsets([[key.time, key.value]])
        self.selected_key_artist.set_visible(True)
        handles = self._selected_curve_handles(index)
        incoming = handles.get("tangent_in")
        outgoing = handles.get("tangent_out")
        self.selected_in_handle_artist.set_offsets(
            [incoming] if incoming else [(math.nan, math.nan)])
        self.selected_in_handle_artist.set_visible(incoming is not None)
        self.selected_out_handle_artist.set_offsets(
            [outgoing] if outgoing else [(math.nan, math.nan)])
        self.selected_out_handle_artist.set_visible(outgoing is not None)
        self.canvas.draw_idle()

    def _clear_key_fields(self) -> None:
        for variable in self.key_vars.values():
            variable.set("")
        for entry in self.key_entries.values():
            entry.state(["disabled"])
        self.key_hint.config(text="Select a key to edit it.")
        if hasattr(self, "selected_key_artist"):
            self.selected_key_artist.set_visible(False)
            self.selected_in_handle_artist.set_visible(False)
            self.selected_out_handle_artist.set_visible(False)
            self.canvas.draw_idle()

    def _selected_curve_handles(self, index: int) -> dict[str, tuple[float, float]]:
        curve = self.current_curve()
        key = curve.keys[index]
        handles: dict[str, tuple[float, float]] = {}
        if index > 0:
            previous = curve.keys[index - 1]
            dt = key.time - previous.time
            handles["tangent_in"] = (
                key.time - dt / 3.0,
                key.value - dt * key.tangent_in / 3.0,
            )
        if index + 1 < len(curve.keys):
            following = curve.keys[index + 1]
            dt = following.time - key.time
            handles["tangent_out"] = (
                key.time + dt / 3.0,
                key.value + dt * key.tangent_out / 3.0,
            )
        return handles

    def _curve_mouse_pressed(self, event) -> None:
        if event.button != 1 or event.inaxes is not self.axes or event.x is None or event.y is None:
            return
        selected = self.key_tree.selection()
        if not selected:
            return
        index = int(selected[0])
        key = self.current_curve().keys[index]
        candidates: list[tuple[str, tuple[float, float]]] = [
            ("key", (key.time, key.value)),
            *self._selected_curve_handles(index).items(),
        ]
        closest_kind = None
        closest_distance_squared = 12.0 * 12.0
        for kind, position in candidates:
            display_x, display_y = self.axes.transData.transform(position)
            distance_squared = ((display_x - event.x) ** 2 + (display_y - event.y) ** 2)
            if distance_squared <= closest_distance_squared:
                closest_kind = kind
                closest_distance_squared = distance_squared
        self.curve_drag_kind = closest_kind

    def _curve_mouse_moved(self, event) -> None:
        if (self.curve_drag_kind is None or event.inaxes is not self.axes
                or event.xdata is None or event.ydata is None
                or not math.isfinite(event.xdata) or not math.isfinite(event.ydata)):
            return
        selected = self.key_tree.selection()
        if not selected:
            self.curve_drag_kind = None
            return
        index = int(selected[0])
        curve = self.current_curve()
        keys = list(curve.keys)
        key = keys[index]

        if self.curve_drag_kind == "key":
            lower = (math.nextafter(keys[index - 1].time, math.inf)
                     if index > 0 else 0.0)
            upper = (math.nextafter(keys[index + 1].time, -math.inf)
                     if index + 1 < len(keys) else 1.0)
            if lower > upper:
                return
            keys[index] = CurveKey(
                min(max(float(event.xdata), lower), upper), float(event.ydata),
                key.tangent_in, key.tangent_out,
            )
        else:
            handle = self._selected_curve_handles(index).get(self.curve_drag_kind)
            if handle is None:
                self.curve_drag_kind = None
                return
            horizontal = handle[0] - key.time
            if abs(horizontal) <= 1.0e-9:
                return
            tangent = (float(event.ydata) - key.value) / horizontal
            keys[index] = CurveKey(
                key.time, key.value,
                tangent if self.curve_drag_kind == "tangent_in" else key.tangent_in,
                tangent if self.curve_drag_kind == "tangent_out" else key.tangent_out,
            )

        self.properties.curves[self.current_layer][self.current_stat] = Curve(keys)
        self.refresh_curve(index)

    def _curve_mouse_released(self, _event) -> None:
        if self.curve_drag_kind is not None:
            self.status_var.set(
                f"Updated {self.current_layer}/{self.current_stat} by dragging the graph")
        self.curve_drag_kind = None

    def apply_key(self, show_error: bool = True) -> bool:
        selected = self.key_tree.selection()
        if not selected:
            if show_error:
                self.status_var.set("Select a key before applying key values.")
            return False
        try:
            index = int(selected[0])
            replacement = CurveKey(*(float(self.key_vars[name].get()) for name in
                                     ("time", "value", "tangent_in", "tangent_out")))
            keys = list(self.current_curve().keys)
            keys[index] = replacement
            keys.sort(key=lambda key: key.time)
            candidate = Curve(keys)
            validate_properties(self._properties_with_current_curve(candidate))
            self.properties.curves[self.current_layer][self.current_stat] = candidate
            new_index = keys.index(replacement)
            self.refresh_curve(new_index)
            self.status_var.set(
                f"Applied key {new_index} to {self.current_layer}/{self.current_stat}")
            return True
        except Exception as exc:
            self.status_var.set(f"Invalid key: {exc}")
            if show_error:
                messagebox.showerror("Invalid key", str(exc))
            return False

    def _properties_with_current_curve(self, curve: Curve) -> CarProperties:
        candidate = deepcopy(self.properties)
        candidate.curves[self.current_layer][self.current_stat] = curve
        return candidate

    def add_key(self) -> None:
        curve = self.current_curve()
        time = min(max(float(self.sample_var.get()), 0.0), 1.0)
        if any(abs(key.time - time) < 1.0e-6 for key in curve.keys):
            gaps = [(right.time - left.time, (left.time + right.time) * 0.5)
                    for left, right in zip(curve.keys, curve.keys[1:])]
            time = max(gaps)[1] if gaps else (0.5 if curve.keys[0].time != 0.5 else 1.0)
        value = curve.sample(time)
        derivative = curve.derivative(time)
        new_key = CurveKey(time, value, derivative, derivative)
        keys = list(curve.keys) + [new_key]
        keys.sort(key=lambda key: key.time)
        self.properties.curves[self.current_layer][self.current_stat] = Curve(keys)
        self.refresh_curve(keys.index(new_key))

    def remove_key(self) -> None:
        selected = self.key_tree.selection()
        curve = self.current_curve()
        if not selected or len(curve.keys) == 1:
            return
        index = int(selected[0])
        keys = list(curve.keys)
        del keys[index]
        self.properties.curves[self.current_layer][self.current_stat] = Curve(keys)
        self.refresh_curve(min(index, len(keys) - 1))

    def copy_curve(self) -> None:
        self.curve_clipboard = deepcopy(self.current_curve())

    def paste_curve(self) -> None:
        if self.curve_clipboard is not None:
            self.properties.curves[self.current_layer][self.current_stat] = deepcopy(self.curve_clipboard)
            self.refresh_curve()

    def reset_selected_modifier(self) -> None:
        if self.current_layer != "base":
            self.properties.curves[self.current_layer][self.current_stat] = Curve.constant(1.0)
            self.refresh_curve()

    def copy_current_layer(self) -> None:
        if self.current_layer == "base":
            messagebox.showinfo("Copy layer", "Base curves cannot be copied into a modifier layer.")
            return
        target = simpledialog.askstring("Copy layer", "Target layer:\n" + ", ".join(MODIFIER_LAYER_NAMES))
        if target not in MODIFIER_LAYER_NAMES:
            return
        self.properties.curves[target] = deepcopy(self.properties.curves[self.current_layer])
        self.status_var.set(f"Copied {self.current_layer} to {target}")

    def reset_current_layer(self) -> None:
        if self.current_layer == "base":
            return
        self.properties.curves[self.current_layer] = {
            name: Curve.constant(1.0) for name in LIVE_MODIFIER_STATS
        }
        self.refresh_curve()

    def _refresh_sboost(self) -> None:
        self.sboost_tree.delete(*self.sboost_tree.get_children())
        for name in LIVE_MODIFIER_STATS:
            self.sboost_tree.insert("", tk.END, iid=name,
                                    values=(name, f"{self.properties.s_boost_overrides[name]:.9g}"))

    def _sboost_selected(self, _event) -> None:
        selected = self.sboost_tree.selection()
        if selected:
            self.sboost_value.set(f"{self.properties.s_boost_overrides[selected[0]]:.9g}")

    def apply_sboost(self) -> None:
        selected = self.sboost_tree.selection()
        if not selected:
            return
        try:
            self.properties.s_boost_overrides[selected[0]] = float(self.sboost_value.get())
            validate_properties(self.properties)
            self._refresh_sboost()
            self.refresh_curve()
        except Exception as exc:
            messagebox.showerror("Invalid S-BOOST value", str(exc))

    def _load_fixed_data(self) -> None:
        for vars_for_corner, values in zip(
                self.corner_vars, self.properties.tilt_corners + self.properties.wall_corners):
            for var, value in zip(vars_for_corner, values):
                var.set(f"{value:.9g}")
        self.state_flags_var.set(str(self.properties.state_flags))

    def apply_fixed_data(self) -> bool:
        try:
            corners = [tuple(float(var.get()) for var in vars_for_corner)
                       for vars_for_corner in self.corner_vars]
            self.properties.tilt_corners = corners[:4]
            self.properties.wall_corners = corners[4:]
            self.properties.state_flags = int(self.state_flags_var.get(), 0) & 0xFFFFFFFF
            validate_properties(self.properties)
            return True
        except Exception as exc:
            messagebox.showerror("Invalid fixed data", str(exc))
            return False

    def auto_wall_from_tilt(self) -> None:
        if not self.apply_fixed_data():
            return
        wall = []
        for x, _y, z in self.properties.tilt_corners:
            wall.append((x + (0.2 if x >= 0.0 else -0.2), -0.1,
                         z + (0.2 if z >= 0.0 else -0.2)))
        self.properties.wall_corners = wall
        self._load_fixed_data()

    def _load_properties_into_ui(self) -> None:
        self._refresh_sboost()
        self._load_fixed_data()
        self.base_list.selection_clear(0, tk.END)
        self.base_list.selection_set(0)
        self.select_curve("base", STAT_NAMES[0])
        self._update_title()

    def _update_title(self) -> None:
        suffix = str(self.current_path) if self.current_path else "untitled"
        self.master.title(f"MXT Car Properties Curve Editor - {suffix}")

    def new_file(self) -> None:
        self.properties = default_properties()
        self.current_path = None
        self._load_properties_into_ui()

    def load_binary(self) -> None:
        path = filedialog.askopenfilename(filetypes=[("MXT Car Props", "*.mxt_car_props")])
        if not path:
            return
        try:
            self.properties = read_binary(Path(path).read_bytes())
            self.current_path = Path(path)
            self._load_properties_into_ui()
            self.show_validation(False)
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))

    def save_binary(self, choose_path: bool = False) -> None:
        if self.key_tree.selection() and not self.apply_key():
            return
        if not self.apply_fixed_data():
            return
        path = self.current_path
        if choose_path or path is None:
            chosen = filedialog.asksaveasfilename(defaultextension=".mxt_car_props",
                                                  filetypes=[("MXT Car Props", "*.mxt_car_props")])
            if not chosen:
                return
            path = Path(chosen)
        try:
            path.write_bytes(write_binary(self.properties))
            self.current_path = path
            self._update_title()
            self.status_var.set(f"Saved {path} ({path.stat().st_size} bytes)")
        except Exception as exc:
            messagebox.showerror("Save failed", str(exc))

    def import_json(self) -> None:
        path = filedialog.askopenfilename(filetypes=[("JSON", "*.json")])
        if not path:
            return
        try:
            self.properties = properties_from_json(Path(path).read_text(encoding="utf-8"))
            self.current_path = None
            self._load_properties_into_ui()
        except Exception as exc:
            messagebox.showerror("JSON import failed", str(exc))

    def export_json(self) -> None:
        if self.key_tree.selection() and not self.apply_key():
            return
        if not self.apply_fixed_data():
            return
        path = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("JSON", "*.json")])
        if path:
            try:
                Path(path).write_text(properties_to_json(self.properties) + "\n", encoding="utf-8")
            except Exception as exc:
                messagebox.showerror("JSON export failed", str(exc))

    def show_validation(self, popup: bool = True) -> None:
        try:
            if self.key_tree.selection() and not self.apply_key():
                return
            warnings = property_warnings(self.properties)
            text = "Valid" if not warnings else "Valid with warnings: " + "; ".join(warnings)
            self.status_var.set(text)
            if popup:
                messagebox.showwarning("Validation", text) if warnings else messagebox.showinfo("Validation", text)
        except Exception as exc:
            self.status_var.set(f"Invalid: {exc}")
            if popup:
                messagebox.showerror("Validation", str(exc))


def main() -> int:
    root = tk.Tk()
    CarPropsEditor(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
