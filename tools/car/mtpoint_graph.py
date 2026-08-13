"""Machine-setting acceleration graph using the authoritative spline codec."""

from __future__ import annotations

from pathlib import Path
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from mxt_car_props_format import LIVE_MODIFIER_STATS, CarProperties, read_binary  # noqa: E402


class CarSimApp:
    def __init__(self, master: tk.Tk):
        self.master = master
        self.master.title("MXT Machine Speed Grapher")
        self.properties: CarProperties | None = None
        self.figure, self.axes = plt.subplots()
        self.canvas = FigureCanvasTkAgg(self.figure, master=master)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        controls = ttk.Frame(master)
        controls.pack(fill=tk.X, padx=6, pady=6)
        ttk.Button(controls, text="Load MXT Car Props", command=self.load_file).grid(row=0, column=0, padx=4)
        self.machine_setting = tk.DoubleVar(value=0.5)
        self.input_accel = tk.DoubleVar(value=1.0)
        self.starting_speed = tk.StringVar(value="0.0")
        self.technique = tk.StringVar(value="none")
        self.technique_intensity = tk.DoubleVar(value=1.0)
        self.boost_state = tk.StringVar(value="no_boost")
        ttk.Label(controls, text="Machine setting").grid(row=0, column=1)
        ttk.Scale(controls, from_=0.0, to=1.0, variable=self.machine_setting,
                  command=self.update_graph).grid(row=0, column=2, sticky="ew")
        ttk.Label(controls, text="Throttle").grid(row=0, column=3)
        ttk.Scale(controls, from_=0.0, to=1.0, variable=self.input_accel,
                  command=self.update_graph).grid(row=0, column=4, sticky="ew")
        ttk.Label(controls, text="Start km/h").grid(row=1, column=0)
        start_entry = ttk.Entry(controls, textvariable=self.starting_speed, width=10)
        start_entry.grid(row=1, column=1)
        start_entry.bind("<Return>", self.update_graph)
        ttk.Label(controls, text="Technique").grid(row=1, column=2)
        technique_combo = ttk.Combobox(controls, textvariable=self.technique,
                                       values=("none", "mts", "quickturn"), state="readonly")
        technique_combo.grid(row=1, column=3)
        technique_combo.bind("<<ComboboxSelected>>", self.update_graph)
        ttk.Label(controls, text="Intensity").grid(row=1, column=4)
        ttk.Scale(controls, from_=0.0, to=1.0, variable=self.technique_intensity,
                  command=self.update_graph).grid(row=1, column=5, sticky="ew")
        ttk.Label(controls, text="Boost layer").grid(row=2, column=0)
        boost_combo = ttk.Combobox(
            controls, textvariable=self.boost_state,
            values=("no_boost", "manual_boost", "dashplate_boost", "stacked_boost", "s_boost", "s_boost_dashplate"),
            state="readonly")
        boost_combo.grid(row=2, column=1, columnspan=2, sticky="ew")
        boost_combo.bind("<<ComboboxSelected>>", self.update_graph)
        controls.columnconfigure(2, weight=1)
        controls.columnconfigure(4, weight=1)
        controls.columnconfigure(5, weight=1)
        self.status = ttk.Label(master, text="No file loaded")
        self.status.pack(fill=tk.X, padx=6, pady=4)

    def load_file(self) -> None:
        path = filedialog.askopenfilename(filetypes=[("MXT Car Props", "*.mxt_car_props")])
        if not path:
            return
        try:
            self.properties = read_binary(Path(path).read_bytes())
            self.status.config(text=Path(path).name)
            self.update_graph()
        except Exception as exc:
            messagebox.showerror("Load failed", str(exc))

    def sampled_stats(self) -> dict[str, float]:
        assert self.properties is not None
        setting = min(max(float(self.machine_setting.get()), 0.0), 1.0)
        boost_state = self.boost_state.get()
        use_s_boost = boost_state.startswith("s_boost")
        result: dict[str, float] = {}
        for name, curve in self.properties.curves["base"].items():
            supports = name in LIVE_MODIFIER_STATS
            value = self.properties.s_boost_overrides[name] if use_s_boost and supports else curve.sample(setting)
            technique = self.technique.get()
            if supports and technique in ("mts", "quickturn"):
                multiplier = self.properties.curves[technique][name].sample(setting)
                intensity = min(max(float(self.technique_intensity.get()), 0.0), 1.0)
                value *= 1.0 + (multiplier - 1.0) * intensity
            layer = boost_state
            if boost_state == "s_boost":
                layer = ""
            elif boost_state == "s_boost_dashplate":
                layer = "dashplate_boost"
            if supports and layer:
                value *= self.properties.curves[layer][name].sample(setting)
            result[name] = value
        return result

    @staticmethod
    def simulate(props: dict[str, float], starting_speed: float, input_accel: float,
                 duration: float = 30.0, dt: float = 1.0 / 60.0,
                 starting_base_speed: float | None = None) -> tuple[list[float], list[float], float]:
        speed = starting_speed * props["weight_kg"] / 216.0
        base_speed = speed / props["weight_kg"] if starting_base_speed is None else starting_base_speed
        times: list[float] = []
        speeds: list[float] = []
        for frame in range(int(duration / dt)):
            target = input_accel * (40.0 * props["acceleration"]) / 348.0 + base_speed
            normalized_speed = speed / props["weight_kg"]
            difference = target - normalized_speed
            denominator = 36.0 + 40.0 * props["max_speed"]
            speed_factor = max(target / denominator, 0.0) if abs(denominator) > 0.0001 else 0.0
            accel_magnitude = speed_factor * 4.0 * props["acceleration"] * (0.6 + props["acceleration"])
            final_accel = difference * accel_magnitude
            next_base = target - final_accel
            if next_base - base_speed < 0.0:
                next_base = base_speed - final_accel * 0.1
            base_speed = max(next_base - props["drag"], 0.0)
            thrust = 1000.0 * difference
            if difference < 0.0 or normalized_speed < 0.0:
                thrust *= 0.15
            speed += thrust
            ratio = speed / props["weight_kg"]
            if 216.0 * ratio < 2.0:
                speed = 0.0
            else:
                speed -= ratio * ratio * 8.0
            times.append(frame * dt)
            speeds.append(216.0 * speed / props["weight_kg"])
        return times, speeds, base_speed

    def update_graph(self, *_args) -> None:
        if self.properties is None:
            return
        try:
            start = float(self.starting_speed.get())
            props = self.sampled_stats()
            times, speeds, _base = self.simulate(props, start, float(self.input_accel.get()))
            self.axes.clear()
            self.axes.plot(times, speeds)
            self.axes.set_xlabel("Time (s)")
            self.axes.set_ylabel("Speed (km/h)")
            self.axes.grid(True)
            self.axes.set_ylim(bottom=0.0)
            self.axes.set_title(
                f"setting={self.machine_setting.get():.3f} accel={props['acceleration']:.5g} "
                f"max_speed={props['max_speed']:.5g}")
            self.canvas.draw_idle()
            final = speeds[-1]
            threshold = final * 0.999
            reach = next((time for time, speed in zip(times, speeds) if speed >= threshold), times[-1])
            self.status.config(text=f"Top speed {final:.3f} km/h; 99.9% in {reach:.2f}s; drag {props['drag']:.6g}")
        except Exception as exc:
            self.status.config(text=f"Graph error: {exc}")


def main() -> int:
    root = tk.Tk()
    CarSimApp(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
