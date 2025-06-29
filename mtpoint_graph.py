import tkinter as tk
from tkinter import filedialog, messagebox
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import struct
import os


class CarSimApp:
	def __init__(self, master):
		self.master = master
		master.title("Machine Speed Grapher")

		self.car_props = None
		self.figure, self.ax = plt.subplots()
		self.canvas = FigureCanvasTkAgg(self.figure, master=master)
		self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

		tk.Button(master, text="Load MXT Car Props", command=self.load_file).pack(pady=5)

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

		self.status_label = tk.Label(master, text="No file loaded")
		self.status_label.pack()

		self.result_label = tk.Label(master, text="")
		self.result_label.pack()

	def load_file(self):
		path = filedialog.askopenfilename(filetypes=[("MXT Car Props", "*.mxt_car_props")])
		if not path:
			return
		with open(path, "rb") as f:
			data = f.read()
			values = struct.unpack('<46f', data[:184])
			self.car_props = dict(zip([
				"weight_kg", "acceleration", "max_speed", "grip_1", "grip_2", "grip_3",
				"turn_tension", "drift_accel", "turn_movement", "strafe_turn", "strafe",
				"turn_reaction", "boost_strength", "boost_length", "turn_decel", "drag"
			], values[:16]))
			self.status_label.config(text=os.path.basename(path))
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

		thresh = self.find_slope_threshold(props)
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

		for _ in range(steps):
			accel_stat_scaled = 40.0 * props["acceleration"]
			target_speed_component = (input_accel * accel_stat_scaled) / 348.0 + base_speed
			normalized_fwd_speed = speed / props["weight_kg"]
			speed_difference = target_speed_component - normalized_fwd_speed
			speed_factor_denom = 36.0 + 40.0 * props["max_speed"] + boost_turbo * 2.0
			speed_factor = target_speed_component / speed_factor_denom if abs(speed_factor_denom) > 1e-4 else 0.0
			speed_factor = max(speed_factor, 0.0)
			current_accel_magnitude = speed_factor * 4.0 * (props["acceleration"] * (0.6 + props["acceleration"]))
			final_accel_term = speed_difference * current_accel_magnitude + (abs_local_lateral_speed * props["acceleration"] / props["weight_kg"]) * props["turn_decel"]
			if input_accel < 1.0:
				final_accel_term *= (0.05 + 0.95 * input_accel)
			base_speed = target_speed_component - final_accel_term
			base_speed = max(base_speed - props["drag"], 0.0)
			final_thrust_output = 1000.0 * speed_difference
			speed += final_thrust_output
			speed_weight_ratio = speed / props["weight_kg"]
			scaled_speed = 216.0 * speed_weight_ratio
			if scaled_speed < 2.0:
				speed = 0.0
			else:
				base_drag_mag = speed_weight_ratio * speed_weight_ratio * 8.0
				speed = max(speed - base_drag_mag, 0.0)
			times.append(t)
			base_speeds.append((speed / props["weight_kg"]) * 216)
			t += dt

		return times, base_speeds

	# ---- helpers to measure post-warm-up slope and locate threshold -------
	def _slope_after(self, props, start_speed, input_accel,
			warmup_steps=60, dt=1/60.0):
		total_steps = warmup_steps + 2				# need two more frames
		_, bs = self.simulate(props, start_speed, input_accel,
			duration=total_steps * dt, dt=dt)
		k = warmup_steps							# first frame after warm-up
		return (bs[k+1] - bs[k-1]) / (2 * dt)		# central diff

	def find_slope_threshold(self, props,
			v_lo=0.0, v_hi=3000.0, eps=0.5,
			warmup_steps=30, dt=1/60.0):
		while v_hi - v_lo > eps:
			v_mid = 0.5 * (v_lo + v_hi)
			d1 = self._slope_after(props, v_mid, 1.0, warmup_steps, dt)
			d0 = self._slope_after(props, v_mid, 0.0, warmup_steps, dt)
			if d1 > d0:			# throttle still helps
				v_lo = v_mid
			else:				# throttle now hurts
				v_hi = v_mid
		return v_hi			# ≈ threshold speed

if __name__ == "__main__":
	root = tk.Tk()
	app = CarSimApp(root)
	root.mainloop()
