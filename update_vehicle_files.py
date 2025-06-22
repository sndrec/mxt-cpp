import os
import re

BASE = 'mxto/vehicle/asset'

# directories to skip (no car props)
SKIP = {'common_a', 'common_b', 'tex_common'}

def to_display(name):
    return ' '.join(word.capitalize() for word in name.split('_'))

for folder in sorted(os.listdir(BASE)):
    if folder in SKIP:
        continue
    dpath = os.path.join(BASE, folder)
    if not os.path.isdir(dpath):
        continue
    scene_path = os.path.join(dpath, 'vehicle.tscn')
    def_path = os.path.join(dpath, 'definition.tres')
    props_candidates = [f for f in os.listdir(dpath) if f.endswith('.mxt_car_props')]
    if len(props_candidates) != 1:
        print('warning: car props not found or ambiguous in', folder)
        continue
    props_path = os.path.join(dpath, props_candidates[0])
    if not os.path.isfile(scene_path) or not os.path.isfile(def_path):
        continue

    # find obj file
    obj_files = []
    for root, dirs, files in os.walk(os.path.join(dpath, 'vehicle')):
        for f in files:
            if f.endswith('.obj'):
                obj_files.append(os.path.join(root, f))
    if len(obj_files) != 1:
        print('warning: expected 1 obj in', folder, 'found', len(obj_files))
        if not obj_files:
            continue
    obj_file = obj_files[0]
    import_file = obj_file + '.import'
    obj_uid = None
    with open(import_file, 'r') as f:
        for line in f:
            if line.startswith('uid='):
                obj_uid = line.strip().split('=')[1].strip().strip('"')
                break
    if not obj_uid:
        raise ValueError('uid not found for', obj_file)
    obj_rel = os.path.relpath(obj_file, 'mxto')
    obj_res_path = f'res://{obj_rel.replace(os.sep, "/")}'

    # update vehicle.tscn
    with open(scene_path, 'r') as f:
        lines = f.readlines()
    for i,line in enumerate(lines):
        if line.startswith('[ext_resource') and 'ArrayMesh' in line:
            # preserve id
            m = re.search(r' id="([^"]+)"', line)
            ext_id = m.group(1) if m else '1'
            lines[i] = f'[ext_resource type="ArrayMesh" uid="{obj_uid}" path="{obj_res_path}" id="{ext_id}"]\n'
            break
    with open(scene_path, 'w') as f:
        f.writelines(lines)

    # parse vehicle scene uid
    scene_uid = None
    m = re.search(r'uid="([^"]+)"', lines[0])
    if m:
        scene_uid = m.group(1)
    else:
        raise ValueError('scene uid not found for', scene_path)

    # update definition.tres
    with open(def_path, 'r') as f:
        lines = f.readlines()
    for i,line in enumerate(lines):
        if line.startswith('[ext_resource') and 'PackedScene' in line:
            m = re.search(r' id="([^"]+)"', line)
            ext_id = m.group(1) if m else '1'
            lines[i] = f'[ext_resource type="PackedScene" uid="{scene_uid}" path="res://vehicle/asset/{folder}/vehicle.tscn" id="{ext_id}"]\n'
        elif line.startswith('name = '):
            lines[i] = f'name = "{to_display(folder)}"\n'
        elif line.startswith('car_definition = '):
            prop_name = os.path.basename(props_path)
            lines[i] = f'car_definition = "res://vehicle/asset/{folder}/{prop_name}"\n'
    with open(def_path, 'w') as f:
        f.writelines(lines)
