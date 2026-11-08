use macroquad::prelude::*;
use rapier2d::prelude::vector;

use super::physics::{CharacterKind, MapKind, PropKind, Ragdoll};
use super::state::{GameState, SpawnCategory};

pub struct Renderer;

impl Renderer {
    pub fn new() -> Self {
        Self
    }

    pub fn draw_main_menu(&self, state: &GameState) {
        self.draw_header("SANDBOX");
        let center_x = screen_width() * 0.5;
        let start_y = screen_height() * 0.5 - 145.0;

        self.draw_button(
            center_x - 170.0,
            start_y,
            340.0,
            58.0,
            "PLAY",
            Color::from_rgba(52, 168, 99, 255),
        );
        self.draw_button(
            center_x - 170.0,
            start_y + 72.0,
            340.0,
            52.0,
            &format!("CHARACTER: {}", state.character.name()),
            Color::from_rgba(65, 91, 120, 255),
        );
        self.draw_button(
            center_x - 170.0,
            start_y + 136.0,
            340.0,
            52.0,
            &format!("MAP: {}", state.map.name()),
            Color::from_rgba(77, 105, 91, 255),
        );

        draw_text(
            "C: character    M: map",
            center_x - 115.0,
            screen_height() - 72.0,
            18.0,
            LIGHTGRAY,
        );
        draw_text(
            "Enter or left click to play",
            center_x - 130.0,
            screen_height() - 42.0,
            18.0,
            GRAY,
        );
    }

    pub fn draw_playing(&self, state: &GameState) {
        let height = screen_height();

        set_camera(&state.camera.macroquad_camera());
        self.draw_background(state.map);

        draw_rectangle(
            0.0,
            1215.0,
            2400.0,
            300.0,
            Color::from_rgba(67, 75, 81, 255),
        );
        draw_rectangle(
            0.0,
            1210.0,
            2400.0,
            5.0,
            Color::from_rgba(118, 128, 121, 255),
        );

        self.draw_props(state);
        self.draw_explosions(state);
        self.draw_ragdoll(state, &state.physics.ragdoll, state.character);
        for (ragdoll, character) in &state.physics.spawned_ragdolls {
            self.draw_ragdoll(state, ragdoll, *character);
        }

        set_default_camera();
        self.draw_top_bar(state);
        self.draw_joint_health(state);
        if !state.spawn_menu_open {
            self.draw_fire_button();
        }
        if state.spawn_menu_open {
            self.draw_spawn_menu(state);
        } else {
            self.draw_spawn_toggle();
        }

        draw_text(
            &format!("Physics time: {:.1}", state.elapsed),
            24.0,
            height - 28.0,
            18.0,
            Color::from_rgba(220, 225, 226, 220),
        );
    }

    pub fn draw_settings(&self) {
        self.draw_header("SETTINGS");
        let center_x = screen_width() * 0.5;
        let center_y = screen_height() * 0.5;

        draw_text("Sound", center_x - 150.0, center_y - 20.0, 28.0, WHITE);
        draw_rectangle(
            center_x + 80.0,
            center_y - 45.0,
            100.0,
            48.0,
            Color::from_rgba(65, 91, 120, 255),
        );
        draw_text("ON", center_x + 113.0, center_y - 12.0, 24.0, WHITE);
        draw_text(
            "Escape or Backspace to return",
            center_x - 150.0,
            center_y + 100.0,
            18.0,
            GRAY,
        );
    }

    pub fn draw_paused(&self, state: &GameState) {
        self.draw_playing(state);
        draw_rectangle(
            0.0,
            0.0,
            screen_width(),
            screen_height(),
            Color::from_rgba(5, 8, 12, 225),
        );

        let center_x = screen_width() * 0.5;
        let center_y = screen_height() * 0.5;

        draw_text("PAUSED", center_x - 78.0, center_y - 175.0, 42.0, WHITE);
        self.draw_button(
            center_x - 150.0,
            center_y - 125.0,
            300.0,
            56.0,
            "RESUME",
            Color::from_rgba(52, 168, 99, 255),
        );
    }

