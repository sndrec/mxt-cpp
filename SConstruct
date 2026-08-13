#!/usr/bin/env python
import os
import sys

from SCons.Errors import UserError

env = SConscript("godot-cpp/SConstruct")
# For reference:
# - CCFLAGS are compilation flags shared between C and C++
# - CFLAGS are for C-specific compilation flags
# - CXXFLAGS are for C++-specific compilation flags
# - CPPFLAGS are for pre-processor flags
# - CPPDEFINES are for pre-processor defines
# - LINKFLAGS are for linking flags

# tweak this if you want to use different folders, or more folders, to store your source code in.
env.Append(CPPPATH=["src/"])
env.Append(CPPPATH=["src/mathfu/include"])
env.Append(CPPPATH=["thirdparty/zstd/lib"])
env.Append(CPPPATH=["thirdparty/opus/include"])
steamworks_runtime_targets = []
steamworks_enabled = False
steamworks_public_dir = ""
steamworks_sdk_root = ARGUMENTS.get("steamworks_sdk", os.environ.get("STEAMWORKS_SDK_ROOT", ""))
steamworks_sdk_explicit = bool(steamworks_sdk_root)
if not steamworks_sdk_root:
    sibling_sdk = os.path.abspath(os.path.join(os.getcwd(), "..", "steamworks-sdk"))
    if os.path.isfile(os.path.join(sibling_sdk, "public", "steam", "steam_api.h")):
        steamworks_sdk_root = sibling_sdk

if steamworks_sdk_root:
    steamworks_sdk_root = os.path.abspath(steamworks_sdk_root)
    steamworks_header = os.path.join(steamworks_sdk_root, "public", "steam", "steam_api.h")
    if not os.path.isfile(steamworks_header):
        if steamworks_sdk_explicit:
            raise UserError("Steamworks SDK does not contain public/steam/steam_api.h: {}".format(steamworks_sdk_root))
        steamworks_sdk_root = ""

if steamworks_sdk_root and env["platform"] == "windows":
    steamworks_lib_dir = os.path.join(steamworks_sdk_root, "redistributable_bin", "win64")
    steamworks_lib = os.path.join(steamworks_lib_dir, "steam_api64.lib")
    steamworks_dll = os.path.join(steamworks_lib_dir, "steam_api64.dll")
    if not os.path.isfile(steamworks_lib) or not os.path.isfile(steamworks_dll):
        raise UserError("Steamworks Windows x64 redistributables are missing under {}".format(steamworks_lib_dir))
    steamworks_enabled = True
    steamworks_public_dir = os.path.join(steamworks_sdk_root, "public")
    env.Append(LIBPATH=[steamworks_lib_dir])
    env.Append(LIBS=["steam_api64"])
    steamworks_runtime_targets.append(
        env.Command("mxto/bin/steam_api64.dll", steamworks_dll, Copy("$TARGET", "$SOURCE"))
    )
    print("Steamworks enabled from {}".format(steamworks_sdk_root))
else:
    print("Steamworks SDK not linked for this build; MxtSteamService will report unavailable")
if env["platform"] == "windows":
    env.Append(CXXFLAGS=[
        '/fp:precise',  # Ensure safe floating-point math optimizations
    ])

def opus_sources_from_mk(path, var_name):
    out = []
    collecting = False
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not collecting:
                if line.startswith(var_name + " ="):
                    collecting = True
                    line = line.split("=", 1)[1].strip()
                else:
                    continue
            if line.endswith("\\"):
                line = line[:-1].strip()
                keep_collecting = True
            else:
                keep_collecting = False
            if line:
                out.append("thirdparty/opus/" + line)
            collecting = keep_collecting
            if not collecting:
                break
    return out

sources = []
sources.append([
    "src/register_types.cpp",
])
sources.append(Glob("src/camera/*.cpp"))
sources.append(Glob("src/gamesim/*.cpp"))
sources.append(Glob("src/core/*.cpp"))
sources.append(Glob("src/netcode/*.cpp"))
sources.append(Glob("src/audio/*.cpp"))
sources.append(Glob("src/render/*.cpp"))
sources.append(Glob("src/content/*.cpp"))
sources.append(Glob("src/track/*.cpp"))
sources.append(Glob("src/car/*.cpp"))
steam_service_env = env.Clone()
if steamworks_enabled:
    steam_service_env.Append(CPPPATH=[steamworks_public_dir])
    steam_service_env.Append(CPPDEFINES=["MXT_STEAMWORKS_ENABLED"])
    if env["platform"] == "windows":
        steam_service_env.Append(CXXFLAGS=["/wd4828"])
sources += steam_service_env.SharedObject("src/platform/steam/steam_service.cpp")
sources.append(Glob("thirdparty/zstd/lib/common/*.c"))
sources.append(Glob("thirdparty/zstd/lib/compress/*.c"))
sources.append(Glob("thirdparty/zstd/lib/decompress/*.c"))
opus_sources = []
opus_sources += opus_sources_from_mk("thirdparty/opus/celt_sources.mk", "CELT_SOURCES")
opus_sources += opus_sources_from_mk("thirdparty/opus/silk_sources.mk", "SILK_SOURCES")
opus_sources += opus_sources_from_mk("thirdparty/opus/silk_sources.mk", "SILK_SOURCES_FLOAT")
opus_sources += opus_sources_from_mk("thirdparty/opus/opus_sources.mk", "OPUS_SOURCES")
opus_sources += opus_sources_from_mk("thirdparty/opus/opus_sources.mk", "OPUS_SOURCES_FLOAT")

opus_env = env.Clone()
opus_env.Prepend(CPPPATH=["thirdparty/opus/include"])
opus_env.Prepend(CPPPATH=["thirdparty/opus/celt"])
opus_env.Prepend(CPPPATH=["thirdparty/opus/silk"])
opus_env.Prepend(CPPPATH=["thirdparty/opus/silk/float"])
opus_env.Append(CPPDEFINES=["OPUS_BUILD", "USE_ALLOCA", "HAVE_LRINT", "HAVE_LRINTF"])
sources += opus_env.SharedObject(opus_sources)

env['PDB'] = 'symbols.pdb'

if env["target"] in ("editor", "template_debug"):
    env.Append(CPPDEFINES=["DEBUG_ENABLED", "DEBUG_METHODS_ENABLED"])
else:
    if env["platform"] == "windows":
        env.Append(CCFLAGS=["/O2"])
    else:
        env.Append(CCFLAGS=["-O2"])

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "mxto/bin/libgamesim.{}.{}.framework/libgamesim.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "mxto/bin/libgamesim{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )


db = env.CompilationDatabase()
Default([library, db] + steamworks_runtime_targets)
