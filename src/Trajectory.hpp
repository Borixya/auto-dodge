#pragma once
#include "Physics.hpp"
#include <Geode/Geode.hpp>
#include <functional>
#include <vector>

using namespace cocos2d;
using namespace geode::cocos;

struct SceneObject {
    int    id;
    CCRect worldRect;
    bool   lethal;
    bool   isOrb;
    bool   isPad;
    bool   isDashOrb;
};

struct TrajectoryResult {
    bool   died = false;
    int    deathFrame = -1;
    std::vector<SimState> states;
};

struct OrbDecision {
    bool   shouldClick = false;
    int    orbObjectId = -1;
    float  activationX = 0.f;
};

namespace Trajectory {

    inline std::vector<SceneObject> buildSceneSnapshot(
        CCArray* objectLayerChildren,
        float playerX,
        float scanWidth)
    {
        std::vector<SceneObject> scene;
        if (!objectLayerChildren) return scene;

        for (auto* raw : CCArrayExt<CCObject*>(objectLayerChildren)) {
            auto go = dynamic_cast<GameObject*>(raw);
            if (!go || !go->isVisible()) continue;

            int id = go->m_objectID;
            bool lethal  = Hazards::isLethal(id);
            bool orb     = Hazards::isOrb(id);
            bool pad     = Hazards::isPad(id);
            bool dashOrb = Hazards::isDashOrb(id);

            if (!lethal && !orb && !pad) continue;

            CCRect bb = go->boundingBox();
            float objRight = bb.getMaxX();
            float objLeft  = bb.getMinX();
            if (objRight < playerX || objLeft > playerX + scanWidth) continue;

            scene.push_back({ id, bb, lethal, orb, pad, dashOrb });
        }
        return scene;
    }

    inline SimState applyPad(SimState s, const Hazards::PadInfo& pad) {
        s.vy = pad.vyOverride * (s.gravFlipped ? -1.f : 1.f);
        if (pad.flipGrav) {
            s.gravFlipped = !s.gravFlipped;
            s.vy = -s.vy;
        }
        s.onGround = false;
        return s;
    }

    inline SimState applyOrb(SimState s, const Hazards::OrbInfo& orb) {
        if (orb.flipGrav) s.gravFlipped = !s.gravFlipped;
        if (orb.vyOverride != 0.f)
            s.vy = orb.vyOverride * (s.gravFlipped ? -1.f : 1.f);
        if (orb.dashVX != 0.f) {
            s.vx = orb.dashVX;
            s.dashing = true;
            s.dashFrames = 12;
        }
        s.onGround = false;
        s.usedOrb = true;
        return s;
    }

    inline TrajectoryResult simulate(
        SimState                           initial,
        int                                steps,
        const std::vector<SceneObject>&    scene,
        std::function<bool(int)>           heldFn,
        bool                               clickOrbsAutomatically = false)
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

            CCRect playerBox = stateHitbox(s);

            for (const auto& obj : scene) {
                if (!obj.worldRect.intersectsRect(playerBox)) continue;

                if (obj.isPad) {
                    auto it = Hazards::PADS.find(obj.id);
                    if (it != Hazards::PADS.end()) {
                        s = applyPad(s, it->second);
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
                    result.died = true;
                    result.deathFrame = f;
                    result.states.push_back(s);
                    return result;
                }
            }

            if (s.dashing) {
                s.x += s.vx;
                s.dashFrames--;
                if (s.dashFrames <= 0) {
                    s.dashing = false;
                    s.vx = initial.vx;
                }
                result.states.push_back(s);
                continue;
            }

            s = stepPhysics(s, press, held, release);
            result.states.push_back(s);
        }
        return result;
    }

    inline OrbDecision evaluateOrbs(
        const SimState&                  startState,
        int                              steps,
        const std::vector<SceneObject>&  scene,
        const std::function<bool(int)>&  normalHeldFn)
    {
        auto baseResult = simulate(startState, steps, scene, normalHeldFn, false);
        if (!baseResult.died) return {};

        for (const auto& obj : scene) {
            if (!obj.isOrb) continue;
            auto orbIt = Hazards::ORBS.find(obj.id);
            if (orbIt == Hazards::ORBS.end()) continue;

            SimState boosted = applyOrb(startState, orbIt->second);
            auto boostedResult = simulate(boosted, steps, scene, normalHeldFn, false);

            if (!boostedResult.died) {
                return OrbDecision{ true, obj.id, obj.worldRect.getMidX() };
            }
        }
        return {};
    }

} // namespace Trajectory
