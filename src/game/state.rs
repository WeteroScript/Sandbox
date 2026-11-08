use macroquad::prelude::Vec2;

use super::physics::{CharacterKind, MapKind, PhysicsWorld};

#[path = "bounds.rs"]
mod bounds;

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
        self.center.x = self.center.x.clamp(160.0, 1120.0);
        self.center.y = self.center.y.clamp(120.0, 580.0);
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
pub enum SpawnItem {
    Pistol,
    SilencedPistol,
    HeavyPistol,
    Rifle,
    Shotgun,
    Bat,
    Katana,
    Melon,
    Robot,
    MelonHead,
    Box,
    Grenade,
    Barrel,
    Platform,
    Rod,
    Rope,
    Spring,
    Piston,
    Button,
    Toggle,
}

impl SpawnItem {
    pub fn name(self) -> &'static str {
        match self {
            Self::Pistol => "PISTOL",
            Self::SilencedPistol => "SILENCED PISTOL",
            Self::HeavyPistol => "HEAVY PISTOL",
            Self::Rifle => "RIFLE",
            Self::Shotgun => "SHOTGUN",
            Self::Bat => "BAT",
            Self::Katana => "KATANA",
            Self::Melon => "MELON",
            Self::Robot => "ROBOT",
            Self::MelonHead => "MELON HEAD",
            Self::Box => "BOX",
            Self::Grenade => "GRENADE",
            Self::Barrel => "BARREL",
            Self::Platform => "PLATFORM",
            Self::Rod => "ROD",
            Self::Rope => "ROPE",
            Self::Spring => "SPRING",
            Self::Piston => "PISTON",
            Self::Button => "BUTTON",
            Self::Toggle => "TOGGLE",
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
            Self::Mechanisms => &[
                "BOX",
                "GRENADE",
                "BARREL",
                "PLATFORM",
                "ROD",
                "ROPE",
                "SPRING",
                "PISTON",
                "BUTTON",
                "TOGGLE",
            ],
        }
    }

    pub fn item(self, index: usize) -> Option<SpawnItem> {
        match self {
            Self::Pistols => [
                SpawnItem::Pistol,
                SpawnItem::SilencedPistol,
                SpawnItem::HeavyPistol,
            ]
            .get(index)
            .copied(),
            Self::Weapons => [
                SpawnItem::Rifle,
                SpawnItem::Shotgun,
                SpawnItem::Bat,
                SpawnItem::Katana,
            ]
            .get(index)
            .copied(),
            Self::Melons => [
                SpawnItem::Melon,
                SpawnItem::Robot,
                SpawnItem::MelonHead,
            ]
            .get(index)
            .copied(),
            Self::Mechanisms => [
                SpawnItem::Box,
                SpawnItem::Grenade,
                SpawnItem::Barrel,
                SpawnItem::Platform,
                SpawnItem::Rod,
                SpawnItem::Rope,
                SpawnItem::Spring,
                SpawnItem::Piston,
                SpawnItem::Button,
                SpawnItem::Toggle,
            ]
            .get(index)
            .copied(),
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
    pub pending_spawn: Option<SpawnItem>,
    pub spawn_count: u32,
    pub spawn_placing: bool,
}

impl GameState {
    pub fn new() -> Self {
        let character = CharacterKind::Melon;
        let map = MapKind::TestRange;
        let mut physics = PhysicsWorld::new(character, map);
        bounds::install_bounds(&mut physics);
        bounds::install_environment(&mut physics, map);

        Self {
            mode: GameMode::MainMenu,
            elapsed: 0.0,
            character,
            map,
            camera: CameraState::new(),
            physics,
            spawn_menu_open: true,
            spawn_category: SpawnCategory::Pistols,
            pending_spawn: None,
            spawn_count: 1,
            spawn_placing: false,
        }
    }

    pub fn rebuild_world(&mut self) {
        self.physics = PhysicsWorld::new(self.character, self.map);
        bounds::install_bounds(&mut self.physics);
        bounds::install_environment(&mut self.physics, self.map);
        self.camera = CameraState::new();
        self.elapsed = 0.0;
        self.pending_spawn = None;
        self.spawn_count = 1;
        self.spawn_placing = false;
    }
}