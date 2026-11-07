use macroquad::prelude::Vec2;
use rapier2d::prelude::{
    vector, Real, RigidBodyHandle, RigidBodySet, Vector,
};

use super::physics::PhysicsWorld;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MechanismKind {
    Rod,
    Rope,
    Spring,
    Piston,
    Button,
    Toggle,
}

#[derive(Clone, Copy, Debug)]
pub struct Mechanism {
    pub kind: MechanismKind,
    pub first: Option<RigidBodyHandle>,
    pub second: Option<RigidBodyHandle>,
    pub anchor: Vec2,
    pub rest_length: f32,
    pub strength: f32,
    pub active: bool,
    pub phase: f32,
    pub pressed: bool,
}

pub struct MechanismWorld {
    pub items: Vec<Mechanism>,
    next_toggle: bool,
}

impl MechanismWorld {
    pub fn new() -> Self {
        Self {
            items: Vec::new(),
            next_toggle: false,
        }
    }

    pub fn clear(&mut self) {
        self.items.clear();
        self.next_toggle = false;
    }

    pub fn toggle_next(&mut self) {
        self.next_toggle = true;
    }

    pub fn spawn_rod(&mut self, physics: &PhysicsWorld, position: Vec2) {
        self.spawn_pair(
            physics,
            MechanismKind::Rod,
            position,
            170.0,
            2200.0,
        );
    }

    pub fn spawn_rope(&mut self, physics: &PhysicsWorld, position: Vec2) {
        self.spawn_pair(
            physics,
            MechanismKind::Rope,
            position,
            210.0,
            850.0,
        );
    }

    pub fn spawn_spring(&mut self, physics: &PhysicsWorld, position: Vec2) {
        self.spawn_pair(
            physics,
            MechanismKind::Spring,
            position,
            150.0,
            520.0,
        );
    }

    pub fn spawn_piston(&mut self, physics: &PhysicsWorld, position: Vec2) {
        let first = self.closest_dynamic_body(physics, position, 220.0);

        self.items.push(Mechanism {
            kind: MechanismKind::Piston,
            first,
            second: None,
            anchor: position,
            rest_length: 100.0,
            strength: 900.0,
            active: true,
            phase: 0.0,
            pressed: false,
        });
    }

    pub fn spawn_button(&mut self, physics: &PhysicsWorld, position: Vec2) {
        let first = self.closest_dynamic_body(physics, position, 180.0);

        self.items.push(Mechanism {
            kind: MechanismKind::Button,
            first,
            second: None,
            anchor: position,
            rest_length: 48.0,
            strength: 1.0,
            active: true,
            phase: 0.0,
            pressed: false,
        });
    }

    pub fn spawn_toggle(&mut self, _physics: &PhysicsWorld, position: Vec2) {
        self.items.push(Mechanism {
            kind: MechanismKind::Toggle,
            first: None,
            second: None,
            anchor: position,
            rest_length: 0.0,
            strength: 1.0,
            active: true,
            phase: 0.0,
            pressed: false,
        });
    }

    pub fn step(&mut self, physics: &mut PhysicsWorld, delta_time: f32) {
        let dt = delta_time.clamp(1.0 / 120.0, 1.0 / 30.0);

        if self.next_toggle {
            for mechanism in &mut self.items {
                if mechanism.kind == MechanismKind::Toggle {
                    mechanism.pressed = !mechanism.pressed;
                }
            }

            self.next_toggle = false;
        }

        for mechanism in &mut self.items {
            if !mechanism.active {
                continue;
            }

            mechanism.phase += dt;

            match mechanism.kind {
                MechanismKind::Rod => {
                    Self::apply_distance_constraint(
                        &mut physics.bodies,
                        mechanism.first,
                        mechanism.second,
                        mechanism.rest_length,
                        mechanism.strength,
                        false,
                    );
                }
                MechanismKind::Rope => {
                    Self::apply_distance_constraint(
                        &mut physics.bodies,
                        mechanism.first,
                        mechanism.second,
                        mechanism.rest_length,
                        mechanism.strength,
                        true,
                    );
                }
                MechanismKind::Spring => {
                    Self::apply_spring(
                        &mut physics.bodies,
                        mechanism.first,
                        mechanism.second,
                        mechanism.rest_length,
                        mechanism.strength,
                    );
                }
                MechanismKind::Piston => {
                    Self::apply_piston(
                        &mut physics.bodies,
                        mechanism.first,
                        mechanism.anchor,
                        mechanism.phase,
                        mechanism.strength,
                        mechanism.active,
                    );
                }
                MechanismKind::Button => {
                    mechanism.pressed = Self::is_body_near(
                        physics,
                        mechanism.first,
                        mechanism.anchor,
                        mechanism.rest_length,
                    );
                }
                MechanismKind::Toggle => {}
            }
        }
    }

