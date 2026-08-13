bl_info = {
    "name": "MXT Racetrack Road Creator",
    "author": "Twilight",
    "version": (0, 1, 1),
    "blender": (4, 0, 0),
    "location": "3D View > Sidebar (N-Panel) > MXT Road Creator",
    "description": "Design a racetrack for Maxx Throttle!",
    "warning": "",
    "doc_url": "",
    "category": "Object",
}

from .foundation import register, unregister

__all__ = ("register", "unregister")

if __name__ == "__main__":
    register()
