"""Minimal Blender module surface for importing pure add-on helpers in tests."""

from __future__ import annotations

import sys
import types
from unittest.mock import MagicMock


class _BlenderType:
    pass


class _Operator(_BlenderType):
    pass


class _PropertyGroup(_BlenderType):
    pass


class _Panel(_BlenderType):
    pass


class _UIList(_BlenderType):
    pass


class _GizmoGroup(_BlenderType):
    pass


def install() -> None:
    bpy = types.ModuleType("bpy")
    bpy_types = types.ModuleType("bpy.types")
    for name, value in {
        "Operator": _Operator,
        "PropertyGroup": _PropertyGroup,
        "Panel": _Panel,
        "UIList": _UIList,
        "GizmoGroup": _GizmoGroup,
    }.items():
        setattr(bpy_types, name, value)
    bpy_types.__getattr__ = lambda _name: _BlenderType
    bpy.types = bpy_types

    bpy_props = types.ModuleType("bpy.props")
    for name in (
        "FloatProperty",
        "FloatVectorProperty",
        "EnumProperty",
        "PointerProperty",
        "StringProperty",
        "BoolProperty",
        "IntProperty",
        "CollectionProperty",
    ):
        setattr(bpy_props, name, lambda *args, **kwargs: None)
    bpy.props = bpy_props

    bpy_app = types.ModuleType("bpy.app")
    bpy_handlers = types.ModuleType("bpy.app.handlers")
    bpy_handlers.persistent = lambda function: function
    bpy_app.handlers = bpy_handlers
    bpy.app = bpy_app
    bpy.context = MagicMock()
    bpy.data = MagicMock()
    bpy.ops = MagicMock()
    bpy.utils = MagicMock()

    sys.modules.update(
        {
            "bpy": bpy,
            "bpy.types": bpy_types,
            "bpy.props": bpy_props,
            "bpy.app": bpy_app,
            "bpy.app.handlers": bpy_handlers,
            "bmesh": MagicMock(),
            "gpu": MagicMock(),
        }
    )

    gpu_extras = types.ModuleType("gpu_extras")
    gpu_batch = types.ModuleType("gpu_extras.batch")
    gpu_batch.batch_for_shader = MagicMock()
    sys.modules["gpu_extras"] = gpu_extras
    sys.modules["gpu_extras.batch"] = gpu_batch

    mathutils = types.ModuleType("mathutils")
    for name in ("Vector", "Quaternion", "Matrix"):
        setattr(mathutils, name, MagicMock())
    sys.modules["mathutils"] = mathutils
