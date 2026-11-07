mod input;
mod physics;
mod render;
mod state;

use macroquad::prelude::*;

use input::{Action, InputState};
use physics::{CharacterKind, WeaponKind};
use render::Renderer;
use state::{GameMode, GameState, SpawnCategory, SpawnItem};

pub struct Game {
    state: GameState,
    input: InputState,
    renderer: Renderer,
    drag_body: Option<rapier2d::prelude::RigidBodyHandle>,
}

impl Game {
    pub fn new() -> Self {
        Self {
            state: GameState::new(),
            input: InputState::new(),
            renderer: Renderer::new(),
            drag_body: None,
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
                } else if self.input.select_map_pressed() {
                    self.state.map = self.state.map.next();
                    self.state.rebuild_world();
                    self.drag_body = None;
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

    fn update_dragging(&mut self) {
        let Some(pointer) = self.input.drag_pointer() else {
            self.drag_body = None;
            return;
        };

        let world_position = self.screen_to_world(pointer);

        if self.drag_body.is_none() {
            let handles = [
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

            self.drag_body = handles
                .into_iter()
                .filter_map(|handle| {
                    let position = self.state.physics.body_position(handle)?;
                    let distance = vec2(
                        position.translation.x,
                        position.translation.y,
                    )
                    .distance(world_position);

                    if distance <= 72.0 {
                        Some((handle, distance))
                    } else {
                        None
                    }
                })
                .min_by(|left, right| left.1.total_cmp(&right.1))
                .map(|(handle, _)| handle);
        }

        if let Some(handle) = self.drag_body {
            if let Some(body) = self.state.physics.bodies.get_mut(handle) {
                body.set_translation(
                    rapier2d::prelude::vector![world_position.x, world_position.y],
                    true,
                );
                body.set_linvel(rapier2d::prelude::vector![0.0, 0.0], true);
                body.set_angvel(0.0, true);
            }
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