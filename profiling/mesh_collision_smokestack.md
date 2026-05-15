# Smokestack Mesh Collision Profiling

Track source: `track_source_files/smokestack.blend`

Run target: headless singleplayer, `--cpu-drivers 100`, 3600-frame minimum window.

| Date | Build | Track | Collision mode | Frames | Result | Phase avg us | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-05-15 | `96433eb` + `v0.8` analytic flag | `mxto/track/smokestack_profile_analytic` | analytic-only `v0.8`, no mesh payload | 3600 | completed harness run | `total=209 prepare_floor=39 collision=9 post=32 lane_group=137 lanes=4` | Header: `v0.8`, 582 checkpoints, 35 segments, 23 triggers, 0 mesh triangles. Raw line, sampled over the final 360-frame profiler window: `MXT_PHASE_AVG_US frames=360 total=209 input=40 begin=0 prepare_floor=39 project=0 steer_susp=4 linear=4 integrate=3 collision=9 post=32 post_response=31 post_sample_old=4 post_corners=25 post_apply_response=0 post_project_speed=0 post_visual_geom=1 post_damage_tail=0 misc=14 lane_group=137 lanes=4`. |
| 2026-05-15 | `b89082d` + full-lap harness | `mxto/track/smokestack_mesh_only` | mesh-only preview collision, analytic collision disabled | 3600 | completed harness run | `total=772 prepare_floor=605 collision=9 post=32 lane_group=704 lanes=4` | Header: `v0.8`, 582 checkpoints, 35 segments, 23 triggers, 122726 mesh triangles. Raw line, sampled over the final 360-frame profiler window: `MXT_PHASE_AVG_US frames=360 total=772 input=41 begin=0 prepare_floor=605 project=0 steer_susp=4 linear=4 integrate=3 collision=9 post=32 post_response=31 post_sample_old=5 post_corners=24 post_apply_response=0 post_project_speed=0 post_visual_geom=1 post_damage_tail=0 misc=11 lane_group=704 lanes=4`. Full-lap gate: `--frames 9000 --require-full-lap` completed all 100 racers in 6459 frames; final profile line was `total=3290 prepare_floor=3073 collision=9 post=38 lane_group=3212 lanes=4`. |

Record the raw `MXT_PHASE_AVG_US`, `MXT_RENDER_AVG_US`, and `MXT_OUTER_AVG_US` lines under each row's notes when a run completes. Keep analytic and mesh-only rows adjacent so collision cost movement stays obvious.
