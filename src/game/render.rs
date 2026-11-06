use macroquad::prelude::*;

use super::state::GameState;

pub struct Renderer;

impl Renderer {
    pub fn new() -> Self {
        Self
    }

    pub fn draw_main_menu(&self) {
        self.draw_header("SANDBOX");

        let center_x = screen_width() * 0.5;
        let start_y = screen_height() * 0.5 - 70.0;

        self.draw_button(
            center_x - 150.0,
            start_y,
            300.0,
            58.0,
            "PLAY",
            Color::from_rgba(52, 168, 99, 255),
        );

        self.draw_button(
            center_x - 150.0,
            start_y + 76.0,
            300.0,
            58.0,
            "SETTINGS",
            Color::from_rgba(65, 91, 120, 255),
        );

        draw_text(
            "Enter or left click to play",
            center_x - 130.0,
            screen_height() - 46.0,
            18.0,
            GRAY,
        );
    }

    pub fn draw_playing(&self, state: &GameState) {
        let width = screen_width();
        let height = screen_height();

        draw_rectangle(
            0.0,
            height * 0.72,
            width,
            height * 0.28,
            Color::from_rgba(67, 75, 81, 255),
        );

        draw_rectangle(
            0.0,
            height * 0.72 - 6.0,
            width,
            6.0,
            Color::from_rgba(118, 128, 121, 255),
        );

        let x = width * 0.5;
        let y = height * 0.52;

        draw_circle(x, y, 34.0, Color::from_rgba(106, 175, 92, 255));
        draw_rectangle(
            x - 22.0,
            y + 30.0,
            44.0,
            90.0,
            Color::from_rgba(82, 142, 76, 255),
        );

        draw_line(
            x - 22.0,
            y + 48.0,
            x - 72.0,
            y + 92.0,
            12.0,
            Color::from_rgba(82, 142, 76, 255),
        );
        draw_line(
            x + 22.0,
            y + 48.0,
            x + 72.0,
            y + 92.0,
            12.0,
            Color::from_rgba(82, 142, 76, 255),
        );
        draw_line(
            x - 12.0,
            y + 120.0,
            x - 45.0,
            height * 0.72,
            14.0,
            Color::from_rgba(82, 142, 76, 255),
        );
        draw_line(
            x + 12.0,
            y + 120.0,
            x + 45.0,
            height * 0.72,
            14.0,
            Color::from_rgba(82, 142, 76, 255),
        );

        draw_circle(x - 11.0, y - 5.0, 4.0, DARKGRAY);
        draw_circle(x + 11.0, y - 5.0, 4.0, DARKGRAY);

        self.draw_top_bar();
        self.draw_touch_controls();

        draw_text(
            &format!("Prototype time: {:.1}", state.elapsed),
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

    pub fn draw_paused(&self) {
        draw_rectangle(
            0.0,
            0.0,
            screen_width(),
            screen_height(),
            Color::from_rgba(5, 8, 12, 230),
        );

        let center_x = screen_width() * 0.5;
        let center_y = screen_height() * 0.5;

        draw_text("PAUSED", center_x - 70.0, center_y - 80.0, 42.0, WHITE);

        self.draw_button(
            center_x - 140.0,
            center_y - 35.0,
            280.0,
            58.0,
            "RESUME",
            Color::from_rgba(52, 168, 99, 255),
        );
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

    fn draw_top_bar(&self) {
        draw_rectangle(
            0.0,
            0.0,
            screen_width(),
            62.0,
            Color::from_rgba(28, 35, 45, 235),
        );

        draw_text("TEST RANGE", 24.0, 39.0, 22.0, WHITE);
        draw_text(
            "P / ESC: pause",
            screen_width() - 150.0,
            39.0,
            16.0,
            LIGHTGRAY,
        );
    }

    fn draw_touch_controls(&self) {
        let joystick_x = 100.0;
        let joystick_y = screen_height() - 115.0;

        draw_circle(
            joystick_x,
            joystick_y,
            66.0,
            Color::from_rgba(30, 38, 47, 180),
        );
        draw_circle(
            joystick_x,
            joystick_y,
            29.0,
            Color::from_rgba(122, 137, 146, 210),
        );

        let action_x = screen_width() - 100.0;
        let action_y = screen_height() - 118.0;

        draw_circle(
            action_x,
            action_y,
            42.0,
            Color::from_rgba(183, 76, 62, 220),
        );
        draw_text("A", action_x - 10.0, action_y + 9.0, 26.0, WHITE);

        draw_circle(
            action_x - 92.0,
            action_y + 28.0,
            30.0,
            Color::from_rgba(65, 91, 120, 220),
        );
        draw_text("+", action_x - 101.0, action_y + 37.0, 26.0, WHITE);
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

        let text_width = measure_text(label, None, 26, 1.0).width;

        draw_text(
            label,
            x + (width - text_width) * 0.5,
            y + 38.0,
            26.0,
            WHITE,
        );
    }
}