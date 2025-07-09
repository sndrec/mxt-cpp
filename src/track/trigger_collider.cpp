#include "track/trigger_collider.h"
#include "car/physics_car.h"

void TriggerCollider::start_touch(PhysicsCar* car) {}
void TriggerCollider::touch(PhysicsCar* car) {}
void TriggerCollider::end_touch(PhysicsCar* car) {}

Dashplate::Dashplate() { type = TRIGGER_TYPE::DASHPLATE; }
void Dashplate::start_touch(PhysicsCar* car) {}
void Dashplate::touch(PhysicsCar* car) {}
void Dashplate::end_touch(PhysicsCar* car) {}

Jumpplate::Jumpplate() { type = TRIGGER_TYPE::JUMPPLATE; }
void Jumpplate::start_touch(PhysicsCar* car) {}
void Jumpplate::touch(PhysicsCar* car) {}
void Jumpplate::end_touch(PhysicsCar* car) {}

Mine::Mine() { type = TRIGGER_TYPE::MINE; }
void Mine::start_touch(PhysicsCar* car) {}
void Mine::touch(PhysicsCar* car) {}
void Mine::end_touch(PhysicsCar* car) {}

