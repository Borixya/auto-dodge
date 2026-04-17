# Auto Dodge v2.0 — Geode Mod for GD 2.2081

Full trajectory-based auto-dodge with multi-mode support, orb/pad integration,
dash orbs, and a configurable per-mode HUD.

---

## Game Mode Support

| Mode   | Behavior |
|--------|----------|
| **Cube**   | Simulates jump vs no-jump trajectory; jumps at optimal frame |
| **Ship**   | Continuously evaluates hold-up vs release; picks safe corridor |
| **Ball**   | Detects when a gravity flip avoids death; taps at correct moment |
| **Wave**   | Compares up-diagonal vs down-diagonal; switches to avoid spikes |
| **Robot**  | Calculates minimum charge needed to clear gap; releases at peak |
| **Swing**  | Same continuous evaluation as Ship, reversed gravity aware |

---

## Orb / Pad / Dash Orb System

### Jump Orbs (click-triggered)
| Object ID | Type        | Effect |
|-----------|-------------|--------|
| 36        | Yellow orb  | Strong upward jump |
| 141       | Pink orb    | Small upward jump |
| 1022      | Red orb     | Large upward jump |
| 84        | Blue orb    | Flip gravity + kick |
| 1333      | Green orb   | Flip gravity (softer) |
| 1594      | Black orb   | Flip gravity + strong kick |
| 1751      | Dash → right | Horizontal dash right (2.2) |
| 1752      | Dash → left  | Horizontal dash left (2.2) |

### Pads (auto-triggered on contact)
| Object ID | Type       | Effect |
|-----------|------------|--------|
| 35        | Yellow pad | Jump boost |
| 67        | Pink pad   | Small boost |
| 140       | Red pad    | Large boost |
| 1332      | Blue pad   | Gravity flip |

Pads are automatically factored into the trajectory simulation — the bot
knows the velocity change they cause and accounts for it mid-simulation.

For **jump orbs**, the bot evaluates: "does clicking this orb now lead to a
safe N-frame trajectory?" If yes, it fires a one-shot click. It will not click
orbs that make things worse.

---

## Trajectory Engine

The core simulation runs `N` physics steps forward (default 90 = 1.5 seconds).
Each step uses GD's real physics constants (gravity, thrust, max fall speed,
wave angle) per game mode.

For each possible input strategy:

```
Strategy A: hold button for all N frames  →  simulate  →  dies? score = -1
Strategy B: release for all N frames      →  simulate  →  safe? score = +1
Strategy C: switch at frame K             →  simulate  →  …
```

The first safe strategy is chosen. Robot mode uses a binary search over charge
duration to find the minimum jump needed to clear the detected gap.

---

## HUD Status Labels

| Label          | Meaning |
|----------------|---------|
| `SAFE`         | No hazard detected in lookahead window |
| `CUBE:JUMP`    | Issuing jump to clear a spike |
| `SHIP:UP/DOWN` | Holding or releasing to navigate a gap |
| `WAVE:UP/DOWN` | Diagonal direction chosen to dodge |
| `ROBOT:CHARGE` | Holding to charge jump |
| `ROBOT:FIRE`   | Releasing to launch at optimal arc |
| `BALL:FLIP`    | Tapping to flip gravity |
| `SWING:UP/DOWN`| Swinging to avoid ceiling/floor |
| `ORB!`         | Clicking an orb that leads to a safe path |
| `DISABLED`     | This game mode is turned off in settings |

---

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --config RelWithDebInfo
```

Output: `build/auto-dodge.geode` → drop into `Geometry Dash/geode/mods/`

---

## Settings

| Key                  | Default | Description |
|----------------------|---------|-------------|
| `enabled`            | true    | Master switch |
| `trajectory-steps`   | 90      | Frames simulated ahead (30–240) |
| `look-ahead-px`      | 80      | Extra scan range for orbs |
| `use-orbs`           | true    | Allow clicking jump orbs |
| `use-dash-orbs`      | true    | Allow dash orb activation |
| `mode-cube/ship/…`   | true    | Enable per game mode |
| `show-overlay`       | true    | HUD label |
| `show-trajectory`    | false   | Draw green/red simulation dots (debug) |

---

## Limitations & Notes

- **Hitboxes are AABB** — rotated spikes (e.g. diagonal rows) are approximated
  by their axis-aligned bounding box. Near-miss false positives can occur on
  very precise layouts.
- **Dual player** — only Player 1 is controlled. P2 support can be added by
  duplicating the hook with `m_player2`.
- **Very high speeds** — at 4x/5x speed, increase `trajectory-steps` to 180+
  to give the sim enough runway to detect hazards in time.
- **Wave mode** — GD's wave hitbox shrinks at higher speeds; the bot may
  thread tighter gaps correctly at lower speeds but clip at extreme speeds.
- **Spider mode** — treated as a ball with a teleport approximation (instant
  gravity flip to the opposite surface). Actual teleport destination logic
  requires reading portal data which varies by level layout.

---

## License

MIT