    fn draw_fire_button(&self) {
        let center = vec2(screen_width() - 100.0, screen_height() - 118.0);
        draw_circle(
            center.x,
            center.y,
            47.0,
            Color::from_rgba(181, 54, 48, 185),
        );
        draw_circle_lines(center.x, center.y, 47.0, 3.0, Color::from_rgba(250, 210, 198, 230));
        draw_circle(center.x, center.y, 35.0, Color::from_rgba(205, 67, 58, 220));
        let label = "FIRE";
        let label_width = measure_text(label, None, 17, 1.0).width;
        draw_text(label, center.x - label_width * 0.5, center.y + 6.0, 17.0, WHITE);
    }

    fn draw_spawn_toggle(&self) {
        let x = screen_width() - 68.0;
        draw_rectangle(
            x,
            10.0,
            58.0,
            48.0,
            Color::from_rgba(38, 48, 60, 245),
        );
        draw_text("+", x + 17.0, 45.0, 32.0, WHITE);
    }

    fn draw_spawn_menu(&self, state: &GameState) {
        let width = screen_width();
        let panel_x = width - 300.0;

        draw_rectangle(
            panel_x,
            0.0,
            300.0,
            screen_height(),
            Color::from_rgba(25, 31, 40, 248),
        );
        draw_rectangle(
            panel_x,
            0.0,
            300.0,
            58.0,
            Color::from_rgba(39, 50, 63, 255),
        );
        draw_text("SPAWN", panel_x + 18.0, 37.0, 24.0, WHITE);

        draw_rectangle(
            width - 60.0,
            10.0,
            42.0,
            38.0,
            Color::from_rgba(78, 91, 105, 255),
        );
        draw_text("x", width - 47.0, 38.0, 22.0, WHITE);

        let categories = [
            SpawnCategory::Pistols,
            SpawnCategory::Weapons,
            SpawnCategory::Melons,
            SpawnCategory::Mechanisms,
        ];
        let tab_width = 75.0;

        for (index, category) in categories.iter().enumerate() {
            let x = panel_x + index as f32 * tab_width;
            let active = *category == state.spawn_category;

            draw_rectangle(
                x,
                58.0,
                tab_width,
                58.0,
                if active {
                    Color::from_rgba(62, 126, 91, 255)
                } else {
                    Color::from_rgba(45, 55, 68, 255)
                },
            );

            let label = match category {
                SpawnCategory::Pistols => "PIST",
                SpawnCategory::Weapons => "WEAP",
                SpawnCategory::Melons => "MEL",
                SpawnCategory::Mechanisms => "MECH",
            };
            let text_width = measure_text(label, None, 14, 1.0).width;
            draw_text(
                label,
                x + (tab_width - text_width) * 0.5,
                91.0,
                14.0,
                WHITE,
            );
        }

        let row_height = ((screen_height() - 136.0) / 10.0).clamp(16.0, 58.0);
        let icon_size = (row_height - 12.0).max(8.0);
        let font_size = if row_height < 30.0 { 12.0 } else { 17.0 };

        for (index, item) in state.spawn_category.items().iter().enumerate() {
            let y = 126.0 + index as f32 * row_height;
            draw_rectangle(
                panel_x + 12.0,
                y + 2.0,
                276.0,
                (row_height - 4.0).max(12.0),
                Color::from_rgba(48, 59, 72, 255),
            );
            draw_rectangle(
                panel_x + 22.0,
                y + (row_height - icon_size) * 0.5,
                icon_size,
                icon_size,
                self.item_color(state.spawn_category, index),
            );
            draw_text(
                item,
                panel_x + 70.0,
                y + row_height * 0.68,
                font_size,
                WHITE,
            );
        }
    }

    fn item_color(&self, category: SpawnCategory, index: usize) -> Color {
        match category {
            SpawnCategory::Pistols => match index {
                1 => Color::from_rgba(82, 91, 104, 255),
                2 => Color::from_rgba(165, 125, 76, 255),
                _ => Color::from_rgba(120, 128, 140, 255),
            },
            SpawnCategory::Weapons => match index {
                1 => Color::from_rgba(128, 92, 63, 255),
                2 => Color::from_rgba(145, 86, 49, 255),
                3 => Color::from_rgba(185, 195, 202, 255),
                _ => Color::from_rgba(76, 91, 108, 255),
            },
            SpawnCategory::Melons => {
                if index == 1 {
                    Color::from_rgba(156, 166, 176, 255)
                } else {
                    Color::from_rgba(106, 175, 92, 255)
                }
            }
            SpawnCategory::Mechanisms => match index {
                1 => Color::from_rgba(65, 175, 85, 255),
                2 => Color::from_rgba(170, 74, 48, 255),
                3 => Color::from_rgba(110, 123, 135, 255),
                _ => Color::from_rgba(150, 105, 62, 255),
            },
        }
    }

