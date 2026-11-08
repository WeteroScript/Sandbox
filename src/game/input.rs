use macroquad::prelude::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Action {
    None,
    Fire,
}

pub struct InputState {
    play: bool,
    settings: bool,
    pause: bool,
    back: bool,
    damage: bool,
    select_character: bool,
    select_map: bool,
    next_weapon: bool,
    action: Action,
    movement: Vec2,
    camera_pan: Vec2,
    camera_zoom: f32,
    drag_pointer: Option<Vec2>,
    spawn_tab: Option<usize>,
    spawn_item: Option<usize>,
    previous_touches: Vec<(u64, Vec2)>,
}

impl InputState {
    pub fn new() -> Self {
        Self {
            play: false,
            settings: false,
            pause: false,
            back: false,
            damage: false,
            select_character: false,
            select_map: false,
            next_weapon: false,
            action: Action::None,
            movement: Vec2::ZERO,
            camera_pan: Vec2::ZERO,
            camera_zoom: 0.0,
            drag_pointer: None,
            spawn_tab: None,
            spawn_item: None,
            previous_touches: Vec::new(),
        }
    }

    pub fn update(
        &mut self,
        spawn_menu_open: bool,
        dragging: bool,
        spawn_dialog_open: bool,
    ) {
        self.play = is_key_pressed(KeyCode::Enter)
            || is_key_pressed(KeyCode::Space)
            || (is_mouse_button_pressed(MouseButton::Left)
                && !self.in_spawn_panel(mouse_position().into(), spawn_menu_open));

        self.settings = is_key_pressed(KeyCode::S)
            || is_key_pressed(KeyCode::Tab)
            || is_mouse_button_pressed(MouseButton::Right);

        self.pause = is_key_pressed(KeyCode::Escape) || is_key_pressed(KeyCode::P);
        self.back = is_key_pressed(KeyCode::Escape) || is_key_pressed(KeyCode::Backspace);
        self.damage = is_key_pressed(KeyCode::X);
        self.select_character = is_key_pressed(KeyCode::C);
        self.select_map = is_key_pressed(KeyCode::M);
        self.next_weapon = is_key_pressed(KeyCode::E);

        self.spawn_tab = None;
        self.spawn_item = None;

        if spawn_menu_open && !spawn_dialog_open {
            if let Some(position) = self.started_pointer() {
                let panel_x = screen_width() - 300.0;

                if position.x >= panel_x {
                    if position.y >= 58.0 && position.y < 116.0 {
                        let tab_width = 75.0;
                        self.spawn_tab = Some(((position.x - panel_x) / tab_width) as usize);
                    } else if position.y >= 126.0 {
                        let row_height = ((screen_height() - 136.0) / 10.0).clamp(16.0, 58.0);
                        let item = ((position.y - 126.0) / row_height) as usize;
                        if item < 16 {
                            self.spawn_item = Some(item);
                        }
                    }
                }
            }
        }

        self.action = if !spawn_dialog_open
            && (is_key_pressed(KeyCode::F)
                || (is_mouse_button_pressed(MouseButton::Left)
                    && !self.in_spawn_panel(mouse_position().into(), spawn_menu_open))
                || (self.touch_action_pressed() && !spawn_menu_open))
        {
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

        self.movement = movement;
        self.update_drag_pointer(spawn_menu_open);
        self.update_camera_gesture(spawn_menu_open, dragging);
    }

    pub fn play_pressed(&self) -> bool { self.play }
    pub fn settings_pressed(&self) -> bool { self.settings }
    pub fn pause_pressed(&self) -> bool { self.pause }
    pub fn back_pressed(&self) -> bool { self.back }
    pub fn damage_pressed(&self) -> bool { self.damage }
    pub fn select_character_pressed(&self) -> bool { self.select_character }
    pub fn select_map_pressed(&self) -> bool { self.select_map }
    pub fn next_weapon_pressed(&self) -> bool { self.next_weapon }
    pub fn action(&self) -> Action { self.action }
    pub fn movement(&self) -> Vec2 { self.movement }
    pub fn camera_pan(&self) -> Vec2 { self.camera_pan }
    pub fn camera_zoom(&self) -> f32 { self.camera_zoom }
    pub fn drag_pointer(&self) -> Option<Vec2> { self.drag_pointer }
    pub fn spawn_tab(&self) -> Option<usize> { self.spawn_tab }
    pub fn spawn_item(&self) -> Option<usize> { self.spawn_item }

    pub fn pointer_pressed(&self) -> Option<Vec2> {
        self.started_pointer()
    }

    pub fn map_pointer_pressed(&self) -> Option<Vec2> {
        self.started_pointer().filter(|position| {
            position.x < screen_width() - 300.0 && position.y > 62.0
        })
    }

    pub fn spawn_toggle_pressed(&self) -> bool {
        let width = screen_width();
        let mouse = mouse_position();

        if is_mouse_button_pressed(MouseButton::Left)
            && mouse.0 >= width - 72.0
            && mouse.1 <= 58.0
        {
            return true;
        }

        self.started_pointer()
            .map(|position| position.x >= width - 72.0 && position.y <= 58.0)
            .unwrap_or(false)
    }

    pub fn pause_resume_pressed(&self) -> bool { self.play }

    fn update_drag_pointer(&mut self, spawn_menu_open: bool) {
        self.drag_pointer = None;

        if is_mouse_button_down(MouseButton::Left) {
            let mouse = mouse_position();
            let position = vec2(mouse.0, mouse.1);

            if !self.in_spawn_panel(position, spawn_menu_open) {
                self.drag_pointer = Some(position);
                return;
            }
        }

        let active_touches: Vec<Touch> = touches()
            .into_iter()
            .filter(|touch| {
                matches!(
                    touch.phase,
                    TouchPhase::Started | TouchPhase::Stationary | TouchPhase::Moved
                )
            })
            .collect();

        if active_touches.len() == 1 && !self.in_spawn_panel(active_touches[0].position, spawn_menu_open) {
            self.drag_pointer = Some(active_touches[0].position);
        }
    }

    fn update_camera_gesture(&mut self, spawn_menu_open: bool, dragging: bool) {
        self.camera_pan = Vec2::ZERO;
        self.camera_zoom = 0.0;

        let current_touches = touches();

        if current_touches.len() >= 2 {
            let first = &current_touches[0];
            let second = &current_touches[1];

            if let (Some(previous_first), Some(previous_second)) = (
                self.previous_position(first.id),
                self.previous_position(second.id),
            ) {
                let previous_distance = previous_first.distance(previous_second);
                let current_distance = first.position.distance(second.position);

                if previous_distance > 1.0 {
                    self.camera_zoom =
                        (current_distance / previous_distance - 1.0).clamp(-0.12, 0.12);
                }

                self.camera_pan = (first.position + second.position) * 0.5
                    - (previous_first + previous_second) * 0.5;
            }
        } else if !dragging {
            if let Some(touch) = current_touches.first() {
                if !self.in_spawn_panel(touch.position, spawn_menu_open) {
                    if let Some(previous) = self.previous_position(touch.id) {
                        self.camera_pan = touch.position - previous;
                    }
                }
            }
        }

        self.previous_touches = current_touches
            .iter()
            .map(|touch| (touch.id, touch.position))
            .collect();
    }

    fn started_pointer(&self) -> Option<Vec2> {
        if is_mouse_button_pressed(MouseButton::Left) {
            let mouse = mouse_position();
            return Some(vec2(mouse.0, mouse.1));
        }

        touches()
            .into_iter()
            .find(|touch| touch.phase == TouchPhase::Started)
            .map(|touch| touch.position)
    }

    fn in_spawn_panel(&self, position: Vec2, open: bool) -> bool {
        open && position.x >= screen_width() - 300.0
    }

    fn previous_position(&self, id: u64) -> Option<Vec2> {
        self.previous_touches
            .iter()
            .find(|(previous_id, _)| *previous_id == id)
            .map(|(_, position)| *position)
    }

    fn touch_action_pressed(&self) -> bool {
        let center = vec2(screen_width() - 100.0, screen_height() - 118.0);

        touches()
            .into_iter()
            .any(|touch| {
                touch.phase == TouchPhase::Started
                    && touch.position.distance(center) <= 48.0
            })
    }
}