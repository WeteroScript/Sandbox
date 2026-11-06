use super::physics::PhysicsWorld;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum GameMode {
    MainMenu,
    Playing,
    Settings,
    Paused,
}

pub struct GameState {
    pub mode: GameMode,
    pub elapsed: f32,
    pub physics: PhysicsWorld,
}

impl GameState {
    pub fn new() -> Self {
        Self {
            mode: GameMode::MainMenu,
            elapsed: 0.0,
            physics: PhysicsWorld::new(),
        }
    }
}