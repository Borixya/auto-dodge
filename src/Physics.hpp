#pragma once
#include "Hazards.hpp"
#include <cocos2d.h>
#include <vector>
#include <optional>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  GD Game Modes
// ─────────────────────────────────────────────────────────────────────────────
enum class GDMode : int {
    Cube   = 0,
    Ship   = 1,
    Ball   = 2,
    UFO    = 3,
    Wave   = 4,
    Robot  = 5,
    Spider = 6,
    Swing  = 7,
};

// ─────────────────────────────────────────────────────────────────────────────
//  Physics constants (per frame at 60 fps – GD internal units)
// ─────────────────────────────────────────────────────────────────────────────
struct ModePhysics {
    float gravity;          // applied each frame (negative = downward)
    float jumpVelocity;     // vy when jumping
    float maxFallSpeed;     // vy minimum (most negative)
    float thrustAccel;      // ship/swing: vy acceleration when button held
    float maxThrustSpeed;   // ship/swing: max vy upward
    float waveVYRatio;      // wave: vy = vx * this (holding = positive)
    float robotMinJump;     // robot: vy at min charge
    float robotMaxJump;     // robot: vy at max charge
    int   robotMaxCharge;   // robot: frames to reach full charge
    float hitboxW;          // player hitbox width
    float hitboxH;          // player hitbox height
};

// Values tuned to match GD 2.2 internal physics.
// All velocities in cocos2d "points per frame" at 60 fps.
inline ModePhysics PHYSICS_TABLE[8] = {
    // Cube
    { -0.958f, 10.5f, -16.0f,  0.f,   0.f,   0.f, 0.f, 0.f, 0, 17.f, 17.f },
    // Ship
    { -0.43f,   0.f, -10.0f,   0.67f, 9.0f,  0.f, 0.f, 0.f, 0, 17.f, 12.f },
    // Ball (like cube, flip gravity instead of jump)
    { -0.958f,  9.5f, -16.0f,  0.f,   0.f,   0.f, 0.f, 0.f, 0, 17.f, 17.f },
    // UFO
    { -0.43f,   0.f, -10.0f,   0.55f, 7.0f,  0.f, 0.f, 0.f, 0, 17.f, 17.f },
    // Wave  (vy is ±vx * ratio at 45°)
    {  0.f,     0.f,   0.f,    0.f,   0.f,   1.0f,0.f, 0.f, 0,  8.f,  8.f },
    // Robot (charged jump)
    { -0.958f,  0.f, -16.0f,  0.f,   0.f,   0.f, 6.5f,14.5f,20, 17.f, 25.f },
    // Spider (treat like ball for trajectory)
    { -0.958f,  9.0f, -14.0f,  0.f,   0.f,   0.f, 0.f, 0.f, 0, 17.f, 17.f },
    // Swing
    { -0.50f,   0.f, -11.0f,   0.70f, 10.0f, 0.f, 0.f, 0.f, 0, 17.f, 17.f },
};

inline const ModePhysics& physicsFor(GDMode m) {
    int i = std::clamp((int)m, 0, 7);
    return PHYSICS_TABLE[i];
}

// ─────────────────────────────────────────────────────────────────────────────
//  Simulation State – one snapshot in the trajectory
// ─────────────────────────────────────────────────────────────────────────────
struct SimState {
    float   x = 0.f, y = 0.f;
    float   vx = 0.f, vy = 0.f;
    bool    gravFlipped = false;    // true = gravity is upward
    bool    onGround = false;       // true = resting on a surface
    bool    buttonHeld = false;     // current button state
    int     robotCharge = 0;        // robot mode: how many frames held so far
    bool    dashing = false;        // in a dash orb movement
    int     dashFrames = 0;         // remaining dash frames
    GDMode  mode = GDMode::Cube;
    bool    dead = false;           // hit a lethal during simulation
    bool    usedOrb = false;        // flagged when an orb was consumed
};

