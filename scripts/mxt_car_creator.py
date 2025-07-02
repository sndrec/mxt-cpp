import struct
import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg


class CarPropsEditor:
        def __init__(self, master, on_props_changed=None):
                self.master = master
                self.on_props_changed = on_props_changed

                self.fields_general = [
                        "weight_kg", "acceleration", "max_speed", "grip_1", "grip_2", "grip_3",
                        "turn_tension", "drift_accel", "turn_movement", "strafe_turn", "strafe",
                        "turn_reaction", "boost_strength", "boost_length", "turn_decel", "drag",
                        "body", "camera_reorienting", "camera_repositioning", "track_collision",
                        "obstacle_collision", "max_energy"
                ]

                self.tilt_summary_fields = ["front_width", "front_length", "back_width", "back_length"]
                self.wall_summary_fields = ["wall_front_width", "wall_front_length", "wall_back_width", "wall_back_length"]

                self.full_fields = []
                for group in ["tilt", "wall"]:
                        for i in range(4):
                                for axis in ["x", "y", "z"]:
                                        self.full_fields.append(f"{group}_corner_{i}_{axis}")

                self.u32_field = "unk_byte_0x48"
                self.entries = {}

                notebook = ttk.Notebook(master)
                notebook.pack(fill='both', expand=True)

                # Tabs
                general_tab = ttk.Frame(notebook)
                tilt_tab = ttk.Frame(notebook)
                wall_tab = ttk.Frame(notebook)
                meta_tab = ttk.Frame(notebook)

                notebook.add(general_tab, text='General')
                notebook.add(tilt_tab, text='Tilt Corners')
                notebook.add(wall_tab, text='Wall Corners')
                notebook.add(meta_tab, text='Metadata')

                # General fields
                for i, field in enumerate(self.fields_general):
                        label = tk.Label(general_tab, text=field)
                        label.grid(row=i, column=0, sticky='e')
                        entry = tk.Entry(general_tab)
                        entry.grid(row=i, column=1)
                        entry.bind("<KeyRelease>", lambda _e: self._notify_props())
                        self.entries[field] = entry

                # Tilt summary
                for i, field in enumerate(self.tilt_summary_fields):
                        label = tk.Label(tilt_tab, text=field)
                        label.grid(row=i, column=0, sticky='e')
                        entry = tk.Entry(tilt_tab)
                        entry.grid(row=i, column=1)
                        entry.bind("<KeyRelease>", lambda _e: self._notify_props())
                        self.entries[field] = entry

                # Wall summary
                for i, field in enumerate(self.wall_summary_fields):
                        label = tk.Label(wall_tab, text=field)
                        label.grid(row=i, column=0, sticky='e')
                        entry = tk.Entry(wall_tab)
                        entry.grid(row=i, column=1)
                        entry.bind("<KeyRelease>", lambda _e: self._notify_props())
                        self.entries[field] = entry

                # Full vector values (hidden by default, but editable if wanted)
                full_group = ttk.LabelFrame(meta_tab, text="Full Corner Vectors")
                full_group.pack(fill='both', expand=True, padx=10, pady=10)
                for i, field in enumerate(self.full_fields):
                        label = tk.Label(full_group, text=field)
                        label.grid(row=i, column=0, sticky='e')
                        entry = tk.Entry(full_group)
                        entry.grid(row=i, column=1)
                        entry.bind("<KeyRelease>", lambda _e: self._notify_props())
                        self.entries[field] = entry

                # Final u32 field
                u32_label = tk.Label(meta_tab, text=self.u32_field)
                u32_label.pack()
                u32_entry = tk.Entry(meta_tab)
                u32_entry.pack()
                u32_entry.bind("<KeyRelease>", lambda _e: self._notify_props())
                self.entries[self.u32_field] = u32_entry

                # Buttons
                btn_frame = tk.Frame(master)
                btn_frame.pack(pady=5)
                self.load_button = tk.Button(btn_frame, text="Load", command=self.load_file)
                self.load_button.pack(side='left', padx=10)
                self.save_button = tk.Button(btn_frame, text="Save", command=self.save_file)
                self.save_button.pack(side='left', padx=10)
                self.autoset_button = tk.Button(btn_frame, text="Auto-set Wall from Tilt", command=self.auto_set_wall)
                self.autoset_button.pack(side='left', padx=10)
                self.refresh_button = tk.Button(btn_frame, text="Refresh Graph", command=self._notify_props)
                self.refresh_button.pack(side='left', padx=10)

        def _notify_props(self):
                if self.on_props_changed:
                        self.on_props_changed()

        def load_file(self):
                path = filedialog.askopenfilename(filetypes=[("MXT Car Props", "*.mxt_car_props")])
                if not path:
                        return
                try:
                        with open(path, "rb") as f:
                                data = f.read()
                                floats = struct.unpack('<46f', data[:184])
                                u32 = struct.unpack('<I', data[184:188])[0]
                                all_keys = self.fields_general + self.full_fields
                                for i, key in enumerate(all_keys):
                                        self.entries[key].delete(0, tk.END)
                                        self.entries[key].insert(0, str(floats[i]))
                                self.entries[self.u32_field].delete(0, tk.END)
                                self.entries[self.u32_field].insert(0, str(u32))
                                self.update_summaries_from_full()
                                self._notify_props()
                except Exception as e:
                        messagebox.showerror("Error", f"Failed to load file: {e}")

        def save_file(self):
                self.update_full_from_summaries()
                path = filedialog.asksaveasfilename(defaultextension=".mxt_car_props",
                        filetypes=[("MXT Car Props", "*.mxt_car_props")])
                if not path:
                        return
                try:
                        float_values = [float(self.entries[key].get()) for key in self.fields_general + self.full_fields]
                        u32_value = int(self.entries[self.u32_field].get()) & 0xFFFFFFFF
                        with open(path, "wb") as f:
                                f.write(struct.pack('<46f', *float_values))
                                f.write(struct.pack('<I', u32_value))
                except Exception as e:
                        messagebox.showerror("Error", f"Failed to save file: {e}")

        def get_sim_props(self):
                return {k: float(self.entries[k].get()) for k in self.fields_general[:16]}

        def update_summaries_from_full(self):
                # Tilt
                self.entries["front_width"].delete(0, tk.END)
                self.entries["front_width"].insert(0, self.entries["tilt_corner_0_x"].get())
                self.entries["front_length"].delete(0, tk.END)
                self.entries["front_length"].insert(0, str(-float(self.entries["tilt_corner_0_z"].get())))
                self.entries["back_width"].delete(0, tk.END)
                self.entries["back_width"].insert(0, self.entries["tilt_corner_2_x"].get())
                self.entries["back_length"].delete(0, tk.END)
                self.entries["back_length"].insert(0, self.entries["tilt_corner_2_z"].get())
                # Wall
                self.entries["wall_front_width"].delete(0, tk.END)
                self.entries["wall_front_width"].insert(0, self.entries["wall_corner_0_x"].get())
                self.entries["wall_front_length"].delete(0, tk.END)
                self.entries["wall_front_length"].insert(0, str(-float(self.entries["wall_corner_0_z"].get())))
                self.entries["wall_back_width"].delete(0, tk.END)
                self.entries["wall_back_width"].insert(0, self.entries["wall_corner_2_x"].get())
                self.entries["wall_back_length"].delete(0, tk.END)
                self.entries["wall_back_length"].insert(0, self.entries["wall_corner_2_z"].get())

        def update_full_from_summaries(self):
                fw = float(self.entries["front_width"].get())
                fl = float(self.entries["front_length"].get())
                bw = float(self.entries["back_width"].get())
                bl = float(self.entries["back_length"].get())
                wf = float(self.entries["wall_front_width"].get())
                wl = float(self.entries["wall_front_length"].get())
                wb = float(self.entries["wall_back_width"].get())
                wbl = float(self.entries["wall_back_length"].get())
                # Tilt (Y always 0)
                for i, (x, z) in enumerate([(-fw, -fl), (fw, -fl), (-bw, bl), (bw, bl)]):
                        self.entries[f"tilt_corner_{i}_x"].delete(0, tk.END)
                        self.entries[f"tilt_corner_{i}_x"].insert(0, str(x))
                        self.entries[f"tilt_corner_{i}_y"].delete(0, tk.END)
                        self.entries[f"tilt_corner_{i}_y"].insert(0, "0")
                        self.entries[f"tilt_corner_{i}_z"].delete(0, tk.END)
                        self.entries[f"tilt_corner_{i}_z"].insert(0, str(z))
                # Wall
                for i, (x, z) in enumerate([(-wf, -wl), (wf, -wl), (-wb, wbl), (wb, wbl)]):
                        self.entries[f"wall_corner_{i}_x"].delete(0, tk.END)
                        self.entries[f"wall_corner_{i}_x"].insert(0, str(x))
                        self.entries[f"wall_corner_{i}_y"].delete(0, tk.END)
                        self.entries[f"wall_corner_{i}_y"].insert(0, "-0.1")
                        self.entries[f"wall_corner_{i}_z"].delete(0, tk.END)
                        self.entries[f"wall_corner_{i}_z"].insert(0, str(z))
                self._notify_props()

        def auto_set_wall(self):
                self.entries["wall_front_width"].delete(0, tk.END)
                self.entries["wall_front_width"].insert(0, str(abs(float(self.entries["front_width"].get())) + 0.2))
                self.entries["wall_front_length"].delete(0, tk.END)
                self.entries["wall_front_length"].insert(0, str(float(self.entries["front_length"].get()) + 0.2))
                self.entries["wall_back_width"].delete(0, tk.END)
                self.entries["wall_back_width"].insert(0, str(abs(float(self.entries["back_width"].get())) + 0.2))
                self.entries["wall_back_length"].delete(0, tk.END)
                self.entries["wall_back_length"].insert(0, str(float(self.entries["back_length"].get()) + 0.2))
                self.update_full_from_summaries()


