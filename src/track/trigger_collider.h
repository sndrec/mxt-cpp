#pragma once

#include "godot_cpp/variant/transform3d.hpp"
#include "godot_cpp/variant/vector3.hpp"

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
    RaceTrack* current_track = nullptr;

    uint8_t intersect_segment(int cp_idx, const godot::Vector3 &p0, const godot::Vector3 &p1) const;
    virtual ~TriggerCollider() = default;
    virtual void start_touch(PhysicsCar* car);
    virtual void touch(PhysicsCar* car);
    virtual void end_touch(PhysicsCar* car);
};

class Dashplate : public TriggerCollider {
public:
    Dashplate();
    void start_touch(PhysicsCar* car) override;
    void touch(PhysicsCar* car) override;
    void end_touch(PhysicsCar* car) override;
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
    void start_touch(PhysicsCar* car) override;
    void touch(PhysicsCar* car) override;
    void end_touch(PhysicsCar* car) override;
};

