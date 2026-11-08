use macroquad::prelude::Vec2;
use rapier2d::prelude::*;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CharacterKind {
    Melon,
    Robot,
}

impl CharacterKind {
    pub fn next(self) -> Self {
        match self {
            Self::Melon => Self::Robot,
            Self::Robot => Self::Melon,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Melon => "MELON",
            Self::Robot => "ROBOT",
        }
    }

    pub fn colors(self) -> ([u8; 4], [u8; 4]) {
        match self {
            Self::Melon => ([106, 175, 92, 255], [72, 128, 70, 255]),
            Self::Robot => ([156, 166, 176, 255], [92, 104, 118, 255]),
        }
    }

    fn joint_strength(self) -> f32 {
        match self {
            Self::Melon => 700.0,
            Self::Robot => 950.0,
        }
    }

    fn part_density(self) -> f32 {
        match self {
            Self::Melon => 3.6,
            Self::Robot => 5.2,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MapKind {
    TestRange,
    City,
    Laboratory,
    Lava,
    Water,
}

impl MapKind {
    pub fn next(self) -> Self {
        match self {
            Self::TestRange => Self::City,
            Self::City => Self::Laboratory,
            Self::Laboratory => Self::Lava,
            Self::Lava => Self::Water,
            Self::Water => Self::TestRange,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::TestRange => "TEST RANGE",
            Self::City => "CITY",
            Self::Laboratory => "LABORATORY",
            Self::Lava => "LAVA PIT",
            Self::Water => "WATER PARK",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WeaponKind {
    Pistol,
    SilencedPistol,
    HeavyPistol,
    Rifle,
    Shotgun,
    Bat,
    Katana,
}

impl WeaponKind {
    pub fn next(self) -> Self {
        match self {
            Self::Pistol => Self::SilencedPistol,
            Self::SilencedPistol => Self::HeavyPistol,
            Self::HeavyPistol => Self::Rifle,
            Self::Rifle => Self::Shotgun,
            Self::Shotgun => Self::Bat,
            Self::Bat => Self::Katana,
            Self::Katana => Self::Pistol,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Pistol => "PISTOL",
            Self::SilencedPistol => "SILENCED PISTOL",
            Self::HeavyPistol => "HEAVY PISTOL",
            Self::Rifle => "RIFLE",
            Self::Shotgun => "SHOTGUN",
            Self::Bat => "BAT",
            Self::Katana => "KATANA",
        }
    }
}

#[derive(Clone, Copy)]
pub struct Ragdoll {
    pub head: RigidBodyHandle,
    pub torso: RigidBodyHandle,
    pub left_upper_arm: RigidBodyHandle,
    pub right_upper_arm: RigidBodyHandle,
    pub left_lower_arm: RigidBodyHandle,
    pub right_lower_arm: RigidBodyHandle,
    pub left_upper_leg: RigidBodyHandle,
    pub right_upper_leg: RigidBodyHandle,
    pub left_lower_leg: RigidBodyHandle,
    pub right_lower_leg: RigidBodyHandle,
}

struct JointHealth {
    handle: ImpulseJointHandle,
    parent: RigidBodyHandle,
    child: RigidBodyHandle,
    health: f32,
    max_health: f32,
}

#[derive(Clone)]
pub struct Prop {
    pub body: RigidBodyHandle,
    pub half_width: f32,
    pub half_height: f32,
    pub color: [u8; 4],
    pub kind: PropKind,
    pub active: bool,
    pub timer: f32,
    pub durability: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum PropKind {
    Static,
    Box,
    Platform,
    Weapon,
    Character,
    Grenade,
    Barrel,
    Projectile,
}

pub struct Explosion {
    pub position: Vec2,
    pub radius: f32,
    pub life: f32,
}

pub struct PhysicsWorld {
    pub pipeline: PhysicsPipeline,
    pub gravity: Vector<Real>,
    pub integration_parameters: IntegrationParameters,
    pub islands: IslandManager,
    pub broad_phase: DefaultBroadPhase,
    pub narrow_phase: NarrowPhase,
    pub bodies: RigidBodySet,
    pub colliders: ColliderSet,
    pub impulse_joints: ImpulseJointSet,
    pub multibody_joints: MultibodyJointSet,
    pub ccd_solver: CCDSolver,
    pub query_pipeline: QueryPipeline,
    pub ragdoll: Ragdoll,
    pub spawned_ragdolls: Vec<(Ragdoll, CharacterKind)>,
    pub frozen_bodies: Vec<RigidBodyHandle>,
    pub props: Vec<Prop>,
    pub explosions: Vec<Explosion>,
    pub character: CharacterKind,
    pub map: MapKind,
    pub weapon: WeaponKind,
    joints: Vec<JointHealth>,
    total_joint_capacity: f32,
    detached_parts: Vec<RigidBodyHandle>,
    spawn_index: u32,
}

impl PhysicsWorld {
    pub fn new(character: CharacterKind, map: MapKind) -> Self {
        let mut world = Self {
            pipeline: PhysicsPipeline::new(),
            gravity: vector![0.0, 980.0],
            integration_parameters: IntegrationParameters {
                dt: 1.0 / 60.0,
                ..IntegrationParameters::default()
            },
            islands: IslandManager::new(),
            broad_phase: DefaultBroadPhase::new(),
            narrow_phase: NarrowPhase::new(),
            bodies: RigidBodySet::new(),
            colliders: ColliderSet::new(),
            impulse_joints: ImpulseJointSet::new(),
            multibody_joints: MultibodyJointSet::new(),
            ccd_solver: CCDSolver::new(),
            query_pipeline: QueryPipeline::new(),
            ragdoll: Self::empty_ragdoll(),
            spawned_ragdolls: Vec::new(),
            frozen_bodies: Vec::new(),
            props: Vec::new(),
            explosions: Vec::new(),
            character,
            map,
            weapon: WeaponKind::Pistol,
            joints: Vec::new(),
            total_joint_capacity: 0.0,
            detached_parts: Vec::new(),
            spawn_index: 0,
        };

        world.create_floor();
        world.create_map_geometry();
        world.create_ragdoll(vector![1200.0, 380.0]);
        world
    }

    pub fn step(&mut self, delta_time: f32) {
        let dt = delta_time.clamp(1.0 / 120.0, 1.0 / 30.0);
        self.integration_parameters.dt = dt;

        self.pipeline.step(
            &self.gravity,
            &self.integration_parameters,
            &mut self.islands,
            &mut self.broad_phase,
            &mut self.narrow_phase,
            &mut self.bodies,
            &mut self.colliders,
            &mut self.impulse_joints,
            &mut self.multibody_joints,
            &mut self.ccd_solver,
            Some(&mut self.query_pipeline),
            &(),
            &(),
        );

        self.update_joint_damage(dt);
        self.update_map_effects(dt);
        self.update_props(dt);

        for explosion in &mut self.explosions {
            explosion.life -= dt;
        }

        self.explosions.retain(|explosion| explosion.life > 0.0);
    }

    pub fn body_position(&self, handle: RigidBodyHandle) -> Option<Isometry<Real>> {
        self.bodies.get(handle).map(|body| *body.position())
    }

    pub fn move_ragdoll(&mut self, direction: Vec2) {
        if direction.length_squared() <= 0.01 {
            return;
        }

        let force = match self.character {
            CharacterKind::Melon => 620.0,
            CharacterKind::Robot => 820.0,
        };

        if let Some(torso) = self.bodies.get_mut(self.ragdoll.torso) {
            torso.add_force(
                vector![direction.x * force, direction.y * force * 0.16],
                true,
            );
        }
    }

    pub fn cycle_weapon(&mut self) {
        self.weapon = self.weapon.next();
    }

    pub fn fire_weapon(&mut self) {
        let origin = self
            .body_position(self.ragdoll.torso)
            .map(|position| vector![position.translation.x + 55.0, position.translation.y])
            .unwrap_or(vector![640.0, 300.0]);

        match self.weapon {
            WeaponKind::Pistol => self.spawn_projectile(origin, 1200.0, 8.0),
            WeaponKind::SilencedPistol => self.spawn_projectile(origin, 1350.0, 7.0),
            WeaponKind::HeavyPistol => self.spawn_projectile(origin, 1050.0, 18.0),
            WeaponKind::Rifle => self.spawn_projectile(origin, 1800.0, 12.0),
            WeaponKind::Shotgun => {
                for offset in [-0.18_f32, -0.09, 0.0, 0.09, 0.18] {
                    self.spawn_projectile_with_angle(origin, 1250.0, 7.0, offset);
                }
            }
            WeaponKind::Bat => self.apply_melee_impulse(origin, 100.0, 700.0),
            WeaponKind::Katana => self.apply_melee_impulse(origin, 130.0, 1100.0),
        }
    }

    pub fn spawn_weapon(&mut self, position: Vector<Real>, weapon: WeaponKind) {
        let (half_size, color) = match weapon {
            WeaponKind::Pistol => (vector![28.0, 8.0], [120, 128, 140, 255]),
            WeaponKind::SilencedPistol => (vector![34.0, 7.0], [82, 91, 104, 255]),
            WeaponKind::HeavyPistol => (vector![30.0, 11.0], [165, 125, 76, 255]),
            WeaponKind::Rifle => (vector![48.0, 7.0], [76, 91, 108, 255]),
            WeaponKind::Shotgun => (vector![46.0, 10.0], [128, 92, 63, 255]),
            WeaponKind::Bat => (vector![42.0, 8.0], [145, 86, 49, 255]),
            WeaponKind::Katana => (vector![58.0, 4.0], [185, 195, 202, 255]),
        };

        self.spawn_special(
            position,
            half_size,
            PropKind::Weapon,
            color,
            -1.0,
            f32::INFINITY,
        );
    }

    pub fn spawn_character_prop(
        &mut self,
        position: Vector<Real>,
        character: CharacterKind,
    ) {
        let player_character = self.character;
        let player_ragdoll = self.ragdoll;

        self.character = character;
        self.create_ragdoll(position);
        let spawned = self.ragdoll;
        self.spawned_ragdolls.push((spawned, character));

        self.character = player_character;
        self.ragdoll = player_ragdoll;
    }

    pub fn spawn_melon_head(&mut self, position: Vector<Real>) {
        self.spawn_special(
            position,
            vector![27.0, 27.0],
            PropKind::Character,
            [106, 175, 92, 255],
            -1.0,
            1.5,
        );
    }

    pub fn spawn_platform(&mut self, position: Vector<Real>) {
        self.spawn_special(
            position,
            vector![90.0, 14.0],
            PropKind::Platform,
            [110, 123, 135, 255],
            -1.0,
            f32::INFINITY,
        );
    }

    pub fn spawn_grenade(&mut self, position: Vector<Real>) {
        self.spawn_special(
            position,
            vector![14.0, 14.0],
            PropKind::Grenade,
            [65, 175, 85, 255],
            3.0,
            1.0,
        );
    }

    pub fn spawn_barrel(&mut self, position: Vector<Real>) {
        self.spawn_special(
            position,
            vector![28.0, 38.0],
            PropKind::Barrel,
            [170, 74, 48, 255],
            -1.0,
            4.0,
        );
    }

    pub fn spawn_box(&mut self, position: Vector<Real>, half_size: Vector<Real>) {
        self.spawn_special(
            position,
            half_size,
            PropKind::Box,
            [156, 101, 58, 255],
            -1.0,
            3.0,
        );
    }

    pub fn apply_test_damage(&mut self) {
        for joint in &mut self.joints {
            joint.health -= joint.max_health * 0.08;
        }

        self.remove_broken_joints();
    }

    pub fn joint_health_ratio(&self) -> f32 {
        if self.total_joint_capacity <= 0.0 {
            return 0.0;
        }

        let current: f32 = self.joints.iter().map(|joint| joint.health.max(0.0)).sum();
        (current / self.total_joint_capacity).clamp(0.0, 1.0)
    }

    pub fn is_detached(&self, handle: RigidBodyHandle) -> bool {
        self.detached_parts.contains(&handle)
    }

    pub fn is_frozen(&self, handle: RigidBodyHandle) -> bool {
        self.frozen_bodies.contains(&handle)
    }

    pub fn delete_prop_at(&mut self, position: Vec2) -> bool {
        let selected = self
            .props
            .iter()
            .filter(|prop| prop.active && prop.kind != PropKind::Static)
            .filter_map(|prop| {
                let body_position = self.body_position(prop.body)?;
                let center = Vec2::new(
                    body_position.translation.x,
                    body_position.translation.y,
                );
                let distance = center.distance(position);
                let hit_radius = prop.half_width.max(prop.half_height) + 30.0;
                (distance <= hit_radius).then_some((prop.body, distance))
            })
            .min_by(|left, right| left.1.total_cmp(&right.1))
            .map(|(handle, _)| handle);

        let Some(handle) = selected else {
            return false;
        };

        self.bodies.remove(
            handle,
            &mut self.islands,
            &mut self.colliders,
            &mut self.impulse_joints,
            &mut self.multibody_joints,
            true,
        );

        self.props.retain(|prop| prop.body != handle);
        self.frozen_bodies.retain(|frozen| *frozen != handle);
        true
    }

    pub fn toggle_freeze(&mut self, handle: RigidBodyHandle) {
        let was_frozen = self.is_frozen(handle);
        let Some(body) = self.bodies.get_mut(handle) else {
            return;
        };

        if was_frozen {
            body.set_body_type(RigidBodyType::Dynamic, true);
            self.frozen_bodies.retain(|frozen| *frozen != handle);
        } else if body.is_dynamic() {
            body.set_linvel(vector![0.0, 0.0], true);
            body.set_angvel(0.0, true);
            body.set_body_type(RigidBodyType::Fixed, true);
            self.frozen_bodies.push(handle);
        }
    }

    pub fn duplicate_body(&mut self, handle: RigidBodyHandle) -> Option<RigidBodyHandle> {
        let source_position = self.body_position(handle)?;
        let source_velocity = self.bodies.get(handle).map(|body| *body.linvel())?;

        let character = if Self::ragdoll_has_body(&self.ragdoll, handle) {
            Some(self.character)
        } else {
            self.spawned_ragdolls
                .iter()
                .find(|(ragdoll, _)| Self::ragdoll_has_body(ragdoll, handle))
                .map(|(_, character)| *character)
        };

        let side = if source_position.translation.x < 1800.0 { 1.0 } else { -1.0 };
        if let Some(character) = character {
            let position = vector![
                (source_position.translation.x + side * 170.0).clamp(90.0, 2310.0),
                (source_position.translation.y - 35.0).clamp(45.0, 1320.0)
            ];
            self.spawn_character_prop(position, character);
            return self.spawned_ragdolls.last().map(|(ragdoll, _)| ragdoll.head);
        }

        let (kind, half_width, half_height, color, timer, durability) = self
            .props
            .iter()
            .find(|prop| prop.body == handle && prop.active)
            .map(|prop| (
                prop.kind,
                prop.half_width,
                prop.half_height,
                prop.color,
                prop.timer,
                prop.durability,
            ))?;

        if matches!(kind, PropKind::Static | PropKind::Projectile) {
            return None;
        }

        let position = vector![
            (source_position.translation.x + side * (half_width.max(24.0) * 2.0 + 24.0))
                .clamp(55.0, 2345.0),
            (source_position.translation.y - 18.0).clamp(35.0, 1390.0)
        ];
        self.spawn_special(
            position,
            vector![half_width, half_height],
            kind,
            color,
            timer,
            durability,
        );

        let duplicate = self.props.last()?.body;
        if let Some(body) = self.bodies.get_mut(duplicate) {
            body.set_linvel(source_velocity * 0.2, true);
        }
        Some(duplicate)
    }

    fn ragdoll_has_body(ragdoll: &Ragdoll, handle: RigidBodyHandle) -> bool {
        [
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
        ]
        .contains(&handle)
    }

    fn spawn_special(
        &mut self,
        position: Vector<Real>,
        half_size: Vector<Real>,
        kind: PropKind,
        color: [u8; 4],
        timer: f32,
        durability: f32,
    ) {
        let body = RigidBodyBuilder::dynamic()
            .translation(position)
            .rotation((self.spawn_index as f32 * 0.17) % 0.4)
            .linear_damping(0.28)
            .angular_damping(0.42)
            .ccd_enabled(true)
            .build();

        let body_handle = self.bodies.insert(body);

        let collider = if kind == PropKind::Character {
            ColliderBuilder::ball(half_size.x)
        } else {
            ColliderBuilder::cuboid(half_size.x, half_size.y)
        }
        .density(match kind {
            PropKind::Weapon => 0.55,
            PropKind::Character => 1.15,
            PropKind::Barrel => 1.5,
            _ => 0.9,
        })
        .friction(0.82)
        .restitution(0.08)
        .build();

        self.colliders
            .insert_with_parent(collider, body_handle, &mut self.bodies);

        self.props.push(Prop {
            body: body_handle,
            half_width: half_size.x,
            half_height: half_size.y,
            color,
            kind,
            active: true,
            timer,
            durability,
        });

        self.spawn_index = self.spawn_index.wrapping_add(1);
    }

    fn spawn_projectile(&mut self, position: Vector<Real>, speed: f32, damage: f32) {
        self.spawn_projectile_with_angle(position, speed, damage, 0.0);
    }

    fn spawn_projectile_with_angle(
        &mut self,
        position: Vector<Real>,
        speed: f32,
        damage: f32,
        angle: f32,
    ) {
        let body = RigidBodyBuilder::dynamic()
            .translation(position)
            .linvel(vector![speed * angle.cos(), speed * angle.sin()])
            .gravity_scale(0.0)
            .ccd_enabled(true)
            .build();

        let handle = self.bodies.insert(body);
        let collider = ColliderBuilder::ball(5.0)
            .density(0.2)
            .sensor(true)
            .build();

        self.colliders
            .insert_with_parent(collider, handle, &mut self.bodies);

        self.props.push(Prop {
            body: handle,
            half_width: damage * 0.35,
            half_height: 5.0,
            color: [230, 210, 80, 255],
            kind: PropKind::Projectile,
            active: true,
            timer: 1.2,
            durability: damage,
        });
    }

    fn apply_melee_impulse(&mut self, origin: Vector<Real>, radius: f32, impulse: f32) {
        for (_, body) in self.bodies.iter_mut() {
            if !body.is_dynamic() {
                continue;
            }

            let offset = body.translation() - origin;
            let distance = offset.norm();

            if distance < radius {
                let direction = if distance > 0.1 {
                    offset / distance
                } else {
                    vector![1.0, 0.0]
                };

                body.apply_impulse(direction * impulse, true);
            }
        }
    }

    fn update_props(&mut self, delta_time: f32) {
        let mut detonations = Vec::new();

        for index in 0..self.props.len() {
            if !self.props[index].active {
                continue;
            }

            if self.props[index].timer > 0.0 {
                self.props[index].timer -= delta_time;

                if self.props[index].timer <= 0.0
                    && self.props[index].kind == PropKind::Grenade
                {
                    if let Some(position) = self.body_position(self.props[index].body) {
                        detonations.push(vector![
                            position.translation.x,
                            position.translation.y
                        ]);
                    }

                    self.props[index].active = false;
                }
            }

            if self.props[index].kind == PropKind::Projectile
                && self.props[index].timer <= 0.0
            {
                self.props[index].active = false;
            }
        }

        let mut projectile_hits = Vec::new();

        for projectile_index in 0..self.props.len() {
            let projectile = &self.props[projectile_index];

            if !projectile.active || projectile.kind != PropKind::Projectile {
                continue;
            }

            let Some(projectile_position) = self.body_position(projectile.body) else {
                continue;
            };

            let projectile_position = vector![
                projectile_position.translation.x,
                projectile_position.translation.y
            ];

            for target_index in 0..self.props.len() {
                let target = &self.props[target_index];

                if !target.active
                    || !matches!(
                        target.kind,
                        PropKind::Box | PropKind::Barrel | PropKind::Character
                    )
                    || target_index == projectile_index
                {
                    continue;
                }

                let Some(target_position) = self.body_position(target.body) else {
                    continue;
                };

                let target_position =
                    vector![target_position.translation.x, target_position.translation.y];
                let hit_radius = target.half_width.max(target.half_height) + 12.0;

                if (target_position - projectile_position).norm() <= hit_radius {
                    projectile_hits.push((projectile_index, target_index));
                    break;
                }
            }
        }

        for (projectile_index, target_index) in projectile_hits {
            let damage = self
                .props
                .get(projectile_index)
                .map(|projectile| projectile.durability)
                .unwrap_or(8.0);

            if let Some(projectile) = self.props.get_mut(projectile_index) {
                projectile.active = false;
            }

            let target_info = self
                .props
                .get(target_index)
                .map(|target| (target.active, target.kind, target.body));

            let barrel_position = target_info.and_then(|(active, kind, body)| {
                if active && kind == PropKind::Barrel {
                    self.body_position(body)
                        .map(|position| vector![position.translation.x, position.translation.y])
                } else {
                    None
                }
            });

            let mut detonation = None;

            if let Some(target) = self.props.get_mut(target_index) {
                target.durability -= damage;

                if target.durability <= 0.0 {
                    target.active = false;

                    if target.kind == PropKind::Barrel {
                        detonation = barrel_position;
                    }
                }
            }

            if let Some(position) = detonation {
                detonations.push(position);
            }
        }

        for position in detonations {
            self.explode(
                macroquad::prelude::vec2(position.x, position.y),
                240.0,
                1150.0,
            );
        }
    }

    fn explode(&mut self, position: Vec2, radius: f32, impulse: f32) {
        let center = vector![position.x, position.y];
        let mut chain_detonations = Vec::new();

        for (_, body) in self.bodies.iter_mut() {
            if !body.is_dynamic() {
                continue;
            }

            let offset = body.translation() - center;
            let distance = offset.norm();

            if distance < radius {
                let falloff = 1.0 - distance / radius;
                let direction = if distance > 0.1 {
                    offset / distance
                } else {
                    vector![0.0, -1.0]
                };

                body.apply_impulse(direction * impulse * falloff, true);
            }
        }

        let affected_props: Vec<(usize, Vec2)> = self
            .props
            .iter()
            .enumerate()
            .filter_map(|(index, prop)| {
                if !prop.active
                    || !matches!(prop.kind, PropKind::Box | PropKind::Barrel)
                {
                    return None;
                }

                let body_position = self.body_position(prop.body)?;
                let point = macroquad::prelude::vec2(
                    body_position.translation.x,
                    body_position.translation.y,
                );

                if point.distance(position) < radius {
                    Some((index, point))
                } else {
                    None
                }
            })
            .collect();

        for (index, point) in affected_props {
            if let Some(prop) = self.props.get_mut(index) {
                prop.durability -= 5.0;

                if prop.durability <= 0.0 {
                    prop.active = false;

                    if prop.kind == PropKind::Barrel {
                        chain_detonations.push(point);
                    }
                }
            }
        }

        self.damage_joints_from_explosion(position, radius);

        self.explosions.push(Explosion {
            position,
            radius,
            life: 0.35,
        });

        for chain_position in chain_detonations {
            self.explode(chain_position, radius * 0.8, impulse * 0.75);
        }
    }

    fn damage_joints_from_explosion(&mut self, position: Vec2, radius: f32) {
        let center = vector![position.x, position.y];

        for joint in &mut self.joints {
            let parent_position = self
                .bodies
                .get(joint.parent)
                .map(|body| *body.translation());
            let child_position = self
                .bodies
                .get(joint.child)
                .map(|body| *body.translation());

            let (Some(parent_position), Some(child_position)) =
                (parent_position, child_position)
            else {
                continue;
            };

            let distance = ((parent_position + child_position) * 0.5 - center).norm();

            if distance < radius {
                let damage = 0.12 * (1.0 - distance / radius);
                joint.health -= joint.max_health * damage;
            }
        }

        self.remove_broken_joints();
    }

    fn update_map_effects(&mut self, delta_time: f32) {
        match self.map {
            MapKind::Water => {
                let submerged: Vec<RigidBodyHandle> = self
                    .bodies
                    .iter()
                    .filter_map(|(handle, body)| {
                        let position = body.translation();
                        (body.is_dynamic()
                            && position.y > 870.0
                            && position.y < 1420.0
                            && position.x > 40.0
                            && position.x < 2360.0)
                            .then_some(handle)
                    })
                    .collect();

                for handle in submerged {
                    if let Some(body) = self.bodies.get_mut(handle) {
                        let mass = body.mass().max(0.1);
                        let depth = (body.translation().y - 870.0).max(0.0);
                        let submersion = (depth / 120.0).clamp(0.0, 1.0);
                        let velocity = *body.linvel();
                        let drag = velocity * (-mass * 1.9 * submersion);
                        let buoyancy = mass * 1020.0 * submersion;
                        let angular_velocity = body.angvel();
                        body.add_force(vector![drag.x, drag.y - buoyancy], true);
                        body.add_torque(-angular_velocity * mass * 1.6 * submersion, true);
                    }
                }
            }
            MapKind::Lava => {
                let mut damaged = false;
                for joint in &mut self.joints {
                    let parent_in_lava = self
                        .bodies
                        .get(joint.parent)
                        .map(|body| body.translation().y > 1080.0)
                        .unwrap_or(false);
                    let child_in_lava = self
                        .bodies
                        .get(joint.child)
                        .map(|body| body.translation().y > 1080.0)
                        .unwrap_or(false);

                    if parent_in_lava || child_in_lava {
                        joint.health -= 150.0 * delta_time;
                        damaged |= joint.health <= 0.0;
                    }
                }

                if damaged {
                    self.remove_broken_joints();
                }
            }
            _ => {}
        }
    }

    fn update_joint_damage(&mut self, delta_time: f32) {
        let mut broken = false;

        for joint in &mut self.joints {
            let parent_velocity = self
                .bodies
                .get(joint.parent)
                .map(|body| *body.linvel())
                .unwrap_or(vector![0.0, 0.0]);
            let child_velocity = self
                .bodies
                .get(joint.child)
                .map(|body| *body.linvel())
                .unwrap_or(vector![0.0, 0.0]);

            let relative_speed = (parent_velocity - child_velocity).norm();

            if relative_speed > 900.0 {
                let impact = relative_speed - 900.0;
                joint.health -= impact * delta_time * 0.025;
            }

            if joint.health <= 0.0 {
                broken = true;
            }
        }

        if broken {
            self.remove_broken_joints();
        }
    }

    fn remove_broken_joints(&mut self) {
        let mut broken_handles = Vec::new();
        let mut detached = Vec::new();

        for joint in &self.joints {
            if joint.health <= 0.0 {
                broken_handles.push(joint.handle);
                detached.push(joint.child);
            }
        }

        for handle in broken_handles {
            self.impulse_joints.remove(handle, true);
        }

        for part in detached {
            if !self.detached_parts.contains(&part) {
                self.detached_parts.push(part);
            }
        }

        self.joints.retain(|joint| joint.health > 0.0);
    }

    fn empty_ragdoll() -> Ragdoll {
        Ragdoll {
            head: RigidBodyHandle::invalid(),
            torso: RigidBodyHandle::invalid(),
            left_upper_arm: RigidBodyHandle::invalid(),
            right_upper_arm: RigidBodyHandle::invalid(),
            left_lower_arm: RigidBodyHandle::invalid(),
            right_lower_arm: RigidBodyHandle::invalid(),
            left_upper_leg: RigidBodyHandle::invalid(),
            right_upper_leg: RigidBodyHandle::invalid(),
            left_lower_leg: RigidBodyHandle::invalid(),
            right_lower_leg: RigidBodyHandle::invalid(),
        }
    }

    fn create_floor(&mut self) {
        let body = self.bodies.insert(
            RigidBodyBuilder::fixed()
                .translation(vector![1200.0, 1250.0])
                .build(),
        );

        let collider = ColliderBuilder::cuboid(1200.0, 35.0)
            .friction(0.9)
            .restitution(0.03)
            .build();

        self.colliders
            .insert_with_parent(collider, body, &mut self.bodies);
    }

    fn create_map_geometry(&mut self) {
        match self.map {
            MapKind::TestRange => {
                self.create_static_box(vector![1930.0, 1050.0], vector![150.0, 160.0]);
                self.create_static_box(vector![300.0, 1080.0], vector![190.0, 130.0]);
            }
            MapKind::City => {
                self.create_static_box(vector![340.0, 1040.0], vector![220.0, 175.0]);
                self.create_static_box(vector![1840.0, 1060.0], vector![260.0, 155.0]);
                self.create_static_box(vector![1220.0, 1100.0], vector![155.0, 115.0]);
            }
            MapKind::Laboratory => {
                self.create_static_box(vector![340.0, 980.0], vector![42.0, 220.0]);
                self.create_static_box(vector![2060.0, 980.0], vector![42.0, 220.0]);
                self.create_static_box(vector![1200.0, 920.0], vector![300.0, 38.0]);
            }
            MapKind::Lava => {
                self.create_static_box(vector![430.0, 970.0], vector![210.0, 24.0]);
                self.create_static_box(vector![1200.0, 880.0], vector![170.0, 24.0]);
                self.create_static_box(vector![1970.0, 970.0], vector![210.0, 24.0]);
            }
            MapKind::Water => {
                self.create_static_box(vector![390.0, 1030.0], vector![175.0, 20.0]);
                self.create_static_box(vector![2010.0, 1030.0], vector![175.0, 20.0]);
                self.create_static_box(vector![1200.0, 1110.0], vector![260.0, 18.0]);
            }
        }
    }

    fn create_static_box(&mut self, position: Vector<Real>, half_size: Vector<Real>) {
        let body = self
            .bodies
            .insert(RigidBodyBuilder::fixed().translation(position).build());

        let collider = ColliderBuilder::cuboid(half_size.x, half_size.y)
            .friction(0.85)
            .restitution(0.03)
            .build();

        self.colliders
            .insert_with_parent(collider, body, &mut self.bodies);

        self.props.push(Prop {
            body,
            half_width: half_size.x,
            half_height: half_size.y,
            color: match self.map {
                MapKind::TestRange => [115, 123, 132, 255],
                MapKind::City => [92, 103, 114, 255],
                MapKind::Laboratory => [75, 124, 118, 255],
                MapKind::Lava => [116, 65, 49, 255],
                MapKind::Water => [63, 111, 143, 255],
            },
            kind: PropKind::Static,
            active: true,
            timer: -1.0,
            durability: f32::INFINITY,
        });
    }

    fn create_ragdoll(&mut self, origin: Vector<Real>) {
        let density = self.character.part_density();

        let torso = self.create_part(
            vector![origin.x, origin.y + 70.0],
            PartShape::Box(24.0, 44.0),
            density,
        );
        let head = self.create_part(
            vector![origin.x, origin.y],
            PartShape::Circle(27.0),
            density * 0.95,
        );
        let left_upper_arm = self.create_part(
            vector![origin.x - 43.0, origin.y + 66.0],
            PartShape::Box(10.0, 32.0),
            density * 0.7,
        );
        let right_upper_arm = self.create_part(
            vector![origin.x + 43.0, origin.y + 66.0],
            PartShape::Box(10.0, 32.0),
            density * 0.7,
        );
        let left_lower_arm = self.create_part(
            vector![origin.x - 70.0, origin.y + 112.0],
            PartShape::Box(9.0, 28.0),
            density * 0.6,
        );
        let right_lower_arm = self.create_part(
            vector![origin.x + 70.0, origin.y + 112.0],
            PartShape::Box(9.0, 28.0),
            density * 0.6,
        );
        let left_upper_leg = self.create_part(
            vector![origin.x - 15.0, origin.y + 142.0],
            PartShape::Box(12.0, 38.0),
            density * 0.85,
        );
        let right_upper_leg = self.create_part(
            vector![origin.x + 15.0, origin.y + 142.0],
            PartShape::Box(12.0, 38.0),
            density * 0.85,
        );
        let left_lower_leg = self.create_part(
            vector![origin.x - 18.0, origin.y + 198.0],
            PartShape::Box(10.0, 34.0),
            density * 0.75,
        );
        let right_lower_leg = self.create_part(
            vector![origin.x + 18.0, origin.y + 198.0],
            PartShape::Box(10.0, 34.0),
            density * 0.75,
        );

        self.connect(
            head,
            torso,
            point![0.0, 27.0],
            point![0.0, -43.0],
            [-0.65, 0.65],
        );
        self.connect(
            torso,
            left_upper_arm,
            point![-24.0, -28.0],
            point![19.0, -24.0],
            [-1.45, 1.45],
        );
        self.connect(
            torso,
            right_upper_arm,
            point![24.0, -28.0],
            point![-19.0, -24.0],
            [-1.45, 1.45],
        );
        self.connect(
            left_upper_arm,
            left_lower_arm,
            point![0.0, 32.0],
            point![27.0, -14.0],
            [-2.0, 2.0],
        );
        self.connect(
            right_upper_arm,
            right_lower_arm,
            point![0.0, 32.0],
            point![-27.0, -14.0],
            [-2.0, 2.0],
        );
        self.connect(
            torso,
            left_upper_leg,
            point![-13.0, 44.0],
            point![2.0, -28.0],
            [-1.3, 1.3],
        );
        self.connect(
            torso,
            right_upper_leg,
            point![13.0, 44.0],
            point![-2.0, -28.0],
            [-1.3, 1.3],
        );
        self.connect(
            left_upper_leg,
            left_lower_leg,
            point![0.0, 38.0],
            point![3.0, -18.0],
            [-1.65, 1.65],
        );
        self.connect(
            right_upper_leg,
            right_lower_leg,
            point![0.0, 38.0],
            point![-3.0, -18.0],
            [-1.65, 1.65],
        );

        self.ragdoll = Ragdoll {
            head,
            torso,
            left_upper_arm,
            right_upper_arm,
            left_lower_arm,
            right_lower_arm,
            left_upper_leg,
            right_upper_leg,
            left_lower_leg,
            right_lower_leg,
        };
    }

    fn create_part(
        &mut self,
        position: Vector<Real>,
        shape: PartShape,
        density: Real,
    ) -> RigidBodyHandle {
        let body = self.bodies.insert(
            RigidBodyBuilder::dynamic()
                .translation(position)
                .linear_damping(0.35)
                .angular_damping(0.72)
                .ccd_enabled(true)
                .build(),
        );

        let collider = match shape {
            PartShape::Circle(radius) => ColliderBuilder::ball(radius),
            PartShape::Box(width, height) => ColliderBuilder::cuboid(width, height),
        }
        .density(density)
        .friction(0.82)
        .restitution(0.015)
        .build();

        self.colliders
            .insert_with_parent(collider, body, &mut self.bodies);

        body
    }

    fn connect(
        &mut self,
        parent: RigidBodyHandle,
        child: RigidBodyHandle,
        parent_anchor: Point<Real>,
        child_anchor: Point<Real>,
        limits: [Real; 2],
    ) {
        let joint = RevoluteJointBuilder::new()
            .local_anchor1(parent_anchor)
            .local_anchor2(child_anchor)
            .limits(limits)
            .contacts_enabled(false)
            .build();
        let handle = self.impulse_joints.insert(parent, child, joint, true);
        let max_health = self.character.joint_strength();
        self.total_joint_capacity += max_health;

        self.joints.push(JointHealth {
            handle,
            parent,
            child,
            health: max_health,
            max_health,
        });
    }
}

enum PartShape {
    Circle(Real),
    Box(Real, Real),
}
