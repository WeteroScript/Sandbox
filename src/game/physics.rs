use rapier2d::prelude::*;

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
}

impl PhysicsWorld {
    pub fn new() -> Self {
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
            ragdoll: Ragdoll {
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
            },
        };

        world.create_floor();
        world.create_ragdoll(vector![640.0, 230.0]);
        world.create_box(vector![360.0, 470.0], vector![70.0, 70.0]);

        world
    }

    pub fn step(&mut self, delta_time: f32) {
        self.integration_parameters.dt = delta_time.clamp(1.0 / 120.0, 1.0 / 30.0);

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
    }

    pub fn body_position(&self, handle: RigidBodyHandle) -> Option<Isometry<Real>> {
        self.bodies.get(handle).map(|body| *body.position())
    }

    fn create_floor(&mut self) {
        let body = RigidBodyBuilder::fixed()
            .translation(vector![640.0, 650.0])
            .build();
        let body_handle = self.bodies.insert(body);

        let collider = ColliderBuilder::cuboid(640.0, 25.0)
            .friction(0.8)
            .restitution(0.05)
            .build();

        self.colliders
            .insert_with_parent(collider, body_handle, &mut self.bodies);
    }

    fn create_box(&mut self, position: Vector<Real>, half_size: Vector<Real>) {
        let body = RigidBodyBuilder::dynamic()
            .translation(position)
            .rotation(0.1)
            .linear_damping(0.15)
            .angular_damping(0.2)
            .build();
        let body_handle = self.bodies.insert(body);

        let collider = ColliderBuilder::cuboid(half_size.x, half_size.y)
            .density(0.8)
            .friction(0.7)
            .restitution(0.15)
            .build();

        self.colliders
            .insert_with_parent(collider, body_handle, &mut self.bodies);
    }

    fn create_ragdoll(&mut self, origin: Vector<Real>) {
        let torso = self.create_part(
            vector![origin.x, origin.y + 70.0],
            PartShape::Box(24.0, 44.0),
            1.0,
        );

        let head = self.create_part(
            vector![origin.x, origin.y],
            PartShape::Circle(27.0),
            0.8,
        );

        let left_upper_arm = self.create_part(
            vector![origin.x - 43.0, origin.y + 66.0],
            PartShape::Box(10.0, 32.0),
            0.5,
        );

        let right_upper_arm = self.create_part(
            vector![origin.x + 43.0, origin.y + 66.0],
            PartShape::Box(10.0, 32.0),
            0.5,
        );

        let left_lower_arm = self.create_part(
            vector![origin.x - 70.0, origin.y + 112.0],
            PartShape::Box(9.0, 28.0),
            0.4,
        );

        let right_lower_arm = self.create_part(
            vector![origin.x + 70.0, origin.y + 112.0],
            PartShape::Box(9.0, 28.0),
            0.4,
        );

        let left_upper_leg = self.create_part(
            vector![origin.x - 15.0, origin.y + 142.0],
            PartShape::Box(12.0, 38.0),
            0.7,
        );

        let right_upper_leg = self.create_part(
            vector![origin.x + 15.0, origin.y + 142.0],
            PartShape::Box(12.0, 38.0),
            0.7,
        );

        let left_lower_leg = self.create_part(
            vector![origin.x - 18.0, origin.y + 198.0],
            PartShape::Box(10.0, 34.0),
            0.6,
        );

        let right_lower_leg = self.create_part(
            vector![origin.x + 18.0, origin.y + 198.0],
            PartShape::Box(10.0, 34.0),
            0.6,
        );

        self.connect(head, torso, point![0.0, 27.0], point![0.0, -44.0]);
        self.connect(
            torso,
            left_upper_arm,
            point![-24.0, -28.0],
            point![0.0, -32.0],
        );
        self.connect(
            torso,
            right_upper_arm,
            point![24.0, -28.0],
            point![0.0, -32.0],
        );
        self.connect(
            left_upper_arm,
            left_lower_arm,
            point![0.0, 32.0],
            point![0.0, -28.0],
        );
        self.connect(
            right_upper_arm,
            right_lower_arm,
            point![0.0, 32.0],
            point![0.0, -28.0],
        );
        self.connect(
            torso,
            left_upper_leg,
            point![-13.0, 44.0],
            point![0.0, -38.0],
        );
        self.connect(
            torso,
            right_upper_leg,
            point![13.0, 44.0],
            point![0.0, -38.0],
        );
        self.connect(
            left_upper_leg,
            left_lower_leg,
            point![0.0, 38.0],
            point![0.0, -34.0],
        );
        self.connect(
            right_upper_leg,
            right_lower_leg,
            point![0.0, 38.0],
            point![0.0, -34.0],
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

    fn create_part(&mut self, position: Vector<Real>, shape: PartShape, density: Real) -> RigidBodyHandle {
        let body = RigidBodyBuilder::dynamic()
            .translation(position)
            .linear_damping(0.08)
            .angular_damping(0.12)
            .ccd_enabled(true)
            .build();

        let body_handle = self.bodies.insert(body);

        let collider = match shape {
            PartShape::Circle(radius) => ColliderBuilder::ball(radius),
            PartShape::Box(half_width, half_height) => {
                ColliderBuilder::cuboid(half_width, half_height)
            }
        }
        .density(density)
        .friction(0.55)
        .restitution(0.08)
        .build();

        self.colliders
            .insert_with_parent(collider, body_handle, &mut self.bodies);

        body_handle
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

        self.impulse_joints
            .insert(parent, child, joint, true);
    }
}

enum PartShape {
    Circle(Real),
    Box(Real, Real),
}