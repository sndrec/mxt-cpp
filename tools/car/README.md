# Car authoring tools

This directory is the home for standalone vehicle authoring and inspection
tools. Repository build and packaging entry points remain under `scripts/`.

`mxt_car_props_format.py` is the single authoritative Python implementation of
the `.mxt_car_props` binary and JSON formats. The two interactive programs use
that module directly:

```powershell
python tools/car/mxt_car_creator.py
python tools/car/mtpoint_graph.py
```

`update_vehicle_files.py` repairs official vehicle scene/definition references
from the assets currently present under `mxto/vehicle/asset`. Run it from any
working directory:

```powershell
python tools/car/update_vehicle_files.py
```

`report_official_car_properties.py` emits a deterministic Markdown audit of
every official property document on a fixed machine-setting grid, including
non-identity special-state values and source hashes:

```powershell
python tools/car/report_official_car_properties.py
python tools/car/report_official_car_properties.py --output car-property-audit.md
```

Run the focused codec regression tests from the repository root:

```powershell
python -m unittest discover tools/car/tests
```
