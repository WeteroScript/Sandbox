use macroquad::prelude::*;

pub struct InputState {
    play: bool,
    settings: bool,
    pause: bool,
    back: bool,
}

impl InputState {
    pub fn new() -> Self {
        Self {
            play: false,
            settings: false,
            pause: false,
            back: false,
        }
    }

    pub fn update(&mut self) {
        self.play = is_key_pressed(KeyCode::Enter)
            || is_key_pressed(KeyCode::Space)
            || is_mouse_button_pressed(MouseButton::Left);

        self.settings = is_key_pressed(KeyCode::S)
            || is_key_pressed(KeyCode::Tab)
            || is_mouse_button_pressed(MouseButton::Right);

        self.pause = is_key_pressed(KeyCode::Escape) || is_key_pressed(KeyCode::P);
        self.back = is_key_pressed(KeyCode::Escape) || is_key_pressed(KeyCode::Backspace);
    }

    pub fn play_pressed(&self) -> bool {
        self.play
    }

    pub fn settings_pressed(&self) -> bool {
        self.settings
    }

    pub fn pause_pressed(&self) -> bool {
        self.pause
    }

    pub fn back_pressed(&self) -> bool {
        self.back
    }
}