    fn draw_props(&self, state: &GameState) {
        for prop in &state.physics.props {
            if !prop.active {
                continue;
            }

            let Some(position) = state.physics.body_position(prop.body) else {
                continue;
            };

            let center = vec2(position.translation.x, position.translation.y);
            let angle = position.rotation.angle();

            match prop.kind {
                PropKind::Weapon => {
                    self.draw_weapon(
                        center,
                        angle,
                        prop.half_width,
                        prop.half_height,
                        prop.color,
                    );
                }
                PropKind::Character => {
                    if (prop.half_width - prop.half_height).abs() < 0.5 {
                        self.draw_melon_prop(center, angle, prop.half_width);
                    } else {
                        self.draw_robot_prop(center, angle, prop.half_width, prop.half_height);
                    }
                }
                PropKind::Grenade => self.draw_grenade(center, angle, prop.timer),
                PropKind::Barrel => self.draw_barrel(
                    center,
                    angle,
                    prop.half_width,
                    prop.half_height,
                ),
                PropKind::Box => self.draw_box_prop(
                    center,
                    angle,
                    prop.half_width,
                    prop.half_height,
                    prop.durability,
                ),
                PropKind::Platform => {
                    self.draw_platform(center, angle, prop.half_width, prop.half_height);
                }
                PropKind::Projectile => {
                    let velocity = state
                        .physics
                        .bodies
                        .get(prop.body)
                        .map(|body| *body.linvel())
                        .unwrap_or(vector![1.0, 0.0]);
                    let projectile_angle = velocity.y.atan2(velocity.x);

                    self.draw_rotated_rectangle(
                        center,
                        prop.half_width.max(4.0),
                        prop.half_height,
                        projectile_angle,
                        Color::from_rgba(242, 217, 91, 255),
                    );
                    draw_circle(center.x, center.y, 3.0, WHITE);
                }
                PropKind::Static => {
                    self.draw_rotated_rectangle(
                        center,
                        prop.half_width,
                        prop.half_height,
                        angle,
                        Color::from_rgba(
                            prop.color[0],
                            prop.color[1],
                            prop.color[2],
                            prop.color[3],
                        ),
                    );
                }
            }
        }
    }

    fn draw_weapon(
        &self,
        center: Vec2,
        angle: f32,
        half_width: f32,
        half_height: f32,
        raw_color: [u8; 4],
    ) {
        let color = Color::from_rgba(raw_color[0], raw_color[1], raw_color[2], raw_color[3]);
        let dark = Color::from_rgba(26, 30, 35, 255);
        let width = half_width * 2.0;

        self.draw_rotated_rectangle(center, half_width, half_height, angle, color);

        if width >= 88.0 {
            let stock = self.rotated_point(center, angle, -half_width + 12.0, 0.0);
            let barrel_start = self.rotated_point(center, angle, half_width * 0.35, -2.0);
            let barrel_end = self.rotated_point(center, angle, half_width + 13.0, -2.0);

            self.draw_rotated_rectangle(
                self.rotated_point(center, angle, -half_width * 0.55, half_height + 6.0),
                half_width * 0.22,
                8.0,
                angle,
                dark,
            );
            draw_line(
                barrel_start.x,
                barrel_start.y,
                barrel_end.x,
                barrel_end.y,
                4.0,
                dark,
            );
            draw_circle(stock.x, stock.y, 3.0, WHITE);
        } else if width >= 70.0 {
            let barrel_start = self.rotated_point(center, angle, half_width * 0.3, 0.0);
            let barrel_end = self.rotated_point(center, angle, half_width + 11.0, 0.0);

            draw_line(
                barrel_start.x,
                barrel_start.y,
                barrel_end.x,
                barrel_end.y,
                5.0,
                dark,
            );
            self.draw_rotated_rectangle(
                self.rotated_point(center, angle, -half_width * 0.25, half_height + 8.0),
                7.0,
                10.0,
                angle,
                dark,
            );
        } else if width >= 55.0 {
            let grip = self.rotated_point(center, angle, -half_width * 0.25, half_height + 9.0);

            self.draw_rotated_rectangle(grip, 8.0, 14.0, angle, dark);
            draw_circle(
                self.rotated_point(center, angle, half_width * 0.35, 0.0).x,
                self.rotated_point(center, angle, half_width * 0.35, 0.0).y,
                3.0,
                YELLOW,
            );
        } else {
            let grip = self.rotated_point(center, angle, -half_width * 0.25, half_height + 7.0);
            self.draw_rotated_rectangle(grip, 7.0, 11.0, angle, dark);
            draw_circle(
                self.rotated_point(center, angle, half_width * 0.35, 0.0).x,
                self.rotated_point(center, angle, half_width * 0.35, 0.0).y,
                2.5,
                YELLOW,
            );
        }
    }

