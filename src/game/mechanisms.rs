use macroquad::prelude::*;
use rapier2d::prelude::{vector, RigidBodyHandle, RigidBodySet};

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
        self.spawn_pair(physics, MechanismKind::Rod, position, 170.0, 2200.0);
    }

    pub fn spawn_rope(&mut self, physics: &PhysicsWorld, position: Vec2) {
        self.spawn_pair(physics, MechanismKind::Rope, position, 210.0, 850.0);
    }

    pub fn spawn_spring(&mut self, physics: &PhysicsWorld, position: Vec2) {
        self.spawn_pair(physics, MechanismKind::Spring, position, 150.0, 520.0);
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

    pub fn draw_visuals(&self, physics: &PhysicsWorld) {
        for mechanism in &self.items {
            if !mechanism.active {
                continue;
            }

            match mechanism.kind {
                MechanismKind::Rod => {
                    if let Some((first, second)) = self.endpoints(physics, mechanism) {
                        draw_line(
                            first.x,
                            first.y,
                            second.x,
                            second.y,
                            8.0,
                            Color::from_rgba(170, 181, 188, 255),
                        );
                        draw_circle(first.x, first.y, 9.0, Color::from_rgba(74, 84, 94, 255));
                        draw_circle(second.x, second.y, 9.0, Color::from_rgba(74, 84, 94, 255));
                    }
                }
                MechanismKind::Rope => {
                    if let Some((first, second)) = self.endpoints(physics, mechanism) {
                        draw_line(
                            first.x,
                            first.y,
                            second.x,
                            second.y,
                            4.0,
                            Color::from_rgba(190, 143, 82, 255),
                        );
                        draw_circle(first.x, first.y, 6.0, Color::from_rgba(112, 76, 44, 255));
                        draw_circle(second.x, second.y, 6.0, Color::from_rgba(112, 76, 44, 255));
                    }
                }
                MechanismKind::Spring => {
                    if let Some((first, second)) = self.endpoints(physics, mechanism) {
                        self.draw_spring(first, second);
                    }
                }
                MechanismKind::Piston => {
                    self.draw_piston(physics, mechanism);
                }
                MechanismKind::Button => {
                    self.draw_button(physics, mechanism);
                }
                MechanismKind::Toggle => {
                    self.draw_toggle(mechanism);
                }
            }
        }
    }

    fn endpoints(&self, physics: &PhysicsWorld, mechanism: &Mechanism) -> Option<(Vec2, Vec2)> {
        let first = mechanism.first?;
        let second = mechanism.second?;

        let first_position = physics.body_position(first)?;
        let second_position = physics.body_position(second)?;

        Some((
            vec2(
                first_position.translation.x,
                first_position.translation.y,
            ),
            vec2(
                second_position.translation.x,
                second_position.translation.y,
            ),
        ))
    }

    fn draw_spring(&self, first: Vec2, second: Vec2) {
        let offset = second - first;
        let length = offset.length();

        if length < 1.0 {
            return;
        }

        let direction = offset / length;
        let normal = vec2(-direction.y, direction.x);
        let segments = ((length / 18.0).round() as usize).clamp(4, 18);
        let mut previous = first;

        for index in 1..=segments {
            let progress = index as f32 / segments as f32;
            let base = first + offset * progress;
            let wave = if index == segments {
                0.0
            } else if index % 2 == 0 {
                8.0
            } else {
                -8.0
            };
            let current = base + normal * wave;

            draw_line(
                previous.x,
                previous.y,
                current.x,
                current.y,
                3.0,
                Color::from_rgba(226, 192, 79, 255),
            );
            previous = current;
        }

        draw_circle(first.x, first.y, 7.0, Color::from_rgba(115, 91, 38, 255));
        draw_circle(second.x, second.y, 7.0, Color::from_rgba(115, 91, 38, 255));
    }

    fn draw_piston(&self, physics: &PhysicsWorld, mechanism: &Mechanism) {
        let base = mechanism.anchor;
        let target = mechanism
            .first
            .and_then(|handle| physics.body_position(handle))
            .map(|position| {
                vec2(
                    position.translation.x,
                    position.translation.y,
                )
            })
            .unwrap_or(base);

        draw_line(
            base.x,
            base.y,
            target.x,
            target.y,
            12.0,
            Color::from_rgba(68, 77, 88, 255),
        );
        draw_circle(base.x, base.y, 22.0, Color::from_rgba(48, 57, 68, 255));
        draw_circle_lines(base.x, base.y, 22.0, 3.0, Color::from_rgba(180, 192, 198, 255));
        draw_circle(target.x, target.y, 13.0, Color::from_rgba(204, 125, 56, 255));
    }

    fn draw_button(&self, physics: &PhysicsWorld, mechanism: &Mechanism) {
        let base = mechanism.anchor;
        let target = mechanism
            .first
            .and_then(|handle| physics.body_position(handle))
            .map(|position| {
                vec2(
                    position.translation.x,
                    position.translation.y,
                )
            })
            .unwrap_or(base);

        let pressed_offset = if mechanism.pressed { 7.0 } else { 0.0 };
        let head = vec2(base.x, base.y - 20.0 + pressed_offset);

        draw_rectangle(
            base.x - 18.0,
            base.y - 4.0,
            36.0,
            30.0,
            Color::from_rgba(63, 72, 82, 255),
        );
        draw_line(
            base.x,
            base.y,
            target.x,
            target.y,
            3.0,
            Color::from_rgba(142, 151, 158, 190),
        );
        draw_circle(
            head.x,
            head.y,
            16.0,
            if mechanism.pressed {
                Color::from_rgba(220, 75, 64, 255)
            } else {
                Color::from_rgba(78, 190, 96, 255)
            },
        );
        draw_circle_lines(head.x, head.y, 16.0, 3.0, WHITE);
    }

    fn draw_toggle(&self, mechanism: &Mechanism) {
        let center = mechanism.anchor;
        let body = Color::from_rgba(57, 67, 78, 255);
        let accent = if mechanism.pressed {
            Color::from_rgba(78, 201, 104, 255)
        } else {
            Color::from_rgba(184, 76, 63, 255)
        };

        draw_rectangle(center.x - 28.0, center.y - 20.0, 56.0, 40.0, body);
        draw_rectangle_lines(center.x - 28.0, center.y - 20.0, 56.0, 40.0, 3.0, WHITE);

        let lever_y = if mechanism.pressed {
            center.y - 9.0
        } else {
            center.y + 9.0
        };

        draw_line(center.x, center.y, center.x, lever_y, 6.0, accent);
        draw_circle(center.x, lever_y, 9.0, accent);
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
        let error = distance - target_length;
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

        let Some(second) =
            self.closest_dynamic_body_excluding(physics, position, first, 320.0)
        else {
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
}