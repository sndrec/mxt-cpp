# Smokestack Mesh Collision Profiling

Track source: `track_source_files/smokestack.blend`

Run target: headless singleplayer, `--cpu-drivers 100`, 3600-frame minimum window.

| Date | Build | Track | Collision mode | Frames | Result | Phase avg us | Notes |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-05-15 | `5a1ac83` + profiling harness | `mxto/track/smokestack_profile_analytic` | analytic-only `v0.7`, no mesh payload | 3600 | completed harness run | `total=203 prepare_floor=38 collision=8 post=31 lane_group=132 lanes=4` | Header: `v0.7`, 582 checkpoints, 35 segments, 23 triggers, 0 mesh triangles. Raw line, sampled over the final 360-frame profiler window: `MXT_PHASE_AVG_US frames=360 total=203 input=40 begin=0 prepare_floor=38 project=0 steer_susp=4 linear=4 integrate=3 collision=8 post=31 post_response=30 post_sample_old=4 post_corners=24 post_apply_response=0 post_project_speed=0 post_visual_geom=1 post_damage_tail=0 misc=14 lane_group=132 lanes=4`. |

Record the raw `MXT_PHASE_AVG_US`, `MXT_RENDER_AVG_US`, and `MXT_OUTER_AVG_US` lines under each row's notes when a run completes. Keep analytic and mesh-only rows adjacent so collision cost movement stays obvious.