    fn draw_melon_prop(&self, center: Vec2, angle: f32, radius: f32) {
        draw_circle(center.x, center.y, radius, Color::from_rgba(106, 175, 92, 255));
        draw_circle_lines(center.x, center.y, radius, 2.0, Color::from_rgba(38, 76, 47, 255));

        for offset in [-0.62_f32, -0.2, 0.22, 0.64] {
            let start = self.rotated_point(
                center,
                angle,
                offset * radius,
                -radius * 0.78,
            );
            let end = self.rotated_point(
                center,
                angle,
                offset * radius,
                radius * 0.78,
            );
            draw_line(
                start.x,
                start.y,
                end.x,
                end.y,
                2.0,
                Color::from_rgba(62, 125, 65, 230),
            );
        }

        let left_eye = self.rotated_point(center, angle, -radius * 0.28, -radius * 0.2);
        let right_eye = self.rotated_point(center, angle, radius * 0.28, -radius * 0.2);
        draw_circle(left_eye.x, left_eye.y, radius * 0.09, DARKGRAY);
        draw_circle(right_eye.x, right_eye.y, radius * 0.09, DARKGRAY);

        let mouth_left = self.rotated_point(center, angle, -radius * 0.2, radius * 0.2);
        let mouth_right = self.rotated_point(center, angle, radius * 0.2, radius * 0.2);
        draw_line(
            mouth_left.x,
            mouth_left.y,
            mouth_right.x,
            mouth_right.y,
            2.0,
            Color::from_rgba(45, 74, 44, 255),
        );
    }

    fn draw_robot_prop(&self, center: Vec2, angle: f32, half_width: f32, half_height: f32) {
        let body = Color::from_rgba(156, 166, 176, 255);
        let dark = Color::from_rgba(62, 72, 82, 255);
        let light = Color::from_rgba(208, 216, 219, 255);

        self.draw_rotated_rectangle(center, half_width, half_height, angle, body);
        self.draw_rotated_rectangle(
            self.rotated_point(center, angle, 0.0, -half_height * 0.25),
            half_width * 0.62,
            half_height * 0.3,
            angle,
            dark,
        );

        let eye_left = self.rotated_point(center, angle, -half_width * 0.28, -half_height * 0.24);
        let eye_right = self.rotated_point(center, angle, half_width * 0.28, -half_height * 0.24);
        draw_circle(eye_left.x, eye_left.y, 3.5, Color::from_rgba(85, 220, 184, 255));
        draw_circle(eye_right.x, eye_right.y, 3.5, Color::from_rgba(85, 220, 184, 255));

        draw_line(
            self.rotated_point(center, angle, -half_width * 0.55, half_height * 0.4).x,
            self.rotated_point(center, angle, -half_width * 0.55, half_height * 0.4).y,
            self.rotated_point(center, angle, half_width * 0.55, half_height * 0.4).x,
            self.rotated_point(center, angle, half_width * 0.55, half_height * 0.4).y,
            3.0,
            light,
        );
    }

    fn draw_grenade(&self, center: Vec2, angle: f32, timer: f32) {
        draw_circle(center.x, center.y, 14.0, Color::from_rgba(65, 175, 85, 255));
        draw_circle_lines(center.x, center.y, 14.0, 2.0, Color::from_rgba(27, 78, 45, 255));

        let neck = self.rotated_point(center, angle, 0.0, -13.0);
        self.draw_rotated_rectangle(neck, 5.0, 4.0, angle, Color::from_rgba(65, 70, 72, 255));

        let pin_left = self.rotated_point(center, angle, -4.0, -18.0);
        let pin_right = self.rotated_point(center, angle, 5.0, -18.0);
        draw_line(
            pin_left.x,
            pin_left.y,
            pin_right.x,
            pin_right.y,
            2.0,
            LIGHTGRAY,
        );

        let pulse = if timer > 0.0 {
            (3.0 - timer).sin().abs()
        } else {
            1.0
        };
        draw_circle(center.x, center.y, 3.0 + pulse * 2.0, Color::from_rgba(245, 210, 75, 255));
    }

