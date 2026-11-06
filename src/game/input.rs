use macroquad::prelude::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    None,
    Fire,
    SpawnGrenade,
    SpawnBarrel,
}

pub struct InputState {
    play: bool,
    settings: bool,
    pause: bool,
    back: bool,
    spawn: bool,
    damage: bool,
    select_character: bool,
    select_map: bool,
    next_weapon: bool,
    action: Action,
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
            damage: false,
            select_character: false,
            select_map: false,
            next_weapon: false,
            action: Action::None,
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
        self.spawn = is_key_pressed(KeyCode::Q);
        self.damage = is_key_pressed(KeyCode::X);
        self.select_character = is_key_pressed(KeyCode::C);
        self.select_map = is_key_pressed(KeyCode::M);
        self.next_weapon = is_key_pressed(KeyCode::E);

        self.action = if is_key_pressed(KeyCode::G) {
            Action::SpawnGrenade
        } else if is_key_pressed(KeyCode::B) {
            Action::SpawnBarrel
        } else if is_key_pressed(KeyCode::F) || is_mouse_button_pressed(MouseButton::Left) {
            Action::Fire
        } else if self.touch_action_pressed() {
            Action::Fire
        } else {
            Action::None
        };

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

        for touch in touches() {
            let center = vec2(100.0, screen_height() - 115.0);
            let offset = touch.position - center;

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

    pub fn damage_pressed(&self) -> bool {
        self.damage
    }

    pub fn select_character_pressed(&self) -> bool {
        self.select_character
    }

    pub fn select_map_pressed(&self) -> bool {
        self.select_map
    }

    pub fn next_weapon_pressed(&self) -> bool {
        self.next_weapon
    }

    pub fn action(&self) -> Action {
        self.action
    }

    pub fn movement(&self) -> Vec2 {
        self.movement
    }

    fn touch_action_pressed(&self) -> bool {
        let center = vec2(screen_width() - 100.0, screen_height() - 118.0);

        touches()
            .into_iter()
            .any(|touch| touch.position.distance(center) <= 48.0)
    }
}