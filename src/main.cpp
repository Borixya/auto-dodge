/*  Auto Dodge v2.0 – Geode Mod for Geometry Dash 2.2074
 *  Geode v4.20 | C++20
 *
 *  Supports:  Cube · Ship · Ball · Wave · Robot · Swing
 *  Features:  Trajectory simulation · Orbs · Dash orbs · Pads · HUD overlay
 *
 *  Per-frame pipeline
 *  ──────────────────
 *  1. Snapshot visible objects in [playerX, playerX + scanRange]
 *  2. Build current SimState from PlayerObject live data
 *  3. For each candidate input strategy (hold / don't hold / jump / charge):
 *       simulate N frames, check for lethal collisions
 *  4. Pick the safest strategy and issue handleButton() accordingly
 *  5. Evaluate orbs: if no strategy avoids death, check if clicking an orb
 *       in range produces a safe trajectory → issue a one-shot click
 *  6. Update HUD overlay and optional trajectory debug dots
 */

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <cocos2d.h>

#include "Hazards.hpp"
#include "Physics.hpp"
#include "Trajectory.hpp"

using namespace geode::prelude;
using namespace cocos2d;

// ─────────────────────────────────────────────────────────────────────────────
//  Settings helpers
// ─────────────────────────────────────────────────────────────────────────────
static bool  cfg_enabled()     { return Mod::get()->getSettingValue<bool>("enabled"); }
static int   cfg_steps()       { return Mod::get()->getSettingValue<int64_t>("trajectory-steps"); }
static float cfg_lookAhead()   { return Mod::get()->getSettingValue<float>("look-ahead-px"); }
static bool  cfg_useOrbs()     { return Mod::get()->getSettingValue<bool>("use-orbs"); }
static bool  cfg_useDash()     { return Mod::get()->getSettingValue<bool>("use-dash-orbs"); }
static bool  cfg_showOverlay() { return Mod::get()->getSettingValue<bool>("show-overlay"); }
static bool  cfg_showTraj()    { return Mod::get()->getSettingValue<bool>("show-trajectory"); }

