import struct
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

class CarPropsEditor:
	def __init__(self, master):
		self.master = master
		master.title("MXT Car Props Editor")

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
			self.entries[field] = entry

		# Tilt summary
		for i, field in enumerate(self.tilt_summary_fields):
			label = tk.Label(tilt_tab, text=field)
			label.grid(row=i, column=0, sticky='e')
			entry = tk.Entry(tilt_tab)
			entry.grid(row=i, column=1)
			self.entries[field] = entry

		# Wall summary
		for i, field in enumerate(self.wall_summary_fields):
			label = tk.Label(wall_tab, text=field)
			label.grid(row=i, column=0, sticky='e')
			entry = tk.Entry(wall_tab)
			entry.grid(row=i, column=1)
			self.entries[field] = entry

		# Full vector values (hidden by default, but editable if wanted)
		full_group = ttk.LabelFrame(meta_tab, text="Full Corner Vectors")
		full_group.pack(fill='both', expand=True, padx=10, pady=10)
		for i, field in enumerate(self.full_fields):
			label = tk.Label(full_group, text=field)
			label.grid(row=i, column=0, sticky='e')
			entry = tk.Entry(full_group)
			entry.grid(row=i, column=1)
			self.entries[field] = entry

		# Final u32 field
		u32_label = tk.Label(meta_tab, text=self.u32_field)
		u32_label.pack()
		u32_entry = tk.Entry(meta_tab)
		u32_entry.pack()
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

if __name__ == "__main__":
	root = tk.Tk()
	app = CarPropsEditor(root)
	root.mainloop()
