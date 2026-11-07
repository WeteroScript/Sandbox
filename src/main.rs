macro_rules! vector {
    ($($tokens:tt)*) => {
        rapier2d::prelude::vector![$($tokens)*]
    };
}

mod game;

use game::Game;

#[macroquad::main("Sandbox")]
async fn main() {
    let mut game = Game::new();

    loop {
        game.update();
        game.render();
        macroquad::prelude::next_frame().await;
    }
}