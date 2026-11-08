use nalgebra::vector;
use rapier2d::prelude::{ColliderBuilder, RigidBodyBuilder};

use super::super::physics::{MapKind, PhysicsWorld, Prop, PropKind};

pub fn install_bounds(world: &mut PhysicsWorld) {
    create_boundary(
        world,
        vector![-70.0, 350.0],
        vector![24.0, 430.0],
        [72, 82, 94, 255],
    );
    create_boundary(
        world,
        vector![1350.0, 350.0],
        vector![24.0, 430.0],
        [72, 82, 94, 255],
    );
    create_boundary(
        world,
        vector![640.0, -90.0],
        vector![710.0, 24.0],
        [60, 70, 82, 255],
    );
    create_boundary(
        world,
        vector![640.0, 700.0],
        vector![710.0, 28.0],
        [94, 104, 112, 255],
    );
}

pub fn install_environment(world: &mut PhysicsWorld, map: MapKind) {
    match map {
        MapKind::TestRange => {}
        MapKind::City => {
            create_sensor_zone(
                world,
                vector![650.0, 586.0],
                vector![210.0, 42.0],
                [38, 124, 184, 205],
            );
        }
        MapKind::Laboratory => {
            create_sensor_zone(
                world,
                vector![640.0, 610.0],
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