    fn spawn_pair(
        &mut self,
        physics: &PhysicsWorld,
        kind: MechanismKind,
        position: Vec2,
        rest_length: f32,
        strength: f32,
    ) {
        let Some(first) = self.closest_dynamic_body(physics, position, 260.0) else {
            return;
        };

        let Some(first_position) = physics.body_position(first) else {
            return;
        };

        let first_point = Vec2::new(
            first_position.translation.x,
            first_position.translation.y,
        );

        let Some(second) = self.closest_dynamic_body_excluding(
            physics,
            position,
            first,
            320.0,
        ) else {
            return;
        };

        let second_point = physics
            .body_position(second)
            .map(|position| {
                Vec2::new(
                    position.translation.x,
                    position.translation.y,
                )
            })
            .unwrap_or(first_point);

        let actual_length = first_point.distance(second_point).max(24.0);

        self.items.push(Mechanism {
            kind,
            first: Some(first),
            second: Some(second),
            anchor: position,
            rest_length: actual_length.min(rest_length),
            strength,
            active: true,
            phase: 0.0,
            pressed: false,
        });
    }

    fn closest_dynamic_body(
        &self,
        physics: &PhysicsWorld,
        position: Vec2,
        max_distance: f32,
    ) -> Option<RigidBodyHandle> {
        self.closest_dynamic_body_excluding(
            physics,
            position,
            RigidBodyHandle::invalid(),
            max_distance,
        )
    }

    fn closest_dynamic_body_excluding(
        &self,
        physics: &PhysicsWorld,
        position: Vec2,
        excluded: RigidBodyHandle,
        max_distance: f32,
    ) -> Option<RigidBodyHandle> {
        let mut closest = None;
        let mut closest_distance = max_distance;

        for (handle, body) in physics.bodies.iter() {
            if handle == excluded || !body.is_dynamic() {
                continue;
            }

            let body_position = body.translation();
            let distance = Vec2::new(body_position.x, body_position.y).distance(position);

            if distance < closest_distance {
                closest_distance = distance;
                closest = Some(handle);
            }
        }

        closest
    }

    fn apply_distance_constraint(
        bodies: &mut RigidBodySet,
        first: Option<RigidBodyHandle>,
        second: Option<RigidBodyHandle>,
        target_length: f32,
        strength: f32,
        only_tension: bool,
    ) {
        let (Some(first), Some(second)) = (first, second) else {
            return;
        };

        if first == second {
            return;
        }

        let Some(first_body) = bodies.get(first) else {
            return;
        };
        let Some(second_body) = bodies.get(second) else {
            return;
        };

        let first_position = *first_body.translation();
        let second_position = *second_body.translation();
        let offset = second_position - first_position;
        let distance = offset.norm();

        if distance <= 0.01 || (only_tension && distance <= target_length) {
            return;
        }

        let direction = offset / distance;
        let error = if only_tension {
            distance - target_length
        } else {
            distance - target_length
        };
        let correction = direction * error * strength.min(1.0) * 0.001;

        if let Some(first_body) = bodies.get_mut(first) {
            first_body.apply_impulse(correction, true);
        }

        if let Some(second_body) = bodies.get_mut(second) {
            second_body.apply_impulse(-correction, true);
        }
    }

    fn apply_spring(
        bodies: &mut RigidBodySet,
        first: Option<RigidBodyHandle>,
        second: Option<RigidBodyHandle>,
        rest_length: f32,
        stiffness: f32,
    ) {
        let (Some(first), Some(second)) = (first, second) else {
            return;
        };

        if first == second {
            return;
        }

        let Some(first_body) = bodies.get(first) else {
            return;
        };
        let Some(second_body) = bodies.get(second) else {
            return;
        };

        let offset = *second_body.translation() - *first_body.translation();
        let distance = offset.norm();

        if distance <= 0.01 {
            return;
        }

        let direction = offset / distance;
        let force = direction * (distance - rest_length) * stiffness;

        if let Some(first_body) = bodies.get_mut(first) {
            first_body.add_force(force, true);
        }

        if let Some(second_body) = bodies.get_mut(second) {
            second_body.add_force(-force, true);
        }
    }

    fn apply_piston(
        bodies: &mut RigidBodySet,
        body_handle: Option<RigidBodyHandle>,
        anchor: Vec2,
        phase: f32,
        strength: f32,
        active: bool,
    ) {
        let Some(body_handle) = body_handle else {
            return;
        };

        let Some(body) = bodies.get(body_handle) else {
            return;
        };

        let position = body.translation();
        let displacement = position.y - anchor.y;
        let target = phase.sin() * 90.0;
        let force = (target - displacement) * strength;

        if active {
            if let Some(body) = bodies.get_mut(body_handle) {
                body.add_force(vector![0.0, force], true);
                body.set_linvel(
                    vector![body.linvel().x, body.linvel().y.clamp(-500.0, 500.0)],
                    true,
                );
            }
        }
    }

    fn is_body_near(
        physics: &PhysicsWorld,
        body_handle: Option<RigidBodyHandle>,
        anchor: Vec2,
        radius: f32,
    ) -> bool {
        let Some(body_handle) = body_handle else {
            return false;
        };

        physics
            .body_position(body_handle)
            .map(|position| {
                Vec2::new(position.translation.x, position.translation.y)
                    .distance(anchor)
                    <= radius
            })
            .unwrap_or(false)
    }
}