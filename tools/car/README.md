# Car authoring tools

This directory is the home for standalone vehicle authoring and inspection
tools. Repository build and packaging entry points remain under `scripts/`.

`update_vehicle_files.py` repairs official vehicle scene/definition references
from the assets currently present under `mxto/vehicle/asset`. Run it from any
working directory:

```powershell
python tools/car/update_vehicle_files.py
```

The car-properties codec, editor, graph, and focused codec tests must move here
together so the binary format retains one authoritative implementation.
