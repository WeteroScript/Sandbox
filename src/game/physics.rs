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
            Self::Melon => 34.0,
            Self::Robot => 72.0,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MapKind {
    TestRange,
    City,
    Laboratory,
}

impl MapKind {
    pub fn next(self) -> Self {
        match self {
            Self::TestRange => Self::City,
            Self::City => Self::Laboratory,
            Self::Laboratory => Self::TestRange,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::TestRange => "TEST RANGE",
            Self::City => "CITY",
            Self::Laboratory => "LABORATORY",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum WeaponKind {
    Pistol,
    Rifle,
    Bat,
    Katana,
}

impl WeaponKind {
    pub fn next(self) -> Self {
        match self {
            Self::Pistol => Self::Rifle,
            Self::Rifle => Self::Bat,
            Self::Bat => Self::Katana,
            Self::Katana => Self::Pistol,
        }
    }

    pub fn name(self) -> &'static str {
        match self {
            Self::Pistol => "PISTOL",
            Self::Rifle => "RIFLE",
            Self::Bat => "BAT",
            Self::Katana => "KATANA",
        }
    }
}

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
    pub props: Vec<Prop>,
    pub explosions: Vec<Explosion>,
    pub character: CharacterKind,
    pub map: MapKind,
    pub weapon: WeaponKind,
    joints: Vec<JointHealth>,
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
            props: Vec::new(),
            explosions: Vec::new(),
            character,
            map,
            weapon: WeaponKind::Pistol,
            joints: Vec::new(),
            detached_parts: Vec::new(),
            spawn_index: 0,
        };

        world.create_floor();
        world.create_map_geometry();
        world.create_ragdoll(vector![640.0, 230.0]);
        world.spawn_box(vector![360.0, 470.0], vector![70.0, 70.0]);
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

        let direction = vector![direction.x, direction.y];
        let force = match self.character {
            CharacterKind::Melon => 900.0,
            CharacterKind::Robot => 1250.0,
        };

