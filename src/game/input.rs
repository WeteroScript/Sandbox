use macroquad::prelude::*;

pub struct InputState {
    play: bool,
    settings: bool,
    pause: bool,
    back: bool,
    spawn: bool,
    select_character: bool,
    select_map: bool,
    movement: Vec2,
}

impl InputState {
    pub fn new() -> Self {
        Self {
            play: false,
            settings: false,
            pause: false,
            back: false,
            spawn: false,
            select_character: false,
            select_map: false,
            movement: Vec2::ZERO,
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
        self.spawn = is_key_pressed(KeyCode::Q) || self.touch_spawn_pressed();
        self.select_character = is_key_pressed(KeyCode::C);
        self.select_map = is_key_pressed(KeyCode::M);

        let mut movement = Vec2::ZERO;

        if is_key_down(KeyCode::A) || is_key_down(KeyCode::Left) {
            movement.x -= 1.0;
        }
        if is_key_down(KeyCode::D) || is_key_down(KeyCode::Right) {
            movement.x += 1.0;
        }
        if is_key_down(KeyCode::W) || is_key_down(KeyCode::Up) {
            movement.y -= 1.0;
        }
        if is_key_down(KeyCode::S) || is_key_down(KeyCode::Down) {
            movement.y += 1.0;
        }

        if movement.length_squared() > 0.0 {
            movement = movement.normalize();
        }

        if let Some(touch) = touches().into_iter().next() {
            let joystick_center = vec2(100.0, screen_height() - 115.0);
            let offset = touch.position - joystick_center;

            if offset.length() <= 90.0 && offset.length_squared() > 36.0 {
                movement = (offset / 66.0).clamp_length_max(1.0);
            }
        }

        self.movement = movement;
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

    pub fn spawn_pressed(&self) -> bool {
        self.spawn
    }

    pub fn select_character_pressed(&self) -> bool {
        self.select_character
    }

    pub fn select_map_pressed(&self) -> bool {
        self.select_map
    }

    pub fn movement(&self) -> Vec2 {
        self.movement
    }

    fn touch_spawn_pressed(&self) -> bool {
        let action_center = vec2(screen_width() - 100.0, screen_height() - 118.0);
        let item_center = vec2(screen_width() - 192.0, screen_height() - 90.0);

        touches().into_iter().any(|touch| {
            touch.position.distance(action_center) <= 48.0
                || touch.position.distance(item_center) <= 38.0
        })
    }
}