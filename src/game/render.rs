use macroquad::prelude::*;

use super::physics::{CharacterKind, MapKind, PropKind, Ragdoll};
use super::state::GameState;

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
        let width = screen_width();
        let height = screen_height();

        self.draw_background(state.map);

        draw_rectangle(
            0.0,
            650.0,
            width.max(1280.0),
            height.max(720.0) - 650.0,
            Color::from_rgba(67, 75, 81, 255),
        );
        draw_rectangle(
            0.0,
            645.0,
            width.max(1280.0),
            5.0,
            Color::from_rgba(118, 128, 121, 255),
        );

        self.draw_props(state);
        self.draw_explosions(state);
        self.draw_ragdoll(state, &state.physics.ragdoll, state.character);
        self.draw_top_bar(state);
        self.draw_joint_health(state);
        self.draw_touch_controls();

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
        draw_text(
            "Spawn items while the simulation is paused",
            center_x - 205.0,
            center_y - 145.0,
            16.0,
            LIGHTGRAY,
        );

        self.draw_button(
            center_x - 150.0,
            center_y - 125.0,
            300.0,
            56.0,
            "RESUME",
            Color::from_rgba(52, 168, 99, 255),
        );
        self.draw_button(
            center_x - 150.0,
            center_y - 50.0,
            300.0,
            48.0,
            "SPAWN BOX",
            Color::from_rgba(126, 91, 55, 255),
        );
        self.draw_button(
            center_x - 150.0,
            center_y + 8.0,
            300.0,
            48.0,
            "SPAWN GRENADE",
            Color::from_rgba(65, 145, 80, 255),
        );
        self.draw_button(
            center_x - 150.0,
            center_y + 66.0,
            300.0,
            48.0,
            "SPAWN BARREL",
            Color::from_rgba(170, 74, 48, 255),
        );

        draw_text(
            "Q: box    G: grenade    B: barrel",
            center_x - 145.0,
            center_y + 155.0,
            16.0,
            GRAY,
        );
        draw_text(
            "Escape: return to menu",
            center_x - 105.0,
            center_y + 180.0,
            16.0,
            GRAY,
        );
    }

    fn draw_props(&self, state: &GameState) {
        for prop in &state.physics.props {
            if !prop.active {
                continue;
            }

            if let Some(position) = state.physics.body_position(prop.body) {
                let color = Color::from_rgba(
                    prop.color[0],
                    prop.color[1],
                    prop.color[2],
                    prop.color[3],
                );

                self.draw_rotated_rectangle(
                    vec2(position.translation.x, position.translation.y),
                    prop.half_width,
                    prop.half_height,
                    position.rotation.angle(),
                    color,
                );

                if prop.kind == PropKind::Grenade {
                    draw_circle(position.translation.x, position.translation.y, 5.0, YELLOW);
                }
            }
        }
    }

    fn draw_explosions(&self, state: &GameState) {
        for explosion in &state.physics.explosions {
            let alpha = (explosion.life / 0.35).clamp(0.0, 1.0);

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
            draw_circle(head.translation.x - 9.0, head.translation.y - 5.0, 4.0, DARKGRAY);
            draw_circle(head.translation.x + 9.0, head.translation.y - 5.0, 4.0, DARKGRAY);
        }
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
        let sin = angle.sin();
        let cos = angle.cos();

        let points: Vec<Vec2> = corners
            .iter()
            .map(|point| {
                vec2(
                    center.x + point.x * cos - point.y * sin,
                    center.y + point.x * sin + point.y * cos,
                )
            })
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

        draw_rectangle(0.0, 0.0, screen_width(), 650.0, color);

        match map {
            MapKind::TestRange => {
                for x in (0..1280).step_by(80) {
                    draw_line(
                        x as f32,
                        0.0,
                        x as f32,
                        650.0,
                        1.0,
                        Color::from_rgba(55, 67, 80, 180),
                    );
                }
            }
            MapKind::City => {
                draw_rectangle(
                    0.0,
                    400.0,
                    1280.0,
                    250.0,
                    Color::from_rgba(48, 52, 58, 255),
                );

                for x in (0..1280).step_by(180) {
                    draw_rectangle(
                        x as f32,
                        180.0,
                        110.0,
                        220.0,
                        Color::from_rgba(67, 76, 91, 255),
                    );
                }
            }
            MapKind::Laboratory => {
                draw_circle(
                    270.0,
                    620.0,
                    70.0,
                    Color::from_rgba(45, 190, 104, 170),
                );
                draw_circle(
                    930.0,
                    620.0,
                    85.0,
                    Color::from_rgba(45, 190, 104, 170),
                );
                draw_line(
                    390.0,
                    120.0,
                    390.0,
                    500.0,
                    8.0,
                    Color::from_rgba(235, 55, 75, 220),
                );
                draw_line(
                    890.0,
                    120.0,
                    890.0,
                    500.0,
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

    fn draw_touch_controls(&self) {
        draw_circle(
            100.0,
            screen_height() - 115.0,
            66.0,
            Color::from_rgba(30, 38, 47, 180),
        );
        draw_circle(
            100.0,
            screen_height() - 115.0,
            29.0,
            Color::from_rgba(122, 137, 146, 210),
        );

        let x = screen_width() - 100.0;
        let y = screen_height() - 118.0;

        draw_circle(x, y, 42.0, Color::from_rgba(183, 76, 62, 220));
        draw_text("F", x - 9.0, y + 9.0, 25.0, WHITE);

        draw_circle(
            x - 92.0,
            y + 28.0,
            30.0,
            Color::from_rgba(65, 91, 120, 220),
        );
        draw_text("+", x - 101.0, y + 37.0, 26.0, WHITE);
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