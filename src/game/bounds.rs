use nalgebra::vector;
use rapier2d::prelude::{ColliderBuilder, RigidBodyBuilder};

use super::super::physics::{MapKind, PhysicsWorld, Prop, PropKind};

pub fn install_bounds(world: &mut PhysicsWorld) {
    // A closed 2,400 x 1,250 play area. The side and lower barriers
    // overlap the floor collider so fast-moving bodies cannot escape.
    create_boundary(
        world,
        vector![-55.0, 650.0],
        vector![35.0, 850.0],
        [72, 82, 94, 255],
    );
    create_boundary(
        world,
        vector![2455.0, 650.0],
        vector![35.0, 850.0],
        [72, 82, 94, 255],
    );
    create_boundary(
        world,
        vector![1200.0, -95.0],
        vector![1250.0, 35.0],
        [60, 70, 82, 255],
    );
    create_boundary(
        world,
        vector![1200.0, 1435.0],
        vector![1250.0, 35.0],
        [94, 104, 112, 255],
    );
}

pub fn install_environment(world: &mut PhysicsWorld, map: MapKind) {
    match map {
        MapKind::TestRange => {}
        MapKind::City => {
            create_sensor_zone(
                world,
                vector![1200.0, 1190.0],
                vector![210.0, 42.0],
                [38, 124, 184, 205],
            );
        }
        MapKind::Laboratory => {
            create_sensor_zone(
                world,
                vector![1200.0, 1190.0],
                vector![260.0, 32.0],
                [224, 67, 36, 220],
            );
        }
    }
}

fn create_boundary(
    world: &mut PhysicsWorld,
    position: nalgebra::Vector2<f32>,
    half_size: nalgebra::Vector2<f32>,
    color: [u8; 4],
) {
    let body = world.bodies.insert(
        RigidBodyBuilder::fixed()
            .translation(position)
            .build(),
    );

    let collider = ColliderBuilder::cuboid(half_size.x, half_size.y)
        .friction(0.9)
        .restitution(0.02)
        .build();

    world
        .colliders
        .insert_with_parent(collider, body, &mut world.bodies);

    world.props.push(Prop {
        body,
        half_width: half_size.x,
        half_height: half_size.y,
        color,
        kind: PropKind::Static,
        active: true,
        timer: -1.0,
        durability: f32::INFINITY,
    });
}

fn create_sensor_zone(
    world: &mut PhysicsWorld,
    position: nalgebra::Vector2<f32>,
    half_size: nalgebra::Vector2<f32>,
    color: [u8; 4],
) {
    let body = world.bodies.insert(
        RigidBodyBuilder::fixed()
            .translation(position)
            .build(),
    );

    let collider = ColliderBuilder::cuboid(half_size.x, half_size.y)
        .sensor(true)
        .friction(0.0)
        .restitution(0.0)
        .build();

    world
        .colliders
        .insert_with_parent(collider, body, &mut world.bodies);

    world.props.push(Prop {
        body,
        half_width: half_size.x,
        half_height: half_size.y,
        color,
        kind: PropKind::Static,
        active: true,
        timer: -1.0,
        durability: f32::INFINITY,
    });
}