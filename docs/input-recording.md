# Input recordings

A recording is the input of one run of a scene, as plain text. Played back on the scene it was made for, it gives the same run again, and the checksum at its end proves it: that is what ADR-0002's determinism promise is for. Recordings are for reproducing a bug, for tests that need a long realistic input, and for checking in `marle-scene simulate` that a change did not alter how a scene plays.

```jsonc
{
  "version": 1,
  "scene": "The Sunken Orangerie",
  "sceneHash": "0123456789abcdef",
  "seed": 1,
  "stepsPerSecond": 60,
  "steps": 12,
  "checksum": "fedcba9876543210",
  "changes": [
    { "step": 0, "down": ["D", "W"], "pressed": ["D", "W"] },
    { "step": 3, "up": ["D"] },
    { "step": 4, "scroll": 1.5 },
    { "step": 5, "pressed": ["Q"] },
    { "step": 6, "up": ["W"] },
    { "step": 8, "down": ["Space"], "pressed": ["Space"], "scroll": -2 }
  ]
}
```

## What it records

One **input frame** per fixed step (see ADR-0002): the keys that are held, the keys newly pressed since the step before, and the mouse wheel movement. Nothing else about the machine or the clock is recorded, because behaviors can only read the input frame, the fixed step and the scene's seeded random numbers.

## The file

JSON, with comments allowed when reading. Unknown keys are errors.

| Key | |
|---|---|
| `version` | Required, `1`. |
| `scene` | Required. The scene's name. |
| `sceneHash` | Required. 16 lower-case hex digits: an FNV-1a 64-bit hash of the exact text of the scene file the recording was made on. |
| `seed` | Required. The scene's seed (`Random.seed`, 1 if it has none) when it was recorded. |
| `stepsPerSecond` | Required, 1 to 1000. The fixed step is `1 / stepsPerSecond` seconds. The engine runs at 60. |
| `steps` | Required, 0 to 1,000,000. How many fixed steps were recorded. |
| `checksum` | Optional, 16 hex digits. The scene's checksum after the last step; a replay compares its own to it. |
| `sceneFile` | Optional. The path of the scene file the recording was made on, so a reader knows what it stands for. Not used to play it (the hash is). |
| `description` | Optional. What the route does, in a sentence. |
| `expectedEvents` | Optional. Events the run must produce, in step order (see "Expected events"). |
| `changes` | Required. The steps where the input changed, in order, once each. |

**Changes.** A step is listed only when something about its input differs from the step before; every step not listed holds the same keys down and has nothing newly pressed and no scroll. A change has `step` and any of:

- `down`: keys that went down since the step before (they stay down until listed in `up`).
- `up`: keys that went up.
- `pressed`: keys newly pressed in this step. A key that goes down is normally `pressed` in the same step, and a tap that was released before the step is `pressed` but never `down`.
- `scroll`: the wheel movement in this step.

Keys are named as in scene files (`W`, `Space`, `LeftShift`, ... see the key names in `docs/scene-format.md`), in the order that list has them. A key cannot go down while it is down, go up while it is not, be listed twice, or go down and up in one step, and a change that changes nothing is an error. This describes every input frame exactly.

## Expected events

A checksum says that a run changed, not why. A golden recording can also list the events the run must produce (`Simulation` events, the ones `marle-scene simulate --report` prints; kinds and fields are in `docs/scene-format.md`), so a change that breaks the route shows up as "the player no longer enters the zone at step 246" and not only as a different checksum:

```jsonc
"expectedEvents": [
  { "step": 52,  "kind": "solid_stopped",   "entity": "player", "axis": "x" },
  { "step": 246, "kind": "trigger_entered", "entity": "zone", "other": "player" },
  { "step": 317, "kind": "trigger_left",    "entity": "zone", "other": "player" }
]
```

Every listed event must appear in the run with exactly these fields, in the order listed; the run may have other events between them, so list only what the route is about. `solid_stopped` has an `axis` (`"x"`, `"y"` or `"xy"`) and no `other`; the trigger kinds have `other` and no axis. Steps are inside the recording and not decreasing.

`marle-scene simulate --recording` checks them when the whole recording is played with its own seed (as it does the checksum): it prints which expected events did not happen and exits 1; with `--json` the object has `expected_events` (`count`, `missing`) and `matches_events`. The checksum and the events are separate checks, so when a checksum changes, the events tell you whether the route still does what it was for. The Sandbox does not record or check them; add them to a recording by hand or from `simulate --report`, and keep `sceneFile` and `description` with them.

## Starting point

A recording starts from the scene as it is loaded from its file, with its seed and nothing carried over. The Sandbox restarts the scene when you start recording (and a replay starts on a fresh load), so a replay always starts from a known state.

## Playing it back

Before a replay starts, the recording is checked against the scene it will play on, and refused with a message if it does not fit:

- another scene name, or
- **a different version of the scene**: the hash of the scene text is not the one in the recording. Any change to the file, even a space, is a different version, because the same input would then not give the same run. Record again, or run the scene as it was.
- another seed, or another step rate.

A replay then uses the recorded frames in place of the keyboard, one per fixed step, and at the end compares the scene's checksum with the one in the file. Equal checksums mean the run was reproduced exactly (on the same build and platform; floating-point results are not promised to match across platforms or compilers).

Reloading the scene while recording or replaying stops it, since the run no longer follows from the file the recording was made on. (Restarting the scene with its restart key does not: that is a fresh load of the same file, and happens at the same step in the replay.)

## The Sandbox

- **F5** starts a recording (the world restarts first) and stops it. It is saved to `recording.json` in the working directory when you stop, or when you quit while recording.
- **`--replay <file>`** plays a recording on startup instead of reading the keyboard. If it is refused, the reasons are logged and the Sandbox runs normally. When the replay ends, the log says whether the checksum matched, and the keyboard takes over again.
- The Scene panel shows what is recording or replaying.

## In code

`WriteRecording` and `ReadRecording` (`Scene/InputRecording.h`) turn an `InputRecording` into text and back, reporting every problem with a JSON Pointer path. `InputRecorder` collects frames as they are used, `InputReplay` gives them back, `RecordingMismatch` explains a refusal, and `ReplayRecording` plays a recording into a scene headlessly and returns the checksum.
