mod input;
mod render;
mod state;

use macroquad::prelude::*;

use input::InputState;
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
                if self.input.play_pressed() {
                    self.state.mode = GameMode::Playing;
                } else if self.input.settings_pressed() {
                    self.state.mode = GameMode::Settings;
                }
            }
            GameMode::Playing => {
                self.state.elapsed += get_frame_time();

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
                if self.input.play_pressed() {
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
            GameMode::MainMenu => self.renderer.draw_main_menu(),
            GameMode::Playing => self.renderer.draw_playing(&self.state),
            GameMode::Settings => self.renderer.draw_settings(),
            GameMode::Paused => self.renderer.draw_paused(),
        }
    }
}