    fn draw_barrel(&self, center: Vec2, angle: f32, half_width: f32, half_height: f32) {
        let body = Color::from_rgba(170, 74, 48, 255);
        let dark = Color::from_rgba(75, 35, 30, 255);

        self.draw_rotated_rectangle(center, half_width, half_height, angle, body);

        for offset in [-0.58_f32, 0.0, 0.58] {
            let point = self.rotated_point(center, angle, offset * half_width, 0.0);
            draw_line(
                self.rotated_point(point, angle, 0.0, -half_height).x,
                self.rotated_point(point, angle, 0.0, -half_height).y,
                self.rotated_point(point, angle, 0.0, half_height).x,
                self.rotated_point(point, angle, 0.0, half_height).y,
                3.0,
                dark,
            );
        }

        let cap_top = self.rotated_point(center, angle, 0.0, -half_height + 5.0);
        let cap_bottom = self.rotated_point(center, angle, 0.0, half_height - 5.0);
        draw_line(
            self.rotated_point(cap_top, angle, -half_width, 0.0).x,
            self.rotated_point(cap_top, angle, -half_width, 0.0).y,
            self.rotated_point(cap_top, angle, half_width, 0.0).x,
            self.rotated_point(cap_top, angle, half_width, 0.0).y,
            2.0,
            LIGHTGRAY,
        );
        draw_line(
            self.rotated_point(cap_bottom, angle, -half_width, 0.0).x,
            self.rotated_point(cap_bottom, angle, -half_width, 0.0).y,
            self.rotated_point(cap_bottom, angle, half_width, 0.0).x,
            self.rotated_point(cap_bottom, angle, half_width, 0.0).y,
            2.0,
            LIGHTGRAY,
        );
    }

    fn draw_box_prop(&self, center: Vec2, angle: f32, half_width: f32, half_height: f32, durability: f32) {
        let wood = Color::from_rgba(156, 101, 58, 255);
        let dark = Color::from_rgba(84, 53, 35, 255);

        self.draw_rotated_rectangle(center, half_width, half_height, angle, wood);

        let horizontal_left = self.rotated_point(center, angle, -half_width, 0.0);
        let horizontal_right = self.rotated_point(center, angle, half_width, 0.0);
        draw_line(
            horizontal_left.x,
            horizontal_left.y,
            horizontal_right.x,
            horizontal_right.y,
            3.0,
            dark,
        );

        let vertical_top = self.rotated_point(center, angle, 0.0, -half_height);
        let vertical_bottom = self.rotated_point(center, angle, 0.0, half_height);
        draw_line(
            vertical_top.x,
            vertical_top.y,
            vertical_bottom.x,
            vertical_bottom.y,
            3.0,
            dark,
        );

        if durability.is_finite() {
            let ratio = (durability / 3.0).clamp(0.0, 1.0);
            let bar_center = self.rotated_point(center, angle, 0.0, -half_height - 7.0);
            draw_line(
                bar_center.x - half_width,
                bar_center.y,
                bar_center.x + half_width,
                bar_center.y,
                3.0,
                Color::from_rgba(30, 35, 38, 220),
            );
            draw_line(
                bar_center.x - half_width,
                bar_center.y,
                bar_center.x - half_width + half_width * 2.0 * ratio,
                bar_center.y,
                3.0,
                Color::from_rgba(80, 205, 101, 255),
            );
        }
    }

    fn draw_platform(&self, center: Vec2, angle: f32, half_width: f32, half_height: f32) {
        let metal = Color::from_rgba(110, 123, 135, 255);
        let edge = Color::from_rgba(205, 214, 216, 255);

        self.draw_rotated_rectangle(center, half_width, half_height, angle, metal);

        for offset in [-0.7_f32, -0.35, 0.0, 0.35, 0.7] {
            let point = self.rotated_point(center, angle, offset * half_width, 0.0);
            let start = self.rotated_point(point, angle, 0.0, -half_height);
            let end = self.rotated_point(point, angle, 0.0, half_height);
            draw_line(start.x, start.y, end.x, end.y, 2.0, edge);
        }
    }

