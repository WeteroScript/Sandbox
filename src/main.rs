mod game;

use game::Game;
use macroquad::prelude::*;

#[macroquad::main("Sandbox")]
async fn main() {
    let mut game = Game::new();

    loop {
        game.update();
        game.render();

        draw_rectangle(
            14.0,
            72.0,
            86.0,
            28.0,
            Color::from_rgba(20, 26, 34, 210),
        );
        draw_text(
            &format!("FPS {}", get_fps()),
            24.0,
            92.0,
            18.0,
            Color::from_rgba(230, 236, 238, 255),
        );

        next_frame().await;
    }
}