static bool cfg_modeEnabled(GDMode m) {
    switch (m) {
        case GDMode::Cube:   return Mod::get()->getSettingValue<bool>("mode-cube");
        case GDMode::Ship:   return Mod::get()->getSettingValue<bool>("mode-ship");
        case GDMode::Ball:   return Mod::get()->getSettingValue<bool>("mode-ball");
        case GDMode::Wave:   return Mod::get()->getSettingValue<bool>("mode-wave");
        case GDMode::Robot:  return Mod::get()->getSettingValue<bool>("mode-robot");
        case GDMode::Swing:  return Mod::get()->getSettingValue<bool>("mode-swing");
        default: return true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-level Runtime State
// ─────────────────────────────────────────────────────────────────────────────
struct RuntimeState {
    // Button state
    bool syntheticHeld     = false;   // we are holding the button right now
    bool wasHolding        = false;   // held last frame (for edge detection)
    int  jumpCooldown      = 0;       // frames until next jump decision
    int  robotChargeFrames = 0;       // robot: tracked charge duration

    // Orb state
    bool orbClickPending   = false;   // we need to fire a one-shot click
    int  orbClickId        = -1;      // ID of the orb we're clicking
    int  orbClickTimer     = 0;       // frames until we can click another

    // Trajectory debug dots
    std::vector<CCDrawNode*> debugDots;

    void reset() {
        syntheticHeld     = false;
        wasHolding        = false;
        jumpCooldown      = 0;
        robotChargeFrames = 0;
        orbClickPending   = false;
        orbClickId        = -1;
        orbClickTimer     = 0;
        clearDebugDots();
    }

    void clearDebugDots() {
        for (auto* d : debugDots) {
            if (d) d->removeFromParent();
        }
        debugDots.clear();
    }
};

static RuntimeState g_rt;

// ─────────────────────────────────────────────────────────────────────────────
//  HUD Overlay
// ─────────────────────────────────────────────────────────────────────────────
static CCLabelBMFont* g_hud = nullptr;

static void createHUD(CCLayer* uiLayer) {
    if (g_hud) { g_hud->removeFromParent(); g_hud = nullptr; }
    if (!cfg_showOverlay() || !uiLayer) return;

    g_hud = CCLabelBMFont::create("AUTO DODGE", "bigFont.fnt");
    g_hud->setScale(0.30f);
    g_hud->setOpacity(200);
    g_hud->setColor({ 100, 230, 100 });

    CCSize ws = CCDirector::sharedDirector()->getWinSize();
    g_hud->setPosition({ ws.width * 0.5f, ws.height - 14.f });
    g_hud->setZOrder(200);
    uiLayer->addChild(g_hud);
}

static void updateHUD(const char* status, ccColor3B color) {
    if (!g_hud) return;
    g_hud->setString(status);
    g_hud->setColor(color);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Debug trajectory dots
// ─────────────────────────────────────────────────────────────────────────────
static void drawTrajectoryDots(
    CCLayer* objLayer,
    const std::vector<SimState>& states,
    bool died)
{
    g_rt.clearDebugDots();
    if (!cfg_showTraj() || !objLayer) return;

    for (size_t i = 0; i < states.size(); i += 3) {
        auto* dot = CCDrawNode::create();
        ccColor4F col = died
            ? ccColor4F{1.f, 0.2f, 0.2f, 0.7f}
            : ccColor4F{0.2f, 1.f, 0.5f, 0.7f};
        dot->drawDot({ states[i].x, states[i].y }, 2.f, col);
        dot->setZOrder(300);
        objLayer->addChild(dot);
        g_rt.debugDots.push_back(dot);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Build SimState from live PlayerObject
// ─────────────────────────────────────────────────────────────────────────────
static SimState buildSimState(PlayerObject* player, GDMode mode) {
    SimState s;
    s.x           = player->getPositionX();
    s.y           = player->getPositionY();
    s.vx = player->m_playerSpeed;
    s.vy = 0.f;
    s.gravFlipped = player->m_isUpsideDown;
    s.onGround    = player->m_isOnGround;
    s.buttonHeld  = g_rt.syntheticHeld;
    s.robotCharge = g_rt.robotChargeFrames;
    s.mode        = mode;
    s.dead        = false;
    s.usedOrb     = false;

    // For wave mode, vx might be 0 in the struct; use level speed instead.
    // GD stores the speed internally; we read it from m_xVelocity on the player.
    if (mode == GDMode::Wave && std::abs(s.vx) < 0.5f) {
        s.vx = 9.09f; // default 1x speed in GD units/frame
    }

    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-mode input strategy evaluation
//
//  Each mode returns a recommended "hold" decision (true = press/hold, false = release).
//  The decision is based on comparing trajectories of both options.
// ─────────────────────────────────────────────────────────────────────────────
struct Decision {
    bool hold      = false;   // recommended button held state
    bool forceClick= false;   // robot: release to fire charged jump
    bool doOrbClick= false;   // one-shot click for an orb
    const char* hudText = "SAFE";
    ccColor3B   hudColor = {100, 230, 100};
};

static Decision evaluateInput(
    GDMode mode,
    const SimState& state,
    const std::vector<SceneObject>& scene,
    int steps)
{
    Decision dec;

    // Helper lambdas for trajectory tests
    auto holdTraj = [&]() {
        return Trajectory::simulate(state, steps, scene,
            [](int) { return true; }, false);
    };
    auto releaseTraj = [&]() {
        return Trajectory::simulate(state, steps, scene,
            [](int) { return false; }, false);
    };
    auto altTraj = [&](bool startHeld, int switchFrame) {
        return Trajectory::simulate(state, steps, scene,
            [startHeld, switchFrame](int f) {
                return (f < switchFrame) ? startHeld : !startHeld;
            }, false);
    };

    switch (mode) {

    // ── Cube ──────────────────────────────────────────────────────────────────
    // Options: (a) jump now, (b) wait, (c) jump on next frame
    case GDMode::Cube: {
        auto noJump = releaseTraj();
        if (!noJump.died) {
            dec.hold = false;
            dec.hudText = "CUBE:SAFE";
            dec.hudColor = {100,230,100};
            break;
        }
        // Try jumping immediately
        SimState jumped = state;
        if (state.onGround) {
            auto& ph = physicsFor(mode);
            jumped.vy = ph.jumpVelocity * (state.gravFlipped ? -1.f : 1.f);
            jumped.onGround = false;
        }
        auto jumpTraj = Trajectory::simulate(jumped, steps, scene,
            [](int) { return false; }, false);

        if (!jumpTraj.died) {
            dec.hold = true; // press this frame
            dec.hudText = "CUBE:JUMP";
            dec.hudColor = {255,200,50};
        } else {
            // Try a late jump (hold for a couple frames to get over obstacle)
            auto lateTraj = altTraj(false, 3);
            dec.hold = !lateTraj.died;
            dec.hudText = "CUBE:DANGER";
            dec.hudColor = {255,80,80};
        }
        break;
    }

    // ── Ship ──────────────────────────────────────────────────────────────────
    // Options: hold up vs release (continuous input)
    case GDMode::Ship:
    case GDMode::Swing: {
        auto holdR  = holdTraj();
        auto releaseR = releaseTraj();

        if (!holdR.died && releaseR.died) {
            dec.hold = true;
            dec.hudText = (mode==GDMode::Ship) ? "SHIP:UP" : "SWING:UP";
            dec.hudColor = {100,180,255};
        } else if (holdR.died && !releaseR.died) {
            dec.hold = false;
            dec.hudText = (mode==GDMode::Ship) ? "SHIP:DOWN" : "SWING:DOWN";
            dec.hudColor = {100,230,100};
        } else if (!holdR.died && !releaseR.died) {
            // Both safe — prefer the one that keeps us closer to center
            // (simple heuristic: continue current state to avoid oscillation)
            dec.hold = state.buttonHeld;
            dec.hudText = "SHIP:FREE";
            dec.hudColor = {100,230,100};
        } else {
            // Both die — try alternating (dodge tight corridor)
            auto alt1 = altTraj(true, 4);
            auto alt2 = altTraj(false, 4);
            dec.hold = !alt1.died ? true : false;
            dec.hudText = "SHIP:TIGHT";
            dec.hudColor = {255,80,80};
        }
        break;
    }

    // ── Ball ──────────────────────────────────────────────────────────────────
    // Tap = flip gravity (only when on ground)
    case GDMode::Ball:
    case GDMode::Spider: {
        auto noFlip = releaseTraj();
        if (!noFlip.died) {
            dec.hold = false;
            dec.hudText = "BALL:SAFE";
            dec.hudColor = {100,230,100};
            break;
        }
        // Try flipping now
        SimState flipped = state;
        flipped.gravFlipped = !flipped.gravFlipped;
        flipped.vy = physicsFor(mode).jumpVelocity
                     * (flipped.gravFlipped ? 1.f : -1.f) * 0.5f;

        auto flipTraj = Trajectory::simulate(flipped, steps, scene,
            [](int) { return false; }, false);

        dec.hold = true; // issue tap
        dec.hudText = !flipTraj.died ? "BALL:FLIP" : "BALL:DANGER";
        dec.hudColor = !flipTraj.died ? ccColor3B{255,200,50} : ccColor3B{255,80,80};
        break;
    }

    // ── Wave ──────────────────────────────────────────────────────────────────
    // Holding = move up-right, release = move down-right
    case GDMode::Wave: {
        auto holdR    = holdTraj();
        auto releaseR = releaseTraj();

        if (!holdR.died && releaseR.died) {
            dec.hold = true;
            dec.hudText = "WAVE:UP";
            dec.hudColor = {180,100,255};
        } else if (holdR.died && !releaseR.died) {
            dec.hold = false;
            dec.hudText = "WAVE:DOWN";
            dec.hudColor = {100,230,100};
        } else if (!holdR.died) {
            // Both safe — keep current direction
            dec.hold = state.buttonHeld;
            dec.hudText = "WAVE:FREE";
            dec.hudColor = {100,230,100};
        } else {
            // Both dangerous — try rapid alternation
            dec.hold = !state.buttonHeld; // switch direction
            dec.hudText = "WAVE:DODGE";
            dec.hudColor = {255,140,0};
        }
        break;
    }

    // ── Robot ─────────────────────────────────────────────────────────────────
    // Hold to charge, release to fire. Optimal charge = just enough to clear.
    case GDMode::Robot: {
        auto noJump = releaseTraj();
        if (!noJump.died && g_rt.robotChargeFrames == 0) {
            dec.hold = false;
            dec.hudText = "ROBOT:SAFE";
            dec.hudColor = {100,230,100};
            break;
        }

        // Test: release the charge right now and see if we survive
        auto& ph = physicsFor(mode);
        float t = std::min(1.f, (float)g_rt.robotChargeFrames / ph.robotMaxCharge);
        float jumpVy = ph.robotMinJump + t * (ph.robotMaxJump - ph.robotMinJump);
        jumpVy *= state.gravFlipped ? -1.f : 1.f;

        SimState jumpedState = state;
        jumpedState.vy = jumpVy;
        jumpedState.onGround = false;
        jumpedState.robotCharge = 0;

        auto jumpTraj = Trajectory::simulate(jumpedState, steps, scene,
            [](int) { return false; }, false);

        if (!jumpTraj.died) {
            // Good time to release
            dec.hold       = false;
            dec.forceClick = true; // release the charge
            dec.hudText    = "ROBOT:FIRE";
            dec.hudColor   = {255,200,50};
        } else if (g_rt.robotChargeFrames < ph.robotMaxCharge) {
            // Keep charging
            dec.hold     = true;
            dec.hudText  = "ROBOT:CHARGE";
            dec.hudColor = {255,140,0};
        } else {
            // Max charge still dies — fire anyway
            dec.hold       = false;
            dec.forceClick = true;
            dec.hudText    = "ROBOT:PANIC";
            dec.hudColor   = {255,80,80};
        }
        break;
    }

    // ── UFO ───────────────────────────────────────────────────────────────────
    case GDMode::UFO: {
        auto holdR    = holdTraj();
        auto releaseR = releaseTraj();
        dec.hold      = !holdR.died ? false : true;
        dec.hudText   = (!holdR.died) ? "UFO:SAFE" : "UFO:BOOST";
        dec.hudColor  = (!holdR.died) ? ccColor3B{100,230,100} : ccColor3B{100,180,255};
        break;
    }

    default:
        break;
    }

    return dec;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main hook – PlayLayer
// ─────────────────────────────────────────────────────────────────────────────
class $modify(AutoDodgeLayer, PlayLayer) {

    // ── Init ──────────────────────────────────────────────────────────────────
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_rt.reset();
        g_hud = nullptr;
        return true;
    }

    // ── Level objects ready ───────────────────────────────────────────────────
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();
        g_rt.reset();
        if (cfg_enabled() && this->m_uiLayer)
            createHUD(this->m_uiLayer);
    }

    // ── Per-frame update ──────────────────────────────────────────────────────
    void update(float dt) {
        // ── Decide input BEFORE physics runs this frame ───────────────────────
        if (cfg_enabled()
            && this->m_player1
            && !this->m_player1->m_isDead
            && !this->m_isPaused)
        {
            processAutoDodge();
        }

        PlayLayer::update(dt);
    }

    void processAutoDodge() {
        PlayerObject* player = this->m_player1;
        
        // FIX: Changed m_gamemode to m_gameMode (capital M)
        GDMode mode = static_cast<GDMode>((int)this->m_player1->m_gameMode);
        
        if (!cfg_modeEnabled(mode)) {
            // Release any held input and bail
            if (g_rt.syntheticHeld) {
                this->handleButton(false, 1, false);
                g_rt.syntheticHeld = false;
            }
            updateHUD("DISABLED", {150,150,150});
            return;
        }

        // ── Snapshot the scene ────────────────────────────────────────────────
        float scanWidth = cfg_lookAhead()
            + static_cast<float>(cfg_steps()) * std::abs(this->m_player1->m_playerSpeed);

        // FIX: The broken `switch(this->m_level->m_startSpeed)` and `}();` block was completely removed. 
        // It was invalid syntax returning floats in a void function, and `m_playerSpeed` already handles the speed correctly.

        CCArray* children = this->m_objectLayer
                            ? this->m_objectLayer->getChildren()
                            : nullptr;

        auto scene = Trajectory::buildSceneSnapshot(
            children,
            player->getPositionX(),
            scanWidth
        );

        // ── Build current simulation state ────────────────────────────────────
        SimState state = buildSimState(player, mode);

        // ── Evaluate orbs ─────────────────────────────────────────────────────
        if (g_rt.orbClickTimer > 0) g_rt.orbClickTimer--;

        if ((cfg_useOrbs() || cfg_useDash()) && g_rt.orbClickTimer == 0) {
            auto normalHeld = [this](int) { return g_rt.syntheticHeld; };
            auto orbDec = Trajectory::evaluateOrbs(state, cfg_steps(), scene, normalHeld);

            if (orbDec.shouldClick) {
                bool isDash = Hazards::isDashOrb(orbDec.orbObjectId);
                if ((isDash && cfg_useDash()) || (!isDash && cfg_useOrbs())) {
                    // Issue a one-shot click (press + release next frame)
                    this->handleButton(true, 1, false);
                    g_rt.orbClickPending = true;
                    g_rt.orbClickId      = orbDec.orbObjectId;
                    g_rt.orbClickTimer   = 8;
                    updateHUD("ORB!", {255,230,50});
                    return; // Skip normal input this frame
                }
            }
        }

        // Release pending orb click
        if (g_rt.orbClickPending) {
            this->handleButton(false, 1, false);
            g_rt.orbClickPending = false;
            return;
        }

        // ── Evaluate movement decision ─────────────────────────────────────────
        if (g_rt.jumpCooldown > 0) {
            g_rt.jumpCooldown--;
        }

        Decision dec = evaluateInput(mode, state, scene, cfg_steps());

        // ── Track robot charge frames ──────────────────────────────────────────
        if (mode == GDMode::Robot) {
            if (dec.hold && state.onGround)
                g_rt.robotChargeFrames++;
            else if (!dec.hold)
                g_rt.robotChargeFrames = 0;
        }

        // ── Apply decision ────────────────────────────────────────────────────
        bool prevHeld = g_rt.syntheticHeld;

        if (dec.hold != prevHeld) {
            this->handleButton(dec.hold, 1, false);
            g_rt.syntheticHeld = dec.hold;

            // Cooldown after a jump to avoid double-tapping same spike
            if (dec.hold && !prevHeld &&
                (mode == GDMode::Cube || mode == GDMode::Ball))
            {
                g_rt.jumpCooldown = 8;
            }
        }

        // ── Draw trajectory debug ──────────────────────────────────────────────
        if (cfg_showTraj() && this->m_objectLayer) {
            auto debugTraj = Trajectory::simulate(state, cfg_steps(), scene,
                [heldVal = dec.hold](int) -> bool { return heldVal; }, false);
            drawTrajectoryDots(this->m_objectLayer, debugTraj.states, debugTraj.died);
        }

        // ── Update HUD ────────────────────────────────────────────────────────
        updateHUD(dec.hudText, dec.hudColor);
    }

    // ── Death / quit cleanup ──────────────────────────────────────────────────
    void onQuit() {
        if (g_rt.syntheticHeld)
            this->handleButton(false, 1, false);
        g_rt.reset();
        g_hud = nullptr;
        PlayLayer::onQuit();
    }

    void levelComplete() {
        if (g_rt.syntheticHeld) {
            this->handleButton(false, 1, false);
            g_rt.syntheticHeld = false;
        }
        g_rt.clearDebugDots();
        PlayLayer::levelComplete();
    }

    // ── Pause / resume ────────────────────────────────────────────────────────
    void pauseGame(bool p0) {
        if (g_rt.syntheticHeld) {
            this->handleButton(false, 1, false);
            g_rt.syntheticHeld = false;
        }
        PlayLayer::pauseGame(p0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────────
 $on_mod(Loaded) {
    log::info("[AutoDodge v2.0] Loaded – GD 2.2074 / Geode 4.20");
    log::info("  Modes: Cube={} Ship={} Ball={} Wave={} Robot={} Swing={}",
        Mod::get()->getSettingValue<bool>("mode-cube"),
        Mod::get()->getSettingValue<bool>("mode-ship"),
        Mod::get()->getSettingValue<bool>("mode-ball"),
        Mod::get()->getSettingValue<bool>("mode-wave"),
        Mod::get()->getSettingValue<bool>("mode-robot"),
        Mod::get()->getSettingValue<bool>("mode-swing")
    );
    log::info("  Trajectory steps: {}, UseOrbs: {}, UseDash: {}",
        (int)Mod::get()->getSettingValue<int64_t>("trajectory-steps"),
        Mod::get()->getSettingValue<bool>("use-orbs"),
        Mod::get()->getSettingValue<bool>("use-dash-orbs")
    );
}