    fn draw_explosions(&self, state: &GameState) {
        for explosion in &state.physics.explosions {
            let alpha = (explosion.life / 0.35).clamp(0.0, 1.0);

            draw_circle(
                explosion.position.x,
                explosion.position.y,
                explosion.radius * 0.22 * (1.0 - alpha),
                Color::new(1.0, 0.32, 0.04, alpha * 0.55),
            );
            draw_circle_lines(
                explosion.position.x,
                explosion.position.y,
                explosion.radius * (1.0 - alpha * 0.35),
                8.0,
                Color::new(1.0, 0.35, 0.05, alpha),
            );
        }
    }

    fn draw_ragdoll(&self, state: &GameState, ragdoll: &Ragdoll, character: CharacterKind) {
        let (primary, secondary) = character.colors();
        let primary = Color::from_rgba(primary[0], primary[1], primary[2], primary[3]);
        let secondary = Color::from_rgba(secondary[0], secondary[1], secondary[2], secondary[3]);

        self.draw_circle_body(state, ragdoll.head, 27.0, primary);
        self.draw_box_body(state, ragdoll.torso, 24.0, 44.0, secondary);

        self.draw_part(state, ragdoll.left_upper_arm, 10.0, 32.0, primary);
        self.draw_part(state, ragdoll.right_upper_arm, 10.0, 32.0, primary);
        self.draw_part(state, ragdoll.left_lower_arm, 9.0, 28.0, primary);
        self.draw_part(state, ragdoll.right_lower_arm, 9.0, 28.0, primary);
        self.draw_part(state, ragdoll.left_upper_leg, 12.0, 38.0, secondary);
        self.draw_part(state, ragdoll.right_upper_leg, 12.0, 38.0, secondary);
        self.draw_part(state, ragdoll.left_lower_leg, 10.0, 34.0, secondary);
        self.draw_part(state, ragdoll.right_lower_leg, 10.0, 34.0, secondary);

        if let Some(head) = state.physics.body_position(ragdoll.head) {
            let center = vec2(head.translation.x, head.translation.y);
            let angle = head.rotation.angle();

            match character {
                CharacterKind::Melon => self.draw_melon_head(center, angle),
                CharacterKind::Robot => self.draw_robot_head(center, angle),
            }
        }

        if let Some(torso) = state.physics.body_position(ragdoll.torso) {
            let center = vec2(torso.translation.x, torso.translation.y);
            let angle = torso.rotation.angle();

            match character {
                CharacterKind::Melon => {
                    let leaf = self.rotated_point(center, angle, 0.0, -45.0);
                    draw_circle(leaf.x, leaf.y, 7.0, Color::from_rgba(48, 113, 55, 255));
                    draw_line(
                        self.rotated_point(center, angle, -17.0, 0.0).x,
                        self.rotated_point(center, angle, -17.0, 0.0).y,
                        self.rotated_point(center, angle, 17.0, 0.0).x,
                        self.rotated_point(center, angle, 17.0, 0.0).y,
                        3.0,
                        Color::from_rgba(47, 91, 50, 220),
                    );
                }
                CharacterKind::Robot => {
                    self.draw_rotated_rectangle(
                        self.rotated_point(center, angle, 0.0, -8.0),
                        12.0,
                        5.0,
                        angle,
                        Color::from_rgba(77, 212, 180, 255),
                    );
                    draw_line(
                        self.rotated_point(center, angle, -18.0, 18.0).x,
                        self.rotated_point(center, angle, -18.0, 18.0).y,
                        self.rotated_point(center, angle, 18.0, 18.0).x,
                        self.rotated_point(center, angle, 18.0, 18.0).y,
                        3.0,
                        Color::from_rgba(70, 80, 90, 255),
                    );
                }
            }
        }
    }

