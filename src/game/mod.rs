mod input;
mod physics;
mod render;
mod state;

use macroquad::prelude::*;

use input::{Action, InputState};
use render::Renderer;
use state::{GameMode, GameState};

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
        self.input.update();

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

                match self.input.action() {
                    Action::Fire => self.state.physics.fire_weapon(),
                    Action::SpawnGrenade => self.state.physics.spawn_grenade(),
                    Action::SpawnBarrel => self.state.physics.spawn_barrel(),
                    Action::None => {}
                }

                if self.input.spawn_pressed() {
                    self.state
                        .physics
                        .spawn_box(rapier2d::prelude::Vector::new(780.0, 180.0), rapier2d::prelude::Vector::new(34.0, 34.0));
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
                    self.state
                        .physics
                        .spawn_box(rapier2d::prelude::Vector::new(780.0, 180.0), rapier2d::prelude::Vector::new(34.0, 34.0));
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