#!/usr/bin/env python
import os
import sys

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
sources.append(Glob("src/*.cpp"))
sources.append(Glob("src/mxt_core/*.cpp"))
sources.append(Glob("src/track/*.cpp"))
sources.append(Glob("src/car/*.cpp"))
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
Default([library, db])