    fn draw_melon_head(&self, center: Vec2, angle: f32) {
        draw_circle(center.x, center.y, 27.0, Color::from_rgba(106, 175, 92, 255));
        draw_circle_lines(center.x, center.y, 27.0, 2.0, Color::from_rgba(38, 76, 47, 255));

        for offset in [-0.55_f32, -0.18, 0.2, 0.58] {
            let start = self.rotated_point(center, angle, offset * 27.0, -23.0);
            let end = self.rotated_point(center, angle, offset * 27.0, 23.0);
            draw_line(
                start.x,
                start.y,
                end.x,
                end.y,
                2.0,
                Color::from_rgba(62, 125, 65, 230),
            );
        }

        let left_eye = self.rotated_point(center, angle, -8.0, -5.0);
        let right_eye = self.rotated_point(center, angle, 8.0, -5.0);
        draw_circle(left_eye.x, left_eye.y, 3.5, DARKGRAY);
        draw_circle(right_eye.x, right_eye.y, 3.5, DARKGRAY);

        let mouth_left = self.rotated_point(center, angle, -7.0, 8.0);
        let mouth_right = self.rotated_point(center, angle, 7.0, 8.0);
        draw_line(
            mouth_left.x,
            mouth_left.y,
            mouth_right.x,
            mouth_right.y,
            2.0,
            Color::from_rgba(45, 74, 44, 255),
        );
    }

    fn draw_robot_head(&self, center: Vec2, angle: f32) {
        self.draw_rotated_rectangle(
            center,
            27.0,
            27.0,
            angle,
            Color::from_rgba(156, 166, 176, 255),
        );
        self.draw_rotated_rectangle(
            self.rotated_point(center, angle, 0.0, -5.0),
            18.0,
            9.0,
            angle,
            Color::from_rgba(62, 72, 82, 255),
        );

        let left_eye = self.rotated_point(center, angle, -8.0, -5.0);
        let right_eye = self.rotated_point(center, angle, 8.0, -5.0);
        draw_circle(left_eye.x, left_eye.y, 3.5, Color::from_rgba(85, 220, 184, 255));
        draw_circle(right_eye.x, right_eye.y, 3.5, Color::from_rgba(85, 220, 184, 255));

        let antenna = self.rotated_point(center, angle, 0.0, -30.0);
        draw_line(
            self.rotated_point(center, angle, 0.0, -27.0).x,
            self.rotated_point(center, angle, 0.0, -27.0).y,
            antenna.x,
            antenna.y,
            2.0,
            LIGHTGRAY,
        );
        draw_circle(antenna.x, antenna.y, 3.0, Color::from_rgba(85, 220, 184, 255));
    }

    fn draw_part(
        &self,
        state: &GameState,
        handle: rapier2d::prelude::RigidBodyHandle,
        width: f32,
        height: f32,
        color: Color,
    ) {
        if !state.physics.is_detached(handle) {
            self.draw_box_body(state, handle, width, height, color);
        }
    }

    fn draw_circle_body(
        &self,
        state: &GameState,
        handle: rapier2d::prelude::RigidBodyHandle,
        radius: f32,
        color: Color,
    ) {
        if let Some(position) = state.physics.body_position(handle) {
            draw_circle(position.translation.x, position.translation.y, radius, color);
            draw_circle_lines(
                position.translation.x,
                position.translation.y,
                radius,
                2.0,
                Color::from_rgba(30, 45, 42, 220),
            );
        }
    }

    fn draw_box_body(
        &self,
        state: &GameState,
        handle: rapier2d::prelude::RigidBodyHandle,
        width: f32,
        height: f32,
        color: Color,
    ) {
        if let Some(position) = state.physics.body_position(handle) {
            self.draw_rotated_rectangle(
                vec2(position.translation.x, position.translation.y),
                width,
                height,
                position.rotation.angle(),
                color,
            );
        }
    }

    fn draw_rotated_rectangle(
        &self,
        center: Vec2,
        width: f32,
        height: f32,
        angle: f32,
        color: Color,
    ) {
        let corners = [
            vec2(-width, -height),
            vec2(width, -height),
            vec2(width, height),
            vec2(-width, height),
        ];
        let points: Vec<Vec2> = corners
            .iter()
            .map(|point| self.rotated_point(center, angle, point.x, point.y))
            .collect();

        draw_triangle(points[0], points[1], points[2], color);
        draw_triangle(points[0], points[2], points[3], color);

        for index in 0..4 {
            let next = (index + 1) % 4;
            draw_line(
                points[index].x,
                points[index].y,
                points[next].x,
                points[next].y,
                2.0,
                BLACK,
            );
        }
    }