// ─────────────────────────────────────────────────────────────────────────────
//  Advance simulation by one frame
// ─────────────────────────────────────────────────────────────────────────────
inline SimState stepPhysics(SimState s, bool press, bool held, bool release) {
    if (s.dead) return s;

    const ModePhysics& ph = physicsFor(s.mode);
    const float gravDir = s.gravFlipped ? 1.f : -1.f;

    switch (s.mode) {

    // ── Cube ──────────────────────────────────────────────────────────────────
    case GDMode::Cube:
        if (press && s.onGround) {
            s.vy = ph.jumpVelocity * (s.gravFlipped ? -1.f : 1.f);
            s.onGround = false;
        }
        s.vy += ph.gravity * gravDir * (-1.f); // gravity always pulls toward floor
        // Simplified: gravity pulls downward (negates y) unless flipped
        s.vy = std::max(s.vy, ph.maxFallSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    // ── Ship ──────────────────────────────────────────────────────────────────
    case GDMode::Ship:
        if (held)
            s.vy += ph.thrustAccel;
        else
            s.vy += ph.gravity;
        s.vy = std::clamp(s.vy, ph.maxFallSpeed, ph.maxThrustSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    // ── Ball ──────────────────────────────────────────────────────────────────
    case GDMode::Ball:
        if (press && s.onGround) {
            s.gravFlipped = !s.gravFlipped;
            s.vy = ph.jumpVelocity * (s.gravFlipped ? 1.f : -1.f) * 0.5f;
            s.onGround = false;
        }
        s.vy += ph.gravity * (s.gravFlipped ? -1.f : 1.f);
        s.vy = std::clamp(s.vy,
            s.gravFlipped ? -ph.maxThrustSpeed : ph.maxFallSpeed,
            s.gravFlipped ?  ph.maxThrustSpeed : -ph.maxFallSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    // ── UFO ───────────────────────────────────────────────────────────────────
    case GDMode::UFO:
        if (press) {
            s.vy = ph.thrustAccel * 5.f * (s.gravFlipped ? -1.f : 1.f);
        } else {
            s.vy += ph.gravity * (s.gravFlipped ? -1.f : 1.f);
        }
        s.vy = std::clamp(s.vy, ph.maxFallSpeed, -ph.maxFallSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    // ── Wave ──────────────────────────────────────────────────────────────────
    case GDMode::Wave:
        // Holding = move up-right, released = move down-right
        if (s.gravFlipped)
            s.vy = held ? -(s.vx * ph.waveVYRatio) : (s.vx * ph.waveVYRatio);
        else
            s.vy = held ?  (s.vx * ph.waveVYRatio) : -(s.vx * ph.waveVYRatio);
        s.y += s.vy;
        s.x += s.vx;
        break;

    // ── Robot ─────────────────────────────────────────────────────────────────
    case GDMode::Robot:
        if (held && s.onGround) {
            // Charging: accumulate frames
            s.robotCharge = std::min(s.robotCharge + 1, ph.robotMaxCharge);
        }
        if (release && s.onGround && s.robotCharge > 0) {
            float t = (float)s.robotCharge / (float)ph.robotMaxCharge;
            s.vy = ph.robotMinJump + t * (ph.robotMaxJump - ph.robotMinJump);
            s.vy *= s.gravFlipped ? -1.f : 1.f;
            s.robotCharge = 0;
            s.onGround = false;
        } else if (press && s.onGround && s.robotCharge == 0) {
            // Instant small jump if not charged
            s.vy = ph.robotMinJump * (s.gravFlipped ? -1.f : 1.f);
            s.onGround = false;
        }
        s.vy += ph.gravity * (s.gravFlipped ? -1.f : 1.f);
        s.vy = std::max(s.vy, ph.maxFallSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    // ── Spider / Swing ────────────────────────────────────────────────────────
    case GDMode::Spider:
        // Spider teleports to opposite surface; approximate as instant grav flip
        if (press && s.onGround) {
            s.gravFlipped = !s.gravFlipped;
            s.vy = 0.f;
            s.onGround = false;
        }
        s.vy += ph.gravity * (s.gravFlipped ? -1.f : 1.f);
        s.vy = std::max(s.vy, ph.maxFallSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    case GDMode::Swing:
        if (held)
            s.vy += ph.thrustAccel * (s.gravFlipped ? -1.f : 1.f);
        else
            s.vy += ph.gravity * (s.gravFlipped ? -1.f : 1.f);
        s.vy = std::clamp(s.vy, -ph.maxThrustSpeed, ph.maxThrustSpeed);
        s.y += s.vy;
        s.x += s.vx;
        break;

    default:
        s.x += s.vx;
        break;
    }

    s.buttonHeld = held;
    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Player bounding rect at a given SimState position
// ─────────────────────────────────────────────────────────────────────────────
inline cocos2d::CCRect stateHitbox(const SimState& s) {
    const ModePhysics& ph = physicsFor(s.mode);
    return {
        s.x - ph.hitboxW * 0.5f,
        s.y - ph.hitboxH * 0.5f,
        ph.hitboxW,
        ph.hitboxH
    };
}
