mod input;
mod mechanisms;
mod physics;
mod render;
mod state;

use macroquad::prelude::*;

use input::{Action, InputState};
use mechanisms::MechanismWorld;
use physics::{CharacterKind, WeaponKind};
use render::Renderer;
use state::{GameMode, GameState, SpawnCategory, SpawnItem};

pub struct Game {
    state: GameState,
    input: InputState,
    renderer: Renderer,
    drag_body: Option<rapier2d::prelude::RigidBodyHandle>,
    mechanisms: MechanismWorld,
}

impl Game {
    pub fn new() -> Self {
        Self {
            state: GameState::new(),
            input: InputState::new(),
            renderer: Renderer::new(),
            drag_body: None,
            mechanisms: MechanismWorld::new(),
        }
    }

    pub fn update(&mut self) {
        self.input
            .update(self.state.spawn_menu_open, self.drag_body.is_some());

        self.update_dragging();

        if self.input.spawn_toggle_pressed() {
            self.state.spawn_menu_open = !self.state.spawn_menu_open;
        }

        if let Some(tab) = self.input.spawn_tab() {
            self.state.spawn_category = match tab {
                0 => SpawnCategory::Pistols,
                1 => SpawnCategory::Weapons,
                2 => SpawnCategory::Melons,
                _ => SpawnCategory::Mechanisms,
            };
        }

        if matches!(self.state.mode, GameMode::Playing | GameMode::Paused) {
            self.state.camera.apply_gesture(
                self.input.camera_pan(),
                self.input.camera_zoom(),
            );
        }

        match self.state.mode {
            GameMode::MainMenu => {
                if self.input.select_character_pressed() {
                    self.state.character = self.state.character.next();
                    self.state.rebuild_world();
                    self.drag_body = None;
                    self.mechanisms.clear();
                } else if self.input.select_map_pressed() {
                    self.state.map = self.state.map.next();
                    self.state.rebuild_world();
                    self.drag_body = None;
                    self.mechanisms.clear();
                } else if self.input.play_pressed() {
                    self.state.mode = GameMode::Playing;
                } else if self.input.settings_pressed() {
                    self.state.mode = GameMode::Settings;
                }
            }
            GameMode::Playing => {
                let delta_time = get_frame_time();
                self.state.elapsed += delta_time;

                self.state.physics.move_ragdoll(self.input.movement());

                if self.input.next_weapon_pressed() {
                    self.state.physics.cycle_weapon();
                }

                self.update_mechanism_hotkeys();

                if let Some(item) = self.input.spawn_item() {
                    self.spawn_menu_item(item);
                }

                match self.input.action() {
                    Action::Fire => self.state.physics.fire_weapon(),
                    Action::SpawnGrenade => self.state.physics.spawn_grenade(),
                    Action::SpawnBarrel => self.state.physics.spawn_barrel(),
                    Action::None => {}
                }

                if self.input.spawn_pressed() {
                    self.state.physics.spawn_box(
                        rapier2d::prelude::Vector::new(780.0, 180.0),
                        rapier2d::prelude::Vector::new(34.0, 34.0),
                    );
                }

                if self.input.damage_pressed() {
                    self.state.physics.apply_test_damage();
                }

                self.state.physics.step(delta_time);
                self.mechanisms.step(&mut self.state.physics, delta_time);

                if self.input.pause_pressed() {
                    self.state.mode = GameMode::Paused;
                }
            }
            GameMode::Settings => {
                if self.input.back_pressed() {
                    self.state.mode = GameMode::MainMenu;
                }
            }
            GameMode::Paused => {
                if self.input.pause_spawn_box_pressed() {
                    self.state.physics.spawn_box(
                        rapier2d::prelude::Vector::new(780.0, 180.0),
                        rapier2d::prelude::Vector::new(34.0, 34.0),
                    );
                } else if self.input.pause_spawn_grenade_pressed() {
                    self.state.physics.spawn_grenade();
                } else if self.input.pause_spawn_barrel_pressed() {
                    self.state.physics.spawn_barrel();
                } else if self.input.pause_resume_pressed() {
                    self.state.mode = GameMode::Playing;
                } else if self.input.back_pressed() {
                    self.state.mode = GameMode::MainMenu;
                }
            }
        }
    }

    fn update_mechanism_hotkeys(&mut self) {
        let position = vec2(780.0, 180.0);

        if is_key_pressed(KeyCode::R) {
            self.mechanisms.spawn_rod(&self.state.physics, position);
        }

        if is_key_pressed(KeyCode::Y) {
            self.mechanisms.spawn_rope(&self.state.physics, position);
        }

        if is_key_pressed(KeyCode::L) {
            self.mechanisms.spawn_spring(&self.state.physics, position);
        }

        if is_key_pressed(KeyCode::T) {
            self.mechanisms.spawn_piston(&self.state.physics, position);
        }

        if is_key_pressed(KeyCode::U) {
            self.mechanisms.spawn_button(&self.state.physics, position);
        }

        if is_key_pressed(KeyCode::I) {
            self.mechanisms.spawn_toggle(&self.state.physics, position);
        }

        if is_key_pressed(KeyCode::O) {
            self.mechanisms.toggle_next();
        }
    }

    fn update_dragging(&mut self) {
        let Some(pointer) = self.input.drag_pointer() else {
            self.drag_body = None;
            return;
        };

        let world_position = self.screen_to_world(pointer);

        if self.drag_body.is_none() {
            self.drag_body = self.find_body_at(world_position);
        }

        if let Some(handle) = self.drag_body {
            if let Some(body) = self.state.physics.bodies.get_mut(handle) {
                if body.is_dynamic() {
                    body.set_translation(
                        rapier2d::prelude::vector![world_position.x, world_position.y],
                        true,
                    );
                    body.set_linvel(rapier2d::prelude::vector![0.0, 0.0], true);
                    body.set_angvel(0.0, true);
                }
            }
        }
    }