    fn rotated_point(&self, center: Vec2, angle: f32, local_x: f32, local_y: f32) -> Vec2 {
        let sin = angle.sin();
        let cos = angle.cos();

        vec2(
            center.x + local_x * cos - local_y * sin,
            center.y + local_x * sin + local_y * cos,
        )
    }

    fn draw_joint_health(&self, state: &GameState) {
        let ratio = state.physics.joint_health_ratio();
        let x = screen_width() - 260.0;
        let y = 76.0;
        let width = 220.0;

        draw_text("JOINT INTEGRITY", x, y, 16.0, WHITE);
        draw_rectangle(
            x,
            y + 8.0,
            width,
            14.0,
            Color::from_rgba(35, 40, 46, 220),
        );

        let color = if ratio > 0.6 {
            Color::from_rgba(65, 185, 92, 255)
        } else if ratio > 0.3 {
            Color::from_rgba(220, 166, 55, 255)
        } else {
            Color::from_rgba(210, 62, 62, 255)
        };

        draw_rectangle(x, y + 8.0, width * ratio, 14.0, color);
        draw_text(
            &format!("{:.0}%", ratio * 100.0),
            x + width + 8.0,
            y + 20.0,
            16.0,
            WHITE,
        );
    }

    fn draw_background(&self, map: MapKind) {
        let color = match map {
            MapKind::TestRange => Color::from_rgba(29, 38, 50, 255),
            MapKind::City => Color::from_rgba(42, 49, 62, 255),
            MapKind::Laboratory => Color::from_rgba(25, 49, 53, 255),
        };

        draw_rectangle(0.0, 0.0, 2400.0, 1215.0, color);

        match map {
            MapKind::TestRange => {
                for x in (0..2400).step_by(100) {
                    draw_line(
                        x as f32,
                        0.0,
                        x as f32,
                        1215.0,
                        1.0,
                        Color::from_rgba(55, 67, 80, 180),
                    );
                }
            }
            MapKind::City => {
                draw_rectangle(
                    0.0,
                    800.0,
                    2400.0,
                    415.0,
                    Color::from_rgba(48, 52, 58, 255),
                );
                for x in (0..2400).step_by(220) {
                    draw_rectangle(
                        x as f32,
                        350.0,
                        135.0,
                        450.0,
                        Color::from_rgba(67, 76, 91, 255),
                    );
                }
            }
            MapKind::Laboratory => {
                draw_circle(500.0, 1160.0, 110.0, Color::from_rgba(45, 190, 104, 170));
                draw_circle(1750.0, 1150.0, 130.0, Color::from_rgba(45, 190, 104, 170));
                draw_line(
                    730.0,
                    220.0,
                    730.0,
                    980.0,
                    8.0,
                    Color::from_rgba(235, 55, 75, 220),
                );
                draw_line(
                    1670.0,
                    220.0,
                    1670.0,
                    980.0,
                    8.0,
                    Color::from_rgba(235, 55, 75, 220),
                );
            }
        }
    }

    fn draw_header(&self, title: &str) {
        draw_rectangle(
            0.0,
            0.0,
            screen_width(),
            88.0,
            Color::from_rgba(28, 35, 45, 255),
        );
        draw_text(title, 32.0, 57.0, 32.0, WHITE);
    }

    fn draw_top_bar(&self, state: &GameState) {
        draw_rectangle(
            0.0,
            0.0,
            screen_width(),
            62.0,
            Color::from_rgba(28, 35, 45, 235),
        );
        draw_text(state.map.name(), 24.0, 39.0, 22.0, WHITE);
        draw_text(
            state.character.name(),
            screen_width() - 150.0,
            28.0,
            16.0,
            LIGHTGRAY,
        );
        draw_text(
            state.physics.weapon.name(),
            screen_width() - 150.0,
            48.0,
            16.0,
            YELLOW,
        );
    }

    fn draw_button(
        &self,
        x: f32,
        y: f32,
        width: f32,
        height: f32,
        label: &str,
        color: Color,
    ) {
        draw_rectangle(x, y, width, height, color);
        draw_rectangle_lines(
            x,
            y,
            width,
            height,
            2.0,
            Color::from_rgba(225, 230, 230, 80),
        );

        let text_width = measure_text(label, None, 24, 1.0).width;
        draw_text(
            label,
            x + (width - text_width) * 0.5,
            y + height * 0.64,
            24.0,
            WHITE,
        );
    }
}