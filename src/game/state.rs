use super::physics::{CharacterKind, MapKind, PhysicsWorld};

pub use super::physics::{CharacterKind as SelectedCharacter, MapKind as SelectedMap};

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
    pub character: CharacterKind,
    pub map: MapKind,
    pub physics: PhysicsWorld,
}

impl GameState {
    pub fn new() -> Self {
        let character = CharacterKind::Melon;
        let map = MapKind::TestRange;

        Self {
            mode: GameMode::MainMenu,
            elapsed: 0.0,
            character,
            map,
            physics: PhysicsWorld::new(character, map),
        }
    }

    pub fn rebuild_world(&mut self) {
        self.physics = PhysicsWorld::new(self.character, self.map);
        self.elapsed = 0.0;
    }
}