    fn find_body_at(
        &self,
        world_position: Vec2,
    ) -> Option<rapier2d::prelude::RigidBodyHandle> {
        let ragdoll_handles = [
            self.state.physics.ragdoll.head,
            self.state.physics.ragdoll.torso,
            self.state.physics.ragdoll.left_upper_arm,
            self.state.physics.ragdoll.right_upper_arm,
            self.state.physics.ragdoll.left_lower_arm,
            self.state.physics.ragdoll.right_lower_arm,
            self.state.physics.ragdoll.left_upper_leg,
            self.state.physics.ragdoll.right_upper_leg,
            self.state.physics.ragdoll.left_lower_leg,
            self.state.physics.ragdoll.right_lower_leg,
        ];

        let mut nearest: Option<(rapier2d::prelude::RigidBodyHandle, f32)> = None;

        for handle in ragdoll_handles {
            if let Some(position) = self.state.physics.body_position(handle) {
                let distance = vec2(
                    position.translation.x,
                    position.translation.y,
                )
                .distance(world_position);

                if distance <= 72.0 {
                    nearest = Self::nearest_body(nearest, handle, distance);
                }
            }
        }

        for prop in &self.state.physics.props {
            if !prop.active {
                continue;
            }

            let Some(body) = self.state.physics.bodies.get(prop.body) else {
                continue;
            };

            if !body.is_dynamic() {
                continue;
            }

            let Some(position) = self.state.physics.body_position(prop.body) else {
                continue;
            };

            let distance = vec2(
                position.translation.x,
                position.translation.y,
            )
            .distance(world_position);

            let hit_radius = prop.half_width.max(prop.half_height) + 28.0;

            if distance <= hit_radius {
                nearest = Self::nearest_body(nearest, prop.body, distance);
            }
        }

        nearest.map(|(handle, _)| handle)
    }

    fn nearest_body(
        current: Option<(rapier2d::prelude::RigidBodyHandle, f32)>,
        handle: rapier2d::prelude::RigidBodyHandle,
        distance: f32,
    ) -> Option<(rapier2d::prelude::RigidBodyHandle, f32)> {
        match current {
            Some((_, current_distance)) if current_distance <= distance => current,
            _ => Some((handle, distance)),
        }
    }

    fn screen_to_world(&self, position: Vec2) -> Vec2 {
        let zoom = self.state.camera.zoom.max(0.1);

        vec2(
            self.state.camera.center.x
                + (position.x - screen_width() * 0.5) / zoom,
            self.state.camera.center.y
                - (position.y - screen_height() * 0.5) / zoom,
        )
    }

    fn spawn_menu_item(&mut self, index: usize) {
        let Some(item) = self.state.spawn_category.item(index) else {
            return;
        };

        let position = rapier2d::prelude::vector![780.0, 180.0];

        match item {
            SpawnItem::Pistol => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::Pistol);
            }
            SpawnItem::SilencedPistol => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::SilencedPistol);
            }
            SpawnItem::HeavyPistol => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::HeavyPistol);
            }
            SpawnItem::Rifle => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::Rifle);
            }
            SpawnItem::Shotgun => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::Shotgun);
            }
            SpawnItem::Bat => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::Bat);
            }
            SpawnItem::Katana => {
                self.state
                    .physics
                    .spawn_weapon(position, WeaponKind::Katana);
            }
            SpawnItem::Melon | SpawnItem::MelonHead => {
                self.state
                    .physics
                    .spawn_character_prop(position, CharacterKind::Melon);
            }
            SpawnItem::Robot => {
                self.state
                    .physics
                    .spawn_character_prop(position, CharacterKind::Robot);
            }
            SpawnItem::Box => {
                self.state.physics.spawn_box(
                    position,
                    rapier2d::prelude::vector![34.0, 34.0],
                );
            }
            SpawnItem::Grenade => self.state.physics.spawn_grenade(),
            SpawnItem::Barrel => self.state.physics.spawn_barrel(),
            SpawnItem::Platform => self.state.physics.spawn_platform(position),
            SpawnItem::Rod => {
                self.mechanisms
                    .spawn_rod(&self.state.physics, Vec2::new(780.0, 180.0));
            }
            SpawnItem::Rope => {
                self.mechanisms
                    .spawn_rope(&self.state.physics, Vec2::new(780.0, 180.0));
            }
            SpawnItem::Spring => {
                self.mechanisms
                    .spawn_spring(&self.state.physics, Vec2::new(780.0, 180.0));
            }
            SpawnItem::Piston => {
                self.mechanisms
                    .spawn_piston(&self.state.physics, Vec2::new(780.0, 180.0));
            }
            SpawnItem::Button => {
                self.mechanisms
                    .spawn_button(&self.state.physics, Vec2::new(780.0, 180.0));
            }
            SpawnItem::Toggle => {
                self.mechanisms
                    .spawn_toggle(&self.state.physics, Vec2::new(780.0, 180.0));
            }
        }
    }

    pub fn render(&mut self) {
        clear_background(Color::from_rgba(18, 22, 30, 255));

        match self.state.mode {
            GameMode::MainMenu => self.renderer.draw_main_menu(&self.state),
            GameMode::Playing => self.renderer.draw_playing(&self.state),
            GameMode::Settings => self.renderer.draw_settings(),
            GameMode::Paused => self.renderer.draw_paused(&self.state),
        }
    }
}