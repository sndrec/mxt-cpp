# Official vehicle reference repair

The in-game Car Creator authors vehicle properties, and the native GDExtension
parses their binary format.

`update_vehicle_files.py` is a narrowly scoped maintenance script that repairs
official vehicle scene and definition references from the assets currently
present under `mxto/vehicle/asset`. Run it from any working directory:

```powershell
python tools/car/update_vehicle_files.py
```