class CarSimFrame:
        def __init__(self, master):
                self.master = master
                self.car_props = None
                self.figure, self.ax = plt.subplots()
                self.canvas = FigureCanvasTkAgg(self.figure, master=master)
                self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

                tk.Label(master, text="Machine Setting (0-100):").pack()
                self.balance_slider = tk.Scale(master, from_=0, to=100, orient='horizontal', command=self.update_graph)
                self.balance_slider.pack()

                tk.Label(master, text="Input Accel (0.0 - 1.0):").pack()
                self.input_accel_slider = tk.Scale(master, from_=0.0, to=1.0, resolution=0.01, orient='horizontal', command=self.update_graph)
                self.input_accel_slider.set(1.0)
                self.input_accel_slider.pack()

                tk.Label(master, text="Starting Speed (game units):").pack()
                self.starting_speed_entry = tk.Entry(master)
                self.starting_speed_entry.insert(0, "0.0")
                self.starting_speed_entry.pack()
                self.starting_speed_entry.bind("<KeyRelease>", self.update_graph)

                self.result_label = tk.Label(master, text="")
                self.result_label.pack()

        def set_car_props(self, props):
                self.car_props = props
                self.update_graph(None)

        def update_graph(self, _):
                if not self.car_props:
                        return
                try:
                        starting_speed = float(self.starting_speed_entry.get())
                except Exception:
                        self.result_label.config(text="Invalid starting speed")
                        return

                balance = self.balance_slider.get() / 100.0
                input_accel = self.input_accel_slider.get()
                props = self.derive_stats(self.car_props.copy(), balance)
                times, base_speeds = self.simulate(props, starting_speed, input_accel)

                self.ax.clear()
                self.ax.plot(times, base_speeds)
                self.ax.set_ylim(bottom=0)
                self.ax.set_xlabel("Time (s)")
                self.ax.set_ylabel("Base Speed")
                self.ax.set_title(f"Base Speed Curve (g_balance={balance:.2f}, input_accel={input_accel:.2f})")
                self.ax.grid(True)
                self.canvas.draw()

                final_speed = base_speeds[-1]
                threshold = final_speed * 0.99
                reach_time = next((t for t, s in zip(times, base_speeds) if s >= threshold), times[-1])

                thresh = self.find_mt_threshold(props)
                self.result_label.config(
                        text=f"Top Speed: {final_speed:.3f}  "
                             f"Time to Reach: {reach_time:.2f}s  "
                             f"MT Threshold ≈ {thresh:.2f}"
                )

        def derive_stats(self, result, g_balance):
                balance_offset = g_balance - 0.5

                if balance_offset <= 0.0:
                        if result["drift_accel"] >= 1.0:
                                if result["drift_accel"] >= 1.5:
                                        result["drift_accel"] -= (1.2 - (result["drift_accel"] - 1.5)) * (result["drift_accel"] * balance_offset)
                                else:
                                        result["drift_accel"] -= 1.2 * (result["drift_accel"] * balance_offset)
                        else:
                                result["drift_accel"] -= 2.0 * ((2.0 - result["drift_accel"]) * balance_offset)
                        result["drift_accel"] = min(result["drift_accel"], 2.3)
                elif result["drift_accel"] > 1.0:
                        result["drift_accel"] -= 1.8 * (result["drift_accel"] * balance_offset)

                should_modify_boost = not (balance_offset < 0.0 and result["acceleration"] >= 0.5 and result["max_speed"] <= 0.2)

                if balance_offset <= 0.0:
                        normalized_speed = (result["max_speed"] - 0.12) / 0.08
                        normalized_speed = min(normalized_speed, 1.0)
                        max_speed_delta = 0.45 * (0.4 + 0.2 * normalized_speed)
                else:
                        speed_factor = 1.0
                        if result["acceleration"] >= 0.4:
                                if result["acceleration"] >= 0.5 and result["max_speed"] >= 0.15:
                                        speed_factor = -0.25
                        else:
                                speed_factor = 3.2
                        max_speed_delta = 0.16 * speed_factor

                max_speed_delta *= balance_offset * abs(1.0 - result["max_speed"])

                if result["acceleration"] <= 0.6 or balance_offset >= 0.0:
                        result["acceleration"] += 0.6 * -balance_offset * abs(result["acceleration"])
                else:
                        result["acceleration"] += 2.0 * balance_offset * abs(0.7 - result["acceleration"])

                if result["acceleration"] < 0.4:
                        decel_factor = 1.0
                        if result["acceleration"] < 0.31:
                                max_speed_delta *= 1.5
                                decel_factor = 1.5
                        if result["turn_decel"] > 0.03:
                                decel_factor *= 1.5
                        if balance_offset < 0.0:
                                decel_factor *= 2.0
                        result["turn_decel"] -= abs(0.7 * decel_factor * (result["turn_decel"] * balance_offset))
                        result["turn_decel"] = max(result["turn_decel"], 0.01)

                if result["weight_kg"] < 700.0 and result["acceleration"] > 0.7:
                        result["acceleration"] = 0.7

                result["max_speed"] += max_speed_delta

                if should_modify_boost:
                        result["boost_strength"] *= 1.0 + 0.1 * balance_offset

                return result

        def simulate(self, props, starting_speed, input_accel, duration=15.0, dt=1/60.0):
                speed = (starting_speed * props["weight_kg"]) / 216
                base_speed = speed / props["weight_kg"]
                boost_turbo = 0.0
                abs_local_lateral_speed = 0.0

                times = []
                base_speeds = []
                t = 0.0
                steps = int(duration / dt)

                dt_scale = 60 * dt

                for _ in range(steps):
                        accel_stat_scaled = 40.0 * props["acceleration"]
                        target_speed_component = (input_accel * accel_stat_scaled) / 348.0 + base_speed
                        normalized_fwd_speed = speed / props["weight_kg"]
                        speed_difference = (target_speed_component - normalized_fwd_speed)
                        speed_factor_denom = 36.0 + 40.0 * props["max_speed"] + boost_turbo * 2.0
                        speed_factor = target_speed_component / speed_factor_denom if abs(speed_factor_denom) > 1e-4 else 0.0
                        speed_factor = max(speed_factor, 0.0)
                        current_accel_magnitude = speed_factor * 4.0 * (props["acceleration"] * (0.6 + props["acceleration"]))
                        final_accel_term = speed_difference * current_accel_magnitude + (abs_local_lateral_speed * props["acceleration"] / props["weight_kg"]) * props["turn_decel"]
                        if input_accel < 1.0:
                                final_accel_term *= (0.05 + 0.95 * input_accel)
                        new_base_speed = target_speed_component - final_accel_term
                        new_base_speed = max(new_base_speed - props["drag"], 0.0)
                        base_speed_add = new_base_speed - base_speed
                        base_speed += base_speed_add * dt_scale
                        final_thrust_output = 1000.0 * speed_difference
                        if final_thrust_output < 0.0 or normalized_fwd_speed < 0.0:
                                final_thrust_output *= 0.25
                        speed += final_thrust_output * dt_scale
                        speed_weight_ratio = speed / props["weight_kg"]
                        scaled_speed = 216.0 * speed_weight_ratio
                        if scaled_speed < 2.0:
                                speed = 0.0
                        else:
                                base_drag_mag = speed_weight_ratio * speed_weight_ratio * 8.0
                                speed = max(speed - base_drag_mag * dt_scale, 0.0)
                        times.append(t)
                        base_speeds.append((speed / props["weight_kg"]) * 216)
                        t += dt

                return times, base_speeds

        # ---- helpers to measure post-warm-up slope and locate threshold --------
        def _slope_after(self, props, start_speed, input_accel,
                        warmup_steps=60, dt=1/60.0):
                total_steps = warmup_steps + 2                          # need two more frames
                _, bs = self.simulate(props, start_speed, input_accel,
                        duration=total_steps * dt, dt=dt)
                k = warmup_steps                                               # first frame after warm-up
                return (bs[k+1] - bs[k-1]) / (2 * dt)           # central diff

        def find_slope_threshold(self, props,
                        v_lo=0.0, v_hi=3000.0, eps=0.5,
                        warmup_steps=30, dt=1/60.0):
                while v_hi - v_lo > eps:
                        v_mid = 0.5 * (v_lo + v_hi)
                        d1 = self._slope_after(props, v_mid, 1.0, warmup_steps, dt)
                        d0 = self._slope_after(props, v_mid, 0.0, warmup_steps, dt)
                        if d1 > d0:                     # throttle still helps
                                v_lo = v_mid
                        else:                           # throttle now hurts
                                v_hi = v_mid
                return v_hi                     # ≈ threshold speed

        def _segment(self, props, start_speed, input_accel, frames, dt):
                if frames == 0:
                        return start_speed, 0.0
                _, speeds = self.simulate(
                        props, start_speed, input_accel,
                        duration=frames * dt, dt=dt
                )
                distance = sum(s * dt for s in speeds)          # integrate speed
                return speeds[-1], distance                             # final speed, distance

        def find_mt_threshold(self, props,
                        starting_speed=3000.0, duration=15.0, dt=1/60.0):
                steps = int(duration / dt)

                def total_distance(switch_frame):
                        switch = int(switch_frame)
                        speed_after, dist1 = self._segment(props, starting_speed, 0.0, switch, dt)
                        _, dist2 = self._segment(props, speed_after, 1.0, steps - switch, dt)
                        return dist1 + dist2, speed_after

                lo = 0
                hi = steps
                best_speed = 0.0
                best_dist = -1.0

                while hi - lo > 3:
                        m1 = lo + (hi - lo) // 3
                        m2 = hi - (hi - lo) // 3

                        d1, _ = total_distance(m1)
                        d2, _ = total_distance(m2)

                        if d1 < d2:
                                lo = m1
                        else:
                                hi = m2

                for switch in range(lo, hi + 1):
                        d, speed = total_distance(switch)
                        if d > best_dist:
                                best_dist = d
                                best_speed = speed

                return best_speed


class CarCreatorApp:
        def __init__(self, master):
                self.master = master
                master.title("MXT Car Creator")

                paned = tk.PanedWindow(master, orient=tk.HORIZONTAL)
                paned.pack(fill=tk.BOTH, expand=True)

                editor_frame = tk.Frame(paned)
                graph_frame = tk.Frame(paned)
                paned.add(editor_frame)
                paned.add(graph_frame)

                self.sim_frame = CarSimFrame(graph_frame)
                self.editor = CarPropsEditor(editor_frame, on_props_changed=self.refresh_graph)

        def refresh_graph(self):
                props = self.editor.get_sim_props()
                self.sim_frame.set_car_props(props)


if __name__ == "__main__":
        root = tk.Tk()
        app = CarCreatorApp(root)
        root.mainloop()