        if let Some(torso) = self.bodies.get_mut(self.ragdoll.torso) {
            torso.apply_force(direction * vector![force, force * 0.2], true);
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
            WeaponKind::Rifle => self.spawn_projectile(origin, 1800.0, 12.0),
            WeaponKind::Bat => self.apply_melee_impulse(origin, 100.0, 700.0),
            WeaponKind::Katana => self.apply_melee_impulse(origin, 130.0, 1100.0),
        }
    }

    pub fn spawn_grenade(&mut self) {
        self.spawn_special(
            vector![720.0, 180.0],
            vector![14.0, 14.0],
            PropKind::Grenade,
            [65, 175, 85, 255],
            3.0,
            1.0,
        );
    }

    pub fn spawn_barrel(&mut self) {
        self.spawn_special(
            vector![790.0, 180.0],
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
            joint.health -= joint.max_health * 0.45;
        }
        self.remove_broken_joints();
    }

    pub fn joint_health_ratio(&self) -> f32 {
        if self.joints.is_empty() {
            return 0.0;
        }

        let current: f32 = self.joints.iter().map(|joint| joint.health.max(0.0)).sum();
        let maximum: f32 = self.joints.iter().map(|joint| joint.max_health).sum();

        (current / maximum).clamp(0.0, 1.0)
    }

    pub fn is_detached(&self, handle: RigidBodyHandle) -> bool {
        self.detached_parts.contains(&handle)
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
            .linear_damping(0.15)
            .angular_damping(0.2)
            .ccd_enabled(true)
            .build();

        let body_handle = self.bodies.insert(body);
        let collider = ColliderBuilder::cuboid(half_size.x, half_size.y)
            .density(if kind == PropKind::Barrel { 1.3 } else { 0.8 })
            .friction(0.7)
            .restitution(0.15)
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
        let body = RigidBodyBuilder::dynamic()
            .translation(position)
            .linvel(vector![speed, 0.0])
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
        let origin = vector![origin.x, origin.y];

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

        self.apply_test_damage();
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
                    || !matches!(target.kind, PropKind::Box | PropKind::Barrel)
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

            let mut detonation = None;

            if let Some(target) = self.props.get_mut(target_index) {
                target.durability -= damage;

                if target.durability <= 0.0 {
                    target.active = false;

                    if target.kind == PropKind::Barrel {
                        if let Some(position) = self.body_position(target.body) {
                            detonation =
                                Some(vector![position.translation.x, position.translation.y]);
                        }
                    }
                }
            }

            if let Some(position) = detonation {
                detonations.push(position);
            }
        }

        for position in detonations {
            self.explode(position, 240.0, 1150.0);
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
                if !prop.active || !matches!(prop.kind, PropKind::Box | PropKind::Barrel) {
                    return None;
                }

                let position = self.body_position(prop.body)?;
                let point = vec2(position.translation.x, position.translation.y);

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

        self.apply_test_damage();
        self.explosions.push(Explosion {
            position,
            radius,
            life: 0.35,
        });

        for chain_position in chain_detonations {
            self.explode(chain_position, radius * 0.8, impulse * 0.75);
        }
    }

    fn update_joint_damage(&mut self, delta_time: f32) {
        let mut broken = false;

        for joint in &mut self.joints {
            let parent_speed = self
                .bodies
                .get(joint.parent)
                .map(|body| body.linvel().norm())
                .unwrap_or(0.0);
            let child_speed = self
                .bodies
                .get(joint.child)
                .map(|body| body.linvel().norm())
                .unwrap_or(0.0);
            let relative_speed = (parent_speed - child_speed).abs();

            if relative_speed > 18.0 {
                joint.health -= (relative_speed - 18.0) * delta_time * 1.8;
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
                .translation(vector![640.0, 650.0])
                .build(),
        );
        let collider = ColliderBuilder::cuboid(640.0, 25.0)
            .friction(0.8)
            .restitution(0.05)
            .build();

        self.colliders
            .insert_with_parent(collider, body, &mut self.bodies);
    }

    fn create_map_geometry(&mut self) {
        match self.map {
            MapKind::TestRange => {
                self.create_static_box(vector![1030.0, 550.0], vector![80.0, 100.0]);
                self.create_static_box(vector![180.0, 570.0], vector![100.0, 80.0]);
            }
            MapKind::City => {
                self.create_static_box(vector![180.0, 555.0], vector![120.0, 95.0]);
                self.create_static_box(vector![980.0, 570.0], vector![150.0, 80.0]);
                self.create_static_box(vector![680.0, 590.0], vector![90.0, 60.0]);
            }
            MapKind::Laboratory => {
                self.create_static_box(vector![180.0, 520.0], vector![30.0, 130.0]);
                self.create_static_box(vector![1100.0, 520.0], vector![30.0, 130.0]);
                self.create_static_box(vector![640.0, 490.0], vector![170.0, 25.0]);
            }
        }
    }

    fn create_static_box(&mut self, position: Vector<Real>, half_size: Vector<Real>) {
        let body = self.bodies.insert(RigidBodyBuilder::fixed().translation(position).build());
        let collider = ColliderBuilder::cuboid(half_size.x, half_size.y)
            .friction(0.8)
            .restitution(0.05)
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
            },
            kind: PropKind::Static,
            active: true,
            timer: -1.0,
            durability: f32::INFINITY,
        });
    }

    fn create_ragdoll(&mut self, origin: Vector<Real>) {
        let density = match self.character {
            CharacterKind::Melon => 0.7,
            CharacterKind::Robot => 1.8,
        };

        let torso = self.create_part(vector![origin.x, origin.y + 70.0], PartShape::Box(24.0, 44.0), density);
        let head = self.create_part(vector![origin.x, origin.y], PartShape::Circle(27.0), density * 0.8);
        let left_upper_arm = self.create_part(vector![origin.x - 43.0, origin.y + 66.0], PartShape::Box(10.0, 32.0), density * 0.5);
        let right_upper_arm = self.create_part(vector![origin.x + 43.0, origin.y + 66.0], PartShape::Box(10.0, 32.0), density * 0.5);
        let left_lower_arm = self.create_part(vector![origin.x - 70.0, origin.y + 112.0], PartShape::Box(9.0, 28.0), density * 0.4);
        let right_lower_arm = self.create_part(vector![origin.x + 70.0, origin.y + 112.0], PartShape::Box(9.0, 28.0), density * 0.4);
        let left_upper_leg = self.create_part(vector![origin.x - 15.0, origin.y + 142.0], PartShape::Box(12.0, 38.0), density * 0.7);
        let right_upper_leg = self.create_part(vector![origin.x + 15.0, origin.y + 142.0], PartShape::Box(12.0, 38.0), density * 0.7);
        let left_lower_leg = self.create_part(vector![origin.x - 18.0, origin.y + 198.0], PartShape::Box(10.0, 34.0), density * 0.6);
        let right_lower_leg = self.create_part(vector![origin.x + 18.0, origin.y + 198.0], PartShape::Box(10.0, 34.0), density * 0.6);

        self.connect(head, torso, point![0.0, 27.0], point![0.0, -44.0]);
        self.connect(torso, left_upper_arm, point![-24.0, -28.0], point![0.0, -32.0]);
        self.connect(torso, right_upper_arm, point![24.0, -28.0], point![0.0, -32.0]);
        self.connect(left_upper_arm, left_lower_arm, point![0.0, 32.0], point![0.0, -28.0]);
        self.connect(right_upper_arm, right_lower_arm, point![0.0, 32.0], point![0.0, -28.0]);
        self.connect(torso, left_upper_leg, point![-13.0, 44.0], point![0.0, -38.0]);
        self.connect(torso, right_upper_leg, point![13.0, 44.0], point![0.0, -38.0]);
        self.connect(left_upper_leg, left_lower_leg, point![0.0, 38.0], point![0.0, -34.0]);
        self.connect(right_upper_leg, right_lower_leg, point![0.0, 38.0], point![0.0, -34.0]);

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
                .linear_damping(0.08)
                .angular_damping(0.12)
                .ccd_enabled(true)
                .build(),
        );

        let collider = match shape {
            PartShape::Circle(radius) => ColliderBuilder::ball(radius),
            PartShape::Box(width, height) => ColliderBuilder::cuboid(width, height),
        }
        .density(density)
        .friction(0.55)
        .restitution(0.08)
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
    ) {
        let joint = RevoluteJointBuilder::new()
            .local_anchor1(parent_anchor)
            .local_anchor2(child_anchor)
            .build();
        let handle = self.impulse_joints.insert(parent, child, joint, true);
        let max_health = self.character.joint_strength();

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