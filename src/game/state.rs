use macroquad::prelude::Vec2;

use super::physics::{CharacterKind, MapKind, PhysicsWorld};

pub struct CameraState {
    pub center: Vec2,
    pub zoom: f32,
}

impl CameraState {
    pub fn new() -> Self {
        Self {
            center: Vec2::new(640.0, 360.0),
            zoom: 1.0,
        }
    }

    pub fn apply_gesture(&mut self, pan: Vec2, zoom_delta: f32) {
        self.center -= pan / self.zoom.max(0.1);
        self.zoom = (self.zoom * (1.0 + zoom_delta)).clamp(0.55, 2.8);
        self.center.x = self.center.x.clamp(320.0, 960.0);
        self.center.y = self.center.y.clamp(180.0, 540.0);
    }

    pub fn macroquad_camera(&self) -> macroquad::camera::Camera2D {
        macroquad::camera::Camera2D {
            target: self.center,
            zoom: macroquad::prelude::vec2(
                2.0 * self.zoom / macroquad::prelude::screen_width(),
                -2.0 * self.zoom / macroquad::prelude::screen_height(),
            ),
            ..Default::default()
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum SpawnCategory {
    Pistols,
    Weapons,
    Melons,
    Mechanisms,
}

impl SpawnCategory {
    pub fn next(self) -> Self {
        match self {
            Self::Pistols => Self::Weapons,
            Self::Weapons => Self::Melons,
            Self::Melons => Self::Mechanisms,
            Self::Mechanisms => Self::Pistols,
        }
    }

    pub fn title(self) -> &'static str {
        match self {
            Self::Pistols => "PISTOLS",
            Self::Weapons => "WEAPONS",
            Self::Melons => "MELONS",
            Self::Mechanisms => "MECHANISMS",
        }
    }

    pub fn items(self) -> &'static [&'static str] {
        match self {
            Self::Pistols => &["PISTOL", "SILENCED PISTOL", "HEAVY PISTOL"],
            Self::Weapons => &["RIFLE", "SHOTGUN", "BAT", "KATANA"],
            Self::Melons => &["MELON", "ROBOT", "MELON HEAD"],
            Self::Mechanisms => &["BOX", "GRENADE", "BARREL", "PLATFORM"],
        }
    }
}

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
    pub camera: CameraState,
    pub physics: PhysicsWorld,
    pub spawn_menu_open: bool,
    pub spawn_category: SpawnCategory,
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
            camera: CameraState::new(),
            physics: PhysicsWorld::new(character, map),
            spawn_menu_open: true,
            spawn_category: SpawnCategory::Pistols,
        }
    }

    pub fn rebuild_world(&mut self) {
        self.physics = PhysicsWorld::new(self.character, self.map);
        self.camera = CameraState::new();
        self.elapsed = 0.0;
    }
}