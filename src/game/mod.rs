mod input;
mod physics;
mod render;
mod state;

use macroquad::prelude::*;

use input::{Action, InputState};
use render::Renderer;
use state::{GameMode, GameState, SpawnCategory};

pub struct Game {
    state: GameState,
    input: InputState,
    renderer: Renderer,
}

impl Game {
    pub fn new() -> Self {
        Self {
            state: GameState::new(),
            input: InputState::new(),
            renderer: Renderer::new(),
        }
    }

    pub fn update(&mut self) {
        self.input.update(self.state.spawn_menu_open);

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
                } else if self.input.select_map_pressed() {
                    self.state.map = self.state.map.next();
                    self.state.rebuild_world();
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

    fn spawn_menu_item(&mut self, item: usize) {
        let position = rapier2d::prelude::Vector::new(780.0, 180.0);

        match self.state.spawn_category {
            SpawnCategory::Pistols | SpawnCategory::Weapons => {
                self.state.physics.cycle_weapon();
                self.state.physics.spawn_box(
                    position,
                    rapier2d::prelude::Vector::new(38.0, 10.0),
                );
            }
            SpawnCategory::Melons => {
                let size = if item == 1 {
                    rapier2d::prelude::Vector::new(34.0, 34.0)
                } else {
                    rapier2d::prelude::Vector::new(28.0, 28.0)
                };
                self.state.physics.spawn_box(position, size);
            }
            SpawnCategory::Mechanisms => match item {
                0 => self.state.physics.spawn_box(
                    position,
                    rapier2d::prelude::Vector::new(34.0, 34.0),
                ),
                1 => self.state.physics.spawn_grenade(),
                2 => self.state.physics.spawn_barrel(),
                _ => self.state.physics.spawn_box(
                    position,
                    rapier2d::prelude::Vector::new(90.0, 14.0),
                ),
            },
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