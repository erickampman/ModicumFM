# ModicumFM roadmap — hiding FM complexity

ModicumFM is one 6-operator FM engine with a full "expert" operator panel. The plan is to layer
simpler controls on top of it without taking the expert panel away. Four ideas are planned; this
file records what's already in place for each and where the rest plugs in.

| # | Idea | Status |
|---|------|--------|
| 1 | Role-based macros (Brightness, Bite, Grit, Body) | done (Voice tab → Character) |
| 2 | Shared brightness envelope | done (relative envelopes, Voice tab) |
| 6 | Responsiveness (Touch, Key tracking, Realism) | done except Realism |
| 5 | Instrument templates with their own knobs | not started |

Suggested order: 1 → 2 → 6 → 5. Templates are mostly *curated settings of the macros*, so they
come last.

## What's in place (step 0)

- **Operator roles.** `src/Module/FM6OpAlgorithms.hpp` holds the 32-algorithm table and
  `fm6OpRoles(algorithm)`. For each operator it gives `depth` (0 = carrier, N = modulator N hops
  from the output), plus `carrierCount` and `maxDepth`. It's header-only and `constexpr`, and
  shared by the engine (carrier-count normalisation), the UI (the "out" / "mod N" operator
  captions) and `test/pm_fm6op_test.cpp`. Every macro below acts on roles, not operator
  numbers, so it works on any algorithm.
- **One amplitude choke point.** In `ProcessFM6OpModule` each operator's per-sample amplitude is
  computed in one place:

  ```cpp
  double a = baseLevel[oi] * egv[oi];              // level × operator envelope
  if (ampIn[ch][oi]) { a *= ... ampDepth[oi]; }    // optional CV
  amp[ch][oi] = a;
  ```

  Macro scaling (1), the shared brightness envelope (2) and velocity/key scaling (6) are all
  extra factors here. Their per-operator block constants go in the "hoist per-op params" loop
  above it, next to `baseLevel` and the EG coefficients.
- **Room for new params.**
  - FM6Op module items are append-only: add new ones before `FM6OpParamItemCount_` in
    `pm_params.h`, with matching rows in `fm6OpConstants` (`pm_param_data.cpp`).
    `MODULE_MAX_PARAMS` is 128 and the engine uses 95, and a `static_assert` guards the
    limit.
  - Plugin params are append-only too: add before `kParamCount` in `plugin/ModicumFMParams.h`, with a
    `kParams` spec row, a `kPresetKeys` row (`src/UI/PresetKeys.hpp`), a `vmAddress()` case in
    `ModicumFMPlugin.cpp` and a place in the UI layout. DPF uses the index as the host-facing id, so
    never reorder.
  - Presets load by `"module.item"` name, and missing keys keep their current value. So old
    presets stay valid as params are added. Give every new param a neutral default (one that
    changes nothing).

## 1 · Role-based macros

**Done.** Implemented in `FM6OpModule.cpp` (`fmMacros`, applied in the per-op hoist loop) as four
appended FM6Op items, shown on the Voice tab's Character panel. Tested in `pm_fm6op_test` (neutral
is bit-identical; each macro moves brightness the right way on the operators its role selects).
The design notes below are kept for reference.

New FM6Op items, each defaulting to neutral, with matching plugin params in a new "Character"
panel:

- **Brightness** (−1..+1, 0 = off): scales every *modulator's* level, more for deeper
  modulators, e.g. `scale = exp2(brightness * k * depth)` for `depth > 0`, capped so that
  level × scale ≤ 1. Carriers are untouched, so the loudness holds while the timbre moves.
- **Bite** (0..1): speeds up the modulators' attack and decay relative to the carriers, for a
  percussive transient. Implement it as a multiplier on `egAtkInc` / `egDecCoef` for operators
  with `depth > 0`.
- **Grit** (0..1): adds to feedback, and to the level of the highest-ratio modulator. That
  operator is found per block from the ratios plus the roles.
- **Body** (−1..+1): tilts the carriers' levels between low-ratio and high-ratio carriers.

Test: extend `pm_fm6op_test` with checks such as "Brightness > 0 raises the high-harmonic
energy on algorithm 1 and leaves algorithm 32 (all carriers) unchanged".

## 2 · Shared envelopes

**Done, as *relative* envelopes** (this replaced the original "Envelope Mode" plan, in which one
shared envelope would have *replaced* every carrier's / modulator's own — that loses the per-operator
differences good patches depend on, e.g. E.Piano's short tine vs. long body, and makes switching a
preset to Shared change its sound).

- Eight appended FM6Op items: Loudness (carriers) and Brightness (modulators) × Attack / Decay /
  Release multipliers (×0.01–×1000, neutral ×1; results capped at the operator limits, attack 20 s, decay/release 30 s — wide enough to sharpen a 1.5 s pad attack to 15 ms or stretch a 1 ms pluck attack to 1 s) and a Sustain offset (−1..+1, neutral 0; + toward full,
  − toward zero, so a sustain-0 pluck can be made to hold). Applied by role in the per-op hoist loop
  of `ProcessFM6OpModule`, alongside Bite and Key Track. Neutral is bit-identical (tested).
- UI: two `EnvelopeGraph`s in **relative mode** on the Voice tab. Each draws a representative
  operator's envelope as it sounds — the loudest carrier / strongest modulator — fed in through
  `setBase()`; a drag moves the effective point and stores the multiplier / offset. Brightness is
  disabled on algorithms with no modulators. A **Reset** button returns the Character macros and
  both envelopes to neutral.

## 6 · Responsiveness

**Done: velocity → level, Touch and Key Track** (Response panel). Velocity → level is two indexed
FM6Op items applied in the MIDI proc (`fmVelocityLevel`); Touch and Key Track are applied per
block in `ProcessFM6OpModule` next to the Character macros, and tested in `pm_fm6op_test`.
**Realism** is still open. The original notes, all inside FM6Op: The note and velocity are already captured in the MIDI proc (`st->f0`,
`st->velAmp`).

- **Touch** (0..1): velocity scales the modulators (brightness). Today velocity only scales the
  output.
- **Key Track** (−1..+1): note number scales the modulator levels (darker up the keyboard) and
  the envelope rates (faster decays up the keyboard). Compute it once per note-on.
- **Realism** (0..1): a small random pitch and index offset per note, drawn at note-on. It needs
  a per-voice RNG in the state.

## 5 · Instrument templates

A template is a curated starting patch (an algorithm plus a ratio family) together with 4–6
named knobs, each mapped onto one or more underlying params or macros with a curve (for example
E-Piano: Hammer, Tine, Bark, Decay).

- **Data:** `Templates/*.json`, compiled in by a `gen_templates.py` step, the same way
  `gen_presets.py` handles `Presets/*.json`. Each file holds the base patch (same schema as a
  preset) and the knob list: `{name, targets: [{key: "FM6Op.op2Level", from, to, curve}]}`.
- **Params:** append **Template** (index) and **Template Knob 1..6** (0..1) to `ModicumFMParams.h`.
  The mapping runs DSP-side in `ModicumFMPlugin::applyParam`: a knob change writes the underlying
  VoiceManager params. That way template knobs automate like any other param, and the expert
  panel still shows the true values.
- **UI:** on the Voice tab, in the Character panel (the space under the shared envelopes); the
  FM Engine panel is shared by both tabs.
- **Open question:** when a user edits an underlying param after tweaking a template knob, does
  the knob re-derive from the params, or stay where it is? The simplest rule is that knobs are
  relative offsets from the template's base patch, and choosing a template resets them.
