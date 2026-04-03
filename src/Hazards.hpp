#pragma once
#include <unordered_set>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
//  GD 2.2074 Object ID Classification Tables
// ─────────────────────────────────────────────────────────────────────────────

namespace Hazards {

    // ── Lethal Objects (kill player on touch) ─────────────────────────────────
    inline const std::unordered_set<int> LETHAL = {
        // Classic spikes
        8, 39, 103,
        // Spiked blocks / rectangular hazards
        1, 6, 7,
        // Rotating saw blades
        183, 184, 185, 186,
        // Long spikes / double spikes
        1337, 1338, 1339, 1340,
        // Lava / kill tiles (2.2)
        1329, 1330, 1331, 1332, 1333,
        // Pixel art spikes (2.2)
        1705, 1706, 1707, 1708,
        // Slope hazards
        461, 462, 463, 464, 465, 466, 467,
        // Small block spikes
        678, 679, 680, 681,
        // New 2.2 hazards
        3003, 3004, 3005,
    };

    // ── Solid Blocks (collide but don't kill) ─────────────────────────────────
    // Used for trajectory: player can land on these.
    inline const std::unordered_set<int> SOLID = {
        1, 2, 3, 4, 5,
        // Half blocks, slopes (passable from below in some cases)
        // We keep this minimal; the game engine handles detailed collision.
        // These are used by the trajectory sim to detect "floor" contacts.
        10, 11, 12, 193, 194, 208, 209, 210,
    };

    // ── Jump Orbs ──────────────────────────────────────────────────────────────
    enum class OrbEffect {
        JumpUp,         // Yellow orb
        JumpSmall,      // Pink orb
        JumpBig,        // Red orb
        GravFlip,       // Blue / Green orb
        GravFlipKick,   // Black orb (flip + upward kick)
        DashRight,      // Dash orb right (2.2)
        DashLeft,       // Dash orb left (2.2)
        Teleport,       // Teleport orb (spider)
    };

    struct OrbInfo {
        OrbEffect effect;
        float     vyOverride;   // velocity override, NaN = no override
        bool      flipGrav;
        float     dashVX;       // horizontal dash velocity (0 = no dash)
    };

    inline const std::unordered_map<int, OrbInfo> ORBS = {
        { 36,   { OrbEffect::JumpUp,       10.5f,  false, 0.f } }, // Yellow
        { 141,  { OrbEffect::JumpSmall,     7.5f,  false, 0.f } }, // Pink
        { 1022, { OrbEffect::JumpBig,      16.0f,  false, 0.f } }, // Red
        { 84,   { OrbEffect::GravFlip,     10.5f,  true,  0.f } }, // Blue
        { 1333, { OrbEffect::GravFlip,      7.0f,  true,  0.f } }, // Green
        { 1594, { OrbEffect::GravFlipKick, 10.5f,  true,  0.f } }, // Black
        { 1751, { OrbEffect::DashRight,     0.f,   false, 22.f} }, // Dash right (2.2)
        { 1752, { OrbEffect::DashLeft,      0.f,   false,-22.f} }, // Dash left  (2.2)
    };

    // ── Pads (auto-triggered on touch) ────────────────────────────────────────
    struct PadInfo {
        float vyOverride;
        bool  flipGrav;
    };

    inline const std::unordered_map<int, PadInfo> PADS = {
        { 35,   {  10.5f, false } }, // Yellow pad
        { 67,   {   7.5f, false } }, // Pink pad
        { 140,  {  16.0f, false } }, // Red pad
        { 1332, {   9.0f, true  } }, // Blue pad (gravity flip)
        { 1595, {   9.0f, false } }, // Spider pad (teleport, approximate)
    };

    // ── Speed portals ─────────────────────────────────────────────────────────
    // Object IDs → x-velocity per frame at 60 fps (approximate GD unit/frame)
    inline const std::unordered_map<int, float> SPEED_PORTALS = {
        { 200, 8.36f  }, // 0.9x
        { 201, 9.09f  }, // 1x (normal)
        { 202, 10.09f }, // 1.1x
        { 203, 11.36f }, // 1.2x
        { 1334,12.72f }, // 1.3x (2.2)
    };

    // ── Game Mode Portals ─────────────────────────────────────────────────────
    // Useful if you want to track mode changes during trajectory sim
    inline const std::unordered_map<int, int> MODE_PORTALS = {
        { 12, 0 }, // Cube
        { 13, 1 }, // Ship
        { 47, 2 }, // Ball
        { 111,3 }, // UFO
        { 660,4 }, // Wave
        { 745,5 }, // Robot
        { 1331,6}, // Spider
        { 2016,7}, // Swing (2.2)
    };

    // ── Gravity portals ───────────────────────────────────────────────────────
    inline const std::unordered_set<int> GRAVITY_FLIP_PORTALS = {
        10,   // Gravity flip (down→up)
        11,   // Gravity flip (up→down)
    };

    // ─────────────────────────────────────────────────────────────────────────
    inline bool isLethal(int id)   { return LETHAL.count(id) > 0; }
    inline bool isOrb(int id)      { return ORBS.count(id) > 0; }
    inline bool isDashOrb(int id) {
        auto it = ORBS.find(id);
        return it != ORBS.end() &&
               (it->second.effect == OrbEffect::DashRight ||
                it->second.effect == OrbEffect::DashLeft);
    }
    inline bool isPad(int id)      { return PADS.count(id) > 0; }

} // namespace Hazards
