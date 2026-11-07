use macroquad::prelude::Vec2;
use rapier2d::prelude::RigidBodyHandle;

use super::mechanisms::{MechanismKind, MechanismWorld};
use super::physics::PhysicsWorld;

pub trait ToggleInteraction {
    fn toggle_at(&mut self, physics: &PhysicsWorld, position: Vec2) -> bool;
}

impl ToggleInteraction for MechanismWorld {
    fn toggle_at(&mut self, physics: &PhysicsWorld, position: Vec2) -> bool {
        let mut toggled = false;

        for mechanism in &mut self.items {
            if mechanism.kind != MechanismKind::Toggle || !mechanism.active {
                continue;
            }

            if mechanism.anchor.distance(position) <= 44.0 {
                mechanism.pressed = !mechanism.pressed;
                toggled = true;
            }
        }

        if toggled {
            return true;
        }

        let nearest_toggle = self
            .items
            .iter()
            .enumerate()
            .filter(|(_, mechanism)| {
                mechanism.kind == MechanismKind::Toggle && mechanism.active
            })
            .filter_map(|(index, mechanism)| {
                let distance = mechanism.anchor.distance(position);

                if distance <= 72.0 {
                    Some((index, distance))
                } else {
                    None
                }
            })
            .min_by(|left, right| left.1.total_cmp(&right.1));

        if let Some((index, _)) = nearest_toggle {
            if let Some(mechanism) = self.items.get_mut(index) {
                mechanism.pressed = !mechanism.pressed;
                return true;
            }
        }

        let _ = physics;
        false
    }

    fn toggle_at_unused_body(
        &mut self,
        _physics: &PhysicsWorld,
        _handle: RigidBodyHandle,
    ) -> bool {
        false
    }
}