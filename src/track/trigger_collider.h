#pragma once

#include "godot_cpp/variant/transform3d.hpp"
#include "godot_cpp/variant/vector3.hpp"
#include <cstdint>

class PhysicsCar;
class RaceTrack;

namespace TRIGGER_TYPE {
    enum TYPE {
        DASHPLATE = 0,
        JUMPPLATE = 1,
        MINE      = 2
    };
}

class TriggerCollider {
public:
    TRIGGER_TYPE::TYPE type = TRIGGER_TYPE::DASHPLATE;
    godot::Transform3D transform;
    godot::Vector3 half_extents;
    godot::Transform3D inv_transform;
    int segment_index = -1;
    int checkpoint_index = -1;

    uint8_t intersect_segment(int cp_idx, RaceTrack *in_racetrack, const godot::Vector3 &p0, const godot::Vector3 &p1) const;
    virtual ~TriggerCollider() = default;
    virtual void start_touch(PhysicsCar* car);
    virtual void touch(PhysicsCar* car);
    virtual void end_touch(PhysicsCar* car);
};

class Dashplate : public TriggerCollider {
public:
    static constexpr float kHeatTurboMultiplier = 0.2f;
    static constexpr float kHeatMin = 0.0f;
    static constexpr float kHeatMax = 10.0f;

    Dashplate();
    void start_touch(PhysicsCar* car) override;
    void touch(PhysicsCar* car) override;
    void end_touch(PhysicsCar* car) override;

    float heat;
    uint32_t last_activation_tick;
    bool has_last_activation;
};

class Jumpplate : public TriggerCollider {
public:
    Jumpplate();
    void start_touch(PhysicsCar* car) override;
    void touch(PhysicsCar* car) override;
    void end_touch(PhysicsCar* car) override;
};

class Mine : public TriggerCollider {
public:
    Mine();
    bool exploded;
    void start_touch(PhysicsCar* car) override;
    void touch(PhysicsCar* car) override;
    void end_touch(PhysicsCar* car) override;
};

