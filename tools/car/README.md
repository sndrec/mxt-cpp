# Official vehicle reference repair

Vehicle properties are authored by the in-game Car Creator and parsed by the
native GDExtension. There is no separate Python implementation of the vehicle
property format.

`update_vehicle_files.py` is a narrowly scoped maintenance script that repairs
official vehicle scene and definition references from the assets currently
present under `mxto/vehicle/asset`. Run it from any working directory:

```powershell
python tools/car/update_vehicle_files.py
```
