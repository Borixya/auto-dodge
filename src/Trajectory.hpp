#pragma once
#include "Physics.hpp"
#include <Geode/Geode.hpp>
#include <functional>
#include <vector>
#include <optional>

using namespace cocos2d;

// ─────────────────────────────────────────────────────────────────────────────
//  SceneObject – lightweight snapshot of a relevant game object
// ─────────────────────────────────────────────────────────────────────────────
struct SceneObject {
    int    id;
    CCRect worldRect;          // bounding box in world (object-layer) space
    bool   lethal;
    bool   isOrb;
    bool   isPad;
    bool   isDashOrb;
};

// ─────────────────────────────────────────────────────────────────────────────
//  TrajectoryResult
// ─────────────────────────────────────────────────────────────────────────────
struct TrajectoryResult {
    bool   died = false;                  // does this trajectory hit a lethal?
    int    deathFrame = -1;               // frame index where death occurs
    std::vector<SimState> states;         // full path
};

// ─────────────────────────────────────────────────────────────────────────────
//  OrbDecision – answer from evaluateOrb()
// ─────────────────────────────────────────────────────────────────────────────
struct OrbDecision {
    bool   shouldClick = false;
    int    orbObjectId = -1;
    float  activationX = 0.f;   // world X where we should be when clicking
};

// ─────────────────────────────────────────────────────────────────────────────
//  Trajectory Engine
// ─────────────────────────────────────────────────────────────────────────────
namespace Trajectory {

    // Build a lightweight snapshot of all relevant objects in the scene.
    // Only objects within [playerX, playerX + scanWidth] are included.
    inline std::vector<SceneObject> buildSceneSnapshot(
        CCArray* objectLayerChildren,
        float playerX,
        float scanWidth)
    {
        std::vector<SceneObject> scene;
        if (!objectLayerChildren) return scene;

        CCObject* raw = nullptr;
        CCARRAY_FOREACH(objectLayerChildren, raw) {
            auto go = dynamic_cast<GameObject*>(raw);
            if (!go || !go->isVisible()) continue;

            int id = go->m_objectID;
            bool lethal   = Hazards::isLethal(id);
            bool orb      = Hazards::isOrb(id);
            bool pad      = Hazards::isPad(id);
            bool dashOrb  = Hazards::isDashOrb(id);

            if (!lethal && !orb && !pad) continue;

            CCRect bb = go->boundingBox();

            // Only include objects ahead of the player (within scan width)
            float objRight = bb.getMaxX();
            float objLeft  = bb.getMinX();
            if (objRight < playerX || objLeft > playerX + scanWidth) continue;

            scene.push_back({ id, bb, lethal, orb, pad, dashOrb });
        }
        return scene;
    }

    // Apply a pad effect to a SimState (pads trigger automatically)
    inline SimState applyPad(SimState s, const Hazards::PadInfo& pad) {
        s.vy = pad.vyOverride * (s.gravFlipped ? -1.f : 1.f);
        if (pad.flipGrav) {
            s.gravFlipped = !s.gravFlipped;
            s.vy = -s.vy;
        }
        s.onGround = false;
        return s;
    }

    // Apply an orb effect to a SimState (orb triggered by click)
    inline SimState applyOrb(SimState s, const Hazards::OrbInfo& orb) {
        if (orb.flipGrav) {
            s.gravFlipped = !s.gravFlipped;
        }
        if (orb.vyOverride != 0.f) {
            s.vy = orb.vyOverride * (s.gravFlipped ? -1.f : 1.f);
        }
        if (orb.dashVX != 0.f) {
            s.vx = orb.dashVX;
            s.dashing = true;
            s.dashFrames = 12; // dash lasts ~12 frames
        }
        s.onGround = false;
        s.usedOrb = true;
        return s;
    }

    // ── Core trajectory simulator ─────────────────────────────────────────────
    // inputFn(frame) → {press, held, release} for that frame.
    // Automatically applies pads on contact and optionally orbs.
    inline TrajectoryResult simulate(
        SimState                            initial,
        int                                 steps,
        const std::vector<SceneObject>&     scene,
        std::function<bool(int /*frame*/)>  heldFn,
        bool                                clickOrbsAutomatically = false)
    {
        TrajectoryResult result;
        result.states.reserve(steps);

        SimState s = initial;
        bool prevHeld = initial.buttonHeld;

        for (int f = 0; f < steps; ++f) {
            bool held    = heldFn(f);
            bool press   = held && !prevHeld;
            bool release = !held && prevHeld;
            prevHeld     = held;

            // ── Check pad & orb contacts BEFORE physics step ─────────────────
            CCRect playerBox = stateHitbox(s);

            for (const auto& obj : scene) {
                if (!obj.worldRect.intersectsRect(playerBox)) continue;

                if (obj.isPad) {
                    auto it = Hazards::PADS.find(obj.id);
                    if (it != Hazards::PADS.end()) {
                        s = applyPad(s, it->second);
                        // Recalculate press/held/release after pad
                        press = false; held = s.buttonHeld; release = false;
                    }
                }

                if (clickOrbsAutomatically && obj.isOrb && !s.usedOrb) {
                    auto it = Hazards::ORBS.find(obj.id);
                    if (it != Hazards::ORBS.end()) {
                        s = applyOrb(s, it->second);
                        press = false; release = false;
                    }
                }

                if (obj.lethal) {
                    s.dead = true;
                    result.died      = true;
                    result.deathFrame = f;
                    result.states.push_back(s);
                    return result;
                }
            }

            // ── Dash ──────────────────────────────────────────────────────────
            if (s.dashing) {
                s.x += s.vx;
                s.dashFrames--;
                if (s.dashFrames <= 0) {
                    s.dashing = false;
                    s.vx = initial.vx; // restore normal x speed
                }
                result.states.push_back(s);
                continue;
            }

            // ── Advance physics ───────────────────────────────────────────────
            s = stepPhysics(s, press, held, release);
            result.states.push_back(s);
        }

        return result;
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Evaluate whether clicking a specific orb now leads to a safe path.
    //  Returns OrbDecision::shouldClick = true if the orb saves us.
    // ─────────────────────────────────────────────────────────────────────────
    inline OrbDecision evaluateOrbs(
        const SimState&                  startState,
        int                              steps,
        const std::vector<SceneObject>&  scene,
        const std::function<bool(int)>&  normalHeldFn)
    {
        // First check the "do nothing special with orbs" trajectory
        auto baseResult = simulate(startState, steps, scene, normalHeldFn, false);

        // If base is already safe, no need to use any orb
        if (!baseResult.died) return {};

        // Try clicking each orb in range
        for (const auto& obj : scene) {
            if (!obj.isOrb) continue;
            auto orbIt = Hazards::ORBS.find(obj.id);
            if (orbIt == Hazards::ORBS.end()) continue;

            // Simulate: apply the orb effect right now (at frame 0)
            SimState boosted = startState;
            boosted = applyOrb(boosted, orbIt->second);

            auto boostedResult = simulate(boosted, steps, scene, normalHeldFn, false);

            if (!boostedResult.died) {
                // This orb click saves us
                return OrbDecision{
                    true,
                    obj.id,
                    obj.worldRect.getMidX()
                };
            }
        }

        return {}; // nothing helped
    }

} // namespace Trajectory
