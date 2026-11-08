mod input;
mod mechanisms;
mod physics;
mod render;
mod state;
mod toggle;

use macroquad::prelude::*;

use input::{Action, InputState};
use mechanisms::MechanismWorld;
use physics::{CharacterKind, WeaponKind};
use render::Renderer;
use state::{GameMode, GameState, SpawnCategory, SpawnItem};
use toggle::ToggleInteraction;

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
        let spawn_dialog_was_open = self.state.pending_spawn.is_some();
        let spawn_ui_active = self.state.spawn_menu_open
            && matches!(self.state.mode, GameMode::Playing | GameMode::Paused);
        self.input.update(
            spawn_ui_active,
            self.drag_body.is_some(),
            spawn_dialog_was_open,
        );

        self.handle_spawn_dialog();

        if !spawn_dialog_was_open
            && matches!(self.state.mode, GameMode::Playing | GameMode::Paused)
            && self.input.spawn_toggle_pressed()
        {
            self.state.spawn_menu_open = !self.state.spawn_menu_open;
        }

        if !spawn_dialog_was_open {
            if let Some(tab) = self.input.spawn_tab() {
            self.state.spawn_category = match tab {
                0 => SpawnCategory::Pistols,
                1 => SpawnCategory::Weapons,
                2 => SpawnCategory::Melons,
                _ => SpawnCategory::Mechanisms,
                };
            }
        }

        self.update_toggle_interaction();
        self.update_dragging();

        if matches!(self.state.mode, GameMode::Playing | GameMode::Paused) {
            self.state
                .camera
                .apply_gesture(self.input.camera_pan(), self.input.camera_zoom());
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
                    self.state.spawn_menu_open = true;
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

                if matches!(self.input.action(), Action::Fire) {
                    self.state.physics.fire_weapon();
                }

                if self.input.damage_pressed() {
                    self.state.physics.apply_test_damage();
                }

                self.state.physics.step(delta_time);
                self.mechanisms.step(&mut self.state.physics, delta_time);

                if self.input.pause_pressed() && !spawn_dialog_was_open {
                    self.state.mode = GameMode::Paused;
                }
            }
            GameMode::Settings => {
                if self.input.back_pressed() {
                    self.state.mode = GameMode::MainMenu;
                }
            }
            GameMode::Paused => {
                if self.input.pause_resume_pressed() {
                    self.state.mode = GameMode::Playing;
                } else if self.input.back_pressed() {
                    self.state.mode = GameMode::MainMenu;
                    self.state.spawn_menu_open = false;
                    self.state.pending_spawn = None;
                    self.state.spawn_placing = false;
                }
            }
        }
    }

    fn handle_spawn_dialog(&mut self) {
        if !matches!(self.state.mode, GameMode::Playing | GameMode::Paused) {
            return;
        }

        if self.state.pending_spawn.is_none() {
            if let Some(index) = self.input.spawn_item() {
                if let Some(selected) = self.state.spawn_category.item(index) {
                    self.state.pending_spawn = Some(selected);
                    self.state.spawn_count = 1;
                    self.state.spawn_placing = false;
                }
            }
            return;
        }

        let item = self.state.pending_spawn.expect("checked above");

        if self.input.back_pressed() {
            self.state.pending_spawn = None;
            self.state.spawn_placing = false;
            return;
        }

        if self.state.spawn_placing {
            if let Some(pointer) = self.input.map_pointer_pressed() {
                let world = self.screen_to_world(pointer);
                self.spawn_many(item, self.state.spawn_count, world);
                self.state.pending_spawn = None;
                self.state.spawn_placing = false;
            }
            return;
        }

        if let Some(index) = self.input.spawn_item() {
            if let Some(selected) = self.state.spawn_category.item(index) {
                self.state.pending_spawn = Some(selected);
                self.state.spawn_count = 1;
            }
        }

        let Some(pointer) = self.input.pointer_pressed() else {
            return;
        };

        let width = screen_width();
        let height = screen_height();
        let left = width * 0.5 - 190.0;
        let top = height * 0.5 - 122.0;

        if pointer.x >= left + 24.0
            && pointer.x <= left + 94.0
            && pointer.y >= top + 106.0
            && pointer.y <= top + 158.0
        {
            self.state.spawn_count = self.state.spawn_count.saturating_sub(1).max(1);
        } else if pointer.x >= left + 286.0
            && pointer.x <= left + 356.0
            && pointer.y >= top + 106.0
            && pointer.y <= top + 158.0
        {
            let limit = if matches!(item, SpawnItem::Melon | SpawnItem::Robot) {
                16
            } else {
                100
            };
            self.state.spawn_count = self.state.spawn_count.saturating_add(1).min(limit);
        } else if pointer.x >= left + 94.0
            && pointer.x <= left + 286.0
            && pointer.y >= top + 106.0
            && pointer.y <= top + 158.0
        {
            self.state.spawn_placing = true;
        }
    }

    fn spawn_many(&mut self, item: SpawnItem, count: u32, position: Vec2) {
        let total = count.max(1);
        let spacing = if matches!(item, SpawnItem::Melon | SpawnItem::Robot) {
            120.0
        } else if matches!(item, SpawnItem::MelonHead) {
            64.0
        } else if matches!(item, SpawnItem::Pistol | SpawnItem::SilencedPistol
            | SpawnItem::HeavyPistol | SpawnItem::Rifle | SpawnItem::Shotgun
            | SpawnItem::Bat | SpawnItem::Katana)
        {
            78.0
        } else {
            68.0
        };
        let character_batch = matches!(item, SpawnItem::Melon | SpawnItem::Robot);
        let spacing_x = if character_batch { 170.0 } else { spacing };
        let spacing_y = if character_batch { 250.0 } else { spacing };
        let columns = (total as f32).sqrt().ceil().max(1.0) as u32;
        let rows = (total + columns - 1) / columns;

        for index in 0..total {
            let column = index % columns;
            let row = index / columns;
            let offset_x = (column as f32 - (columns - 1) as f32 * 0.5) * spacing_x;
            let offset_y = (row as f32 - (rows - 1) as f32 * 0.5) * spacing_y;
            let spawn_position = rapier2d::prelude::Vector::new(
                (position.x + offset_x).clamp(75.0, 2325.0),
                (position.y + offset_y).clamp(45.0, 1355.0),
            );
            self.spawn_menu_item_at(item, spawn_position);
        }
    }

    fn spawn_menu_item_at(
        &mut self,
        item: SpawnItem,
        position: rapier2d::prelude::Vector<f32>,
    ) {
        match item {
            SpawnItem::Pistol => self.state.physics.spawn_weapon(position, WeaponKind::Pistol),
            SpawnItem::SilencedPistol => self.state.physics.spawn_weapon(position, WeaponKind::SilencedPistol),
            SpawnItem::HeavyPistol => self.state.physics.spawn_weapon(position, WeaponKind::HeavyPistol),
            SpawnItem::Rifle => self.state.physics.spawn_weapon(position, WeaponKind::Rifle),
            SpawnItem::Shotgun => self.state.physics.spawn_weapon(position, WeaponKind::Shotgun),
            SpawnItem::Bat => self.state.physics.spawn_weapon(position, WeaponKind::Bat),
            SpawnItem::Katana => self.state.physics.spawn_weapon(position, WeaponKind::Katana),
            SpawnItem::Melon => {
                self.state.physics.spawn_character_prop(position, CharacterKind::Melon)
            }
            SpawnItem::MelonHead => self.state.physics.spawn_melon_head(position),
            SpawnItem::Robot => {
                self.state.physics.spawn_character_prop(position, CharacterKind::Robot)
            }
            SpawnItem::Box => self.state.physics.spawn_box(
                position,
                rapier2d::prelude::Vector::new(34.0, 34.0),
            ),
            SpawnItem::Grenade => self.state.physics.spawn_grenade(position),
            SpawnItem::Barrel => self.state.physics.spawn_barrel(position),
            SpawnItem::Platform => self.state.physics.spawn_platform(position),
            SpawnItem::Rod => self.mechanisms.spawn_rod(
                &self.state.physics,
                Vec2::new(position.x, position.y),
            ),
            SpawnItem::Rope => self.mechanisms.spawn_rope(
                &self.state.physics,
                Vec2::new(position.x, position.y),
            ),
            SpawnItem::Spring => self.mechanisms.spawn_spring(
                &self.state.physics,
                Vec2::new(position.x, position.y),
            ),
            SpawnItem::Piston => self.mechanisms.spawn_piston(
                &self.state.physics,
                Vec2::new(position.x, position.y),
            ),
            SpawnItem::Button => self.mechanisms.spawn_button(
                &self.state.physics,
                Vec2::new(position.x, position.y),
            ),
            SpawnItem::Toggle => self.mechanisms.spawn_toggle(
                &self.state.physics,
                Vec2::new(position.x, position.y),
            ),
        }
    }

    fn update_toggle_interaction(&mut self) {
        if self.state.mode != GameMode::Playing
            || self.state.spawn_menu_open
            || self.state.pending_spawn.is_some()
        {
            return;
        }

        let pointer = if is_mouse_button_pressed(MouseButton::Left) {
            let mouse = mouse_position();
            Some(vec2(mouse.0, mouse.1))
        } else {
            touches()
                .into_iter()
                .find(|touch| touch.phase == TouchPhase::Started)
                .map(|touch| touch.position)
        };

        let Some(pointer) = pointer else {
            return;
        };

        if pointer.y <= 62.0 || pointer.x < 0.0 || pointer.x > screen_width() {
            return;
        }

        let world_position = self.screen_to_world(pointer);
        self.mechanisms.toggle_at(&self.state.physics, world_position);
    }

    fn update_dragging(&mut self) {
        if self.state.pending_spawn.is_some() {
            self.drag_body = None;
            return;
        }

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
                        rapier2d::prelude::Vector::new(world_position.x, world_position.y),
                        true,
                    );
                    body.set_linvel(rapier2d::prelude::Vector::new(0.0, 0.0), true);
                    body.set_angvel(0.0, true);
                }
            }
        }
    }

    fn find_body_at(
        &self,
        world_position: Vec2,
    ) -> Option<rapier2d::prelude::RigidBodyHandle> {
        let mut handles = vec![
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

        for (ragdoll, _) in &self.state.physics.spawned_ragdolls {
            handles.extend([
                ragdoll.head,
                ragdoll.torso,
                ragdoll.left_upper_arm,
                ragdoll.right_upper_arm,
                ragdoll.left_lower_arm,
                ragdoll.right_lower_arm,
                ragdoll.left_upper_leg,
                ragdoll.right_upper_leg,
                ragdoll.left_lower_leg,
                ragdoll.right_lower_leg,
            ]);
        }

        let mut nearest = None;

        for handle in handles {
            if let Some(position) = self.state.physics.body_position(handle) {
                let distance = vec2(
                    position.translation.x,
                    position.translation.y,
                ).distance(world_position);

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
            ).distance(world_position);

            if distance <= prop.half_width.max(prop.half_height) + 28.0 {
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
                + (position.y - screen_height() * 0.5) / zoom,
        )
    }

    fn draw_spawn_dialog(&self) {
        let Some(item) = self.state.pending_spawn else {
            return;
        };

        let width = screen_width();
        let height = screen_height();
        let left = width * 0.5 - 190.0;
        let top = height * 0.5 - 122.0;

        draw_rectangle(
            0.0,
            0.0,
            width,
            height,
            Color::from_rgba(0, 0, 0, 145),
        );
        draw_rectangle(
            left,
            top,
            380.0,
            180.0,
            Color::from_rgba(30, 39, 49, 255),
        );
        draw_rectangle_lines(left, top, 380.0, 180.0, 2.0, WHITE);

        let title = if self.state.spawn_placing {
            "SELECT MAP POSITION"
        } else {
            "HOW MANY TO ADD?"
        };

        draw_text(title, left + 24.0, top + 38.0, 23.0, WHITE);
        draw_text(item.name(), left + 24.0, top + 70.0, 17.0, LIGHTGRAY);

        if self.state.spawn_placing {
            draw_text(
                "Tap a place on the map",
                left + 24.0,
                top + 112.0,
                18.0,
                Color::from_rgba(221, 225, 228, 255),
            );
            draw_text(
                "Escape cancels",
                left + 24.0,
                top + 144.0,
                15.0,
                GRAY,
            );
            return;
        }

        draw_rectangle(
            left + 24.0,
            top + 106.0,
            70.0,
            52.0,
            Color::from_rgba(75, 86, 98, 255),
        );
        draw_text("-", left + 49.0, top + 142.0, 30.0, WHITE);

        draw_rectangle(
            left + 104.0,
            top + 106.0,
            182.0,
            52.0,
            Color::from_rgba(48, 61, 76, 255),
        );
        let count = self.state.spawn_count.to_string();
        let text_width = measure_text(&count, None, 25, 1.0).width;
        draw_text(
            &count,
            left + 195.0 - text_width * 0.5,
            top + 141.0,
            25.0,
            WHITE,
        );

        draw_rectangle(
            left + 286.0,
            top + 106.0,
            70.0,
            52.0,
            Color::from_rgba(75, 116, 83, 255),
        );
        draw_text("+", left + 308.0, top + 142.0, 30.0, WHITE);
    }

    pub fn render(&mut self) {
        clear_background(Color::from_rgba(18, 22, 30, 255));

        match self.state.mode {
            GameMode::MainMenu => self.renderer.draw_main_menu(&self.state),
            GameMode::Playing => {
                self.renderer.draw_playing(&self.state);
                self.draw_spawn_dialog();
            }
            GameMode::Settings => self.renderer.draw_settings(),
            GameMode::Paused => {
                self.renderer.draw_paused(&self.state);
                self.draw_spawn_dialog();
            }
        }
    }
}