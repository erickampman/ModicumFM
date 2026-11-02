<div style="display: flex; align-items: center; gap: 0.75rem; margin: 1.5rem 0 0.5rem;">
  <a href="https://www.unlikelyware.com" target="_blank" rel="noopener noreferrer">
    <img src="Unlikelyware.png" alt="Unlikelyware" style="width: 56px; height: auto; border-radius: 10px; margin: 0; display: block;">
  </a>
  <h1 style="margin: 0;">ModicumFM</h1>
</div>

<p>
  <em>ModicumFM is a CLAP and VST3 plugin on macOS and Linux, and an AUv2 plugin on macOS. A 6-operator FM synthesizer.</em>
</p>

::: {.callout .callout-note .card}
## On This Page
[Overview](#overview) · [Voice and Operators](#tabs)

**Voice tab —** [Character](#character) · [Expression](#expression) · [Pitch](#pitch)

**The engine —** [FM Engine](#engine) · [Operators](#operators) · [Low-Pass Filter](#filter)

**Playing and output —** [Response](#response) · [Chorus](#chorus) · [Global](#global) · [Presets](#presets)
:::

::: {.callout .callout-note .card}
## Overview {#overview}
Each voice is one six-operator FM engine, then a resonant low-pass filter and a chorus:

`FM Engine → Low-Pass Filter → Chorus → Master`

There is no separate amp envelope: the *carriers'* own envelopes shape each note's loudness, as on classic FM synths. Pitch bend moves the engine; pressure (aftertouch) and timbre (CC 74) do what the Voice tab's Expression panel says.
:::

::: {.callout .callout-note .card}
## Voice and Operators {#tabs}
The **Voice | Operators** tabs at the top switch the large right-hand panel between the simple controls (**Voice**: Character) and the full engine (**Operators**, shown when the plugin opens). Every other panel — FM Engine, the filter, Response, Chorus and Global — is on both.
:::

::: {.callout .callout-note .card}
## Voice: Character {#character}
Four broad controls that reshape the whole sound at once, and Realism. The four act on operators by their *role* in the current algorithm, so it means the same thing whatever the algorithm. Hover a knob to see the operators it affects ringed in the FM Engine diagram. At the default (0) each leaves the sound exactly as the operators set it; double-click returns it there.

- **Brightness:** Turns every modulator up (brighter, more harmonics) or down (purer, closer to sine waves). Where the carriers sit at different ratios — e.g. the drawbar-style Organ — it also turns the higher ones up or down, like pulling out the upper drawbars.
- **Bite:** A sharper attack: the modulators hit harder but fade faster and settle lower, so each note starts with a brief burst of brightness while the carriers' envelopes stay the same — a plucked or struck edge.
- **Grit:** Adds feedback and drives the highest-ratio modulator harder, for a rougher, buzzier edge.
- **Body:** Warm and wide (up) or thin and focused (down). Turned up, the low-ratio carriers (fundamental, body) get louder and the high-ratio ones softer, the carriers drift slightly apart in pitch, and the left/right detune widens — a thicker, ensemble-like sound. Turned down, the high-ratio carriers come forward and the stereo detune narrows, to mono at the far end. (The loud/soft balance needs carriers at different ratios; the width works on every sound.)
- **Realism:** Makes each note a little different, as no two notes on a real instrument are quite the same. Every note you play gets its own small, random tuning (up to about 6 cents sharp or flat at full) and brightness (the modulators up to about 11% up or down), chosen when the note starts and held for the whole note; most notes land near the middle. A legato note at the same pitch keeps the last note's. At 0 every note plays exactly as set. A little goes a long way: try 0.2–0.4 on keys and plucks; at full, chords sound slightly out of tune, on purpose.

**Loudness envelope** and **Brightness envelope** (under the knobs) reshape every operator's envelope at once without opening the Operators tab: Loudness acts on all the carriers (how the sound swells and fades), Brightness on all the modulators (how the tone opens and closes). Each graph draws a representative operator's envelope — the loudest carrier, the strongest modulator — as it sounds now; drag its points as on any envelope graph. Behind the scenes the times are *multiplied* for every operator of that kind (the readout shows e.g. `×3.0`), so operators keep their differences — E.Piano's tine stays shorter than its body. The range is wide (×0.01 to ×1000): enough to give a slow pad a sharp attack, or to stretch a pluck into a pad. Sustain is moved toward full (up) or toward zero (down), so even a plucked sound can be made to hold. Brightness is greyed out on algorithms with no modulators.

**Reset** (top right of the Character panel) returns the five knobs and both envelopes to neutral, so they no longer change the sound. Double-click any single knob or envelope point to reset just that one.
:::

::: {.callout .callout-note .card}
## Voice: Expression {#expression}
What your controller's **pressure** (aftertouch) and **timbre** (CC 74, the MPE "slide") do to a sounding note. Rows are the source, columns what it moves. Every knob is 0 by default, so nothing happens until you turn one; with MPE or polyphonic aftertouch each note follows its own.

- **Bright:** The modulators' levels, i.e. how bright the FM tone is: at +1, full pressure (or timbre) brightens as far as the Character Brightness knob's top end; at −1, it darkens.
- **Cutoff:** The low-pass filter's cutoff, in octaves at full pressure / timbre. Turn the filter's Cutoff down first, or there is nothing to open.
- **Level:** (Pressure only) the carriers' level, for swells. Positive: notes start quiet and swell as you press (at +1, silent until pressed). Negative: pressing makes them quieter. A controller that sends no pressure stays silent at +1.

Pressure is channel aftertouch, polyphonic aftertouch, or a host's per-note pressure (Bitwig); timbre is CC 74, MIDI 2.0 per-note controller 74, or a host's per-note brightness. These are part of the sound, so presets store them; Character's Reset leaves them alone.
:::

::: {.callout .callout-note .card}
## Voice: Pitch {#pitch}
**Glide** (Mono only; greyed out in Poly, which never glides):

- **Time:** Portamento: each new note slides from the current pitch at this rate, in time per octave, so a two-octave jump takes twice as long as one. `Off` (the minimum) jumps straight to the note. Pitch bend and vibrato are not slowed by the glide.
- **Mode:** `Always`: every note glides, even after you let go of the last one. `Legato`: only a note played while the previous key is still held glides; a detached note starts at its own pitch.

**Vibrato:**

- **Rate:** Speed, 0.5 to 10 Hz.
- **Depth:** Vibrato that is always there, up to 100 cents (a semitone) each way.
- **Wheel:** Extra depth the mod wheel adds at full, up to 100 cents. Leave Depth at 0 and turn this up for vibrato that comes in with the wheel.
- **Sync:** `Shared`: one LFO for every note, so they all wobble together (it restarts when all notes have ended). `Per Note`: each note's vibrato starts from the beginning when you play it, so notes played at different times wobble out of step. In Mono, a note played over a held one keeps the running vibrato.
:::

::: {.callout .callout-note .card}
## FM Engine {#engine}
Six sine-based operators. An *algorithm* decides which operators modulate which: a *carrier* is heard, a *modulator* bends a carrier's phase to add harmonics. Each operator row's caption shows its role in the current algorithm: **out** (a carrier, shown in green) or **mod N** (a modulator N steps away from the output).

- **Algorithm:** Operator routing — the 32 classic six-operator algorithms, shown as a diagram: **filled** boxes are carriers (heard, joined by the output line at the bottom), **outlined** boxes are modulators, stacked above the operators they modulate. The small loop marks the feedback path. Click the diagram to choose from all 32 (click outside the chooser or press Esc to close it); the mouse wheel over it steps to the previous / next algorithm. Low numbers are deep stacks (bright, complex); high numbers are mostly carriers (organ-like, additive; 32 is all six in parallel). Levels are balanced by the number of carriers, so switching algorithm doesn't jump the volume.
- **Feedback:** Sits beside the diagram because the algorithm decides where it goes (the loop lights up when Feedback is above zero): one operator per algorithm modulates itself (in algorithms 4 and 6 a small loop of operators) — from a brighter sine toward a buzzy, saw-like tone.
- **Pitch / Fine:** Transpose the whole engine in semitones / cents.
- **Detune:** Stereo detune: left and right are tuned apart by up to 50 cents in total, for width and movement.
:::

::: {.callout .callout-note .card}
## Operators {#operators}
Each operator row:

- **Wave:** Sine, or a small harmonic stack (`1+2`, `1+3`, `1+3+5`, `1-4`) for richer modulators and carriers.
- **Ratio:** Operator frequency as a multiple of the note. A plain drag (or the mouse wheel) steps through the harmonic ratios 0.5, 1, 2 … 16, which give clean, pitched tones. **Shift**-drag moves the ratio smoothly instead (hold **Ctrl** too for very fine steps; Shift + wheel nudges it): small offsets make slow beating, larger in-between ratios such as 1.41 or 3.5 give bells and metallic sounds. Off-grid ratios show three decimals. Double-click snaps back to the nearest harmonic ratio; double-click again for 1. (A fine-tune offset in cents, from older presets or automation, shows as e.g. `+7c` and is cleared by double-click.)
- **Level:** For a carrier, its loudness. For a modulator, the modulation index — how many harmonics it adds.
- **Envelope:** The operator's own envelope, drawn as a graph. On a carrier it is the loudness contour; on a modulator it shapes the *timbre* over time (e.g. a bright attack that mellows). A voice is freed once its carriers have faded out.

Envelope graphs (operators and the filter envelope) are edited by dragging their points:

- **Delay:** (operators only) the end of the flat start — drag sideways to make the operator come in late. Dimmed while it is zero.
- **Attack:** the peak — drag sideways.
- **Decay / Sustain:** the corner into the held part — sideways sets the decay time, up/down the sustain level.
- **Release:** the end — drag sideways.

Hold **Ctrl** while dragging for fine changes. **Shift**-click or double-click a point to reset it. Hovering or dragging shows the point's value. The time axis is compressed so very short and very long stages both fit; the flat part in the middle stands for the held note. Decay and Release are the time to fade by 60 dB.
:::

::: {.callout .callout-note .card}
## Low-Pass Filter + Filter Envelope {#filter}
- **Cutoff:** Filter frequency. Everything above it is rolled off (24 dB/octave).
- **Resonance:** Emphasis at the cutoff.
- **Env Amount:** How far (in octaves) the Filter Envelope moves the cutoff; negative values sweep it down.
- **Filter Envelope:** Triggered by each note; a graph edited like the operator envelopes (above), without the Delay point.
:::

::: {.callout .callout-note .card}
## Response {#response}
How the way you play shapes each note: velocity sets the loudness (Vel Floor, Vel Curve) and, with Touch, the tone; Key Track changes the tone and length up and down the keyboard.

- **Vel Floor:** The loudness of the softest possible note, as a share of the loudest: `0%` (full velocity range) up to `75%`; `Fixed` ignores velocity entirely.
- **Vel Curve:** How loudness grows between the floor and full: `Concave` gets loud early (light touch), `Linear` is even, `Convex` saves the loudest levels for hard playing (heavy touch).
- **Touch:** Lets velocity change the *tone* as well as the loudness: play harder and the sound gets brighter, softer and it gets mellower — what makes an electric piano or brass patch feel alive. 0 = velocity only changes the loudness.
- **Key Track:** Changes the sound across the keyboard. Turned up, higher notes get darker and die away faster (like most acoustic instruments); turned down, the reverse. 0 = every note the same. Middle C is the pivot.
:::

::: {.callout .callout-note .card}
## Chorus {#chorus}
- **Mix:** Dry / wet balance.
- **Rate:** Speed of the chorus sweep. One LFO drives every voice, so all notes sweep together; left and right run 90° apart for width.
- **Depth:** How far the delay sweeps (milliseconds).
- **Delay:** Base delay time — shorter is tighter, longer is lusher.
- **Feedback:** Feeds the chorus back into itself for a more resonant, flanger-like colour.
:::

::: {.callout .callout-note .card}
## Global {#global}
- **Voices:** Poly (10 voices) or Mono (legato: a note played while another is held changes pitch without re-attacking the envelopes; Glide on the Voice tab's Pitch panel slides between notes).
- **MPE:** On for an MPE controller (lower zone: channel 1 is the master channel, 2–16 carry one note each). Each note bends ±48 semitones on its own channel, and the master channel's bend (the Bend Range) moves every note. Presets leave this switch alone.
- **Bend Range:** Pitch-bend range, up to 4 semitones each way. `Off` ignores pitch bend.
- **Quality:** Normal / High / Best: runs the FM engine at 1× / 2× / 4× the sample rate, reducing harsh aliasing on bright, high-modulation sounds at extra CPU cost.
- **Master:** Output level.
:::

::: {.callout .callout-note .card}
## Presets {#presets}
The **Preset** menu next to the title recalls a factory patch, applying every parameter it specifies at once. Parameters a preset doesn't mention are left at their current value.

**Save** and **Load**, left of the Help button, write the patch to a file and read one back, through the system's file dialog. The first time, the dialog opens on **Documents/ModicumFM**; after that, on the last folder you used.

- A saved patch has every sound parameter and leaves out MPE; loading one sets those parameters and leaves MPE alone.
- The preset menu then shows the patch's name, dimmed.
- The file is the same JSON as the factory presets, so a patch you save can be put in the source's `Presets` folder to build it in as a factory preset.
- The host still saves the patch with its project, whether or not you saved it to a file.
:::
