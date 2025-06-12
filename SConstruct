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
env.Append(CXXFLAGS=[
    '/fp:precise',  # Ensure safe floating-point math optimizations
])
sources = []
sources.append(Glob("src/*.cpp"))
sources.append(Glob("src/mxt_core/*.cpp"))
sources.append(Glob("src/track/*.cpp"))
sources.append(Glob("src/car/*.cpp"))

env['PDB'] = 'symbols.pdb'

#if env["target"] == "debug":
env.Append(CPPDEFINES=["DEBUG_ENABLED", "DEBUG_METHODS_ENABLED"])
#else:
#    env.Append(CCFLAGS=["/O2"])

#if env["platform"] == "macos":
#    library = env.SharedLibrary(
#        "mxto/bin/libgamesim.{}.{}.framework/libgamesim.{}.{}".format(
#            env["platform"], env["target"], env["platform"], env["target"]
#        ),
#        source=sources,
#    )
#else:
library = env.SharedLibrary(
    "mxto/bin/libgamesim{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    source=sources,
)


db = env.CompilationDatabase()
Default([library, db])