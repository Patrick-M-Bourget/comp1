# Compact View: which controls each Style should surface

Research for the plan in issue #196 (the Style-aware Compact View), gathered 2026-10-24. It answers one question per Style: on that kind of compressor, which controls besides the amount do people adjust, and what do simplified or one-knob compressor UIs keep? It then recommends the two Compact View slots for each Style. The recommendations are input for grilling #196, not decisions.

## Scope and method

**Fixed by the user, not researched.** Comp (Threshold and Ratio along the Comp Curve) is the hero in every Style, with the Gain Reduction meter. The slots draw only from Attack, Release, Auto Release, Hold, Knee, Range, the Gate (on plus Gate Threshold), the Detection EQ's High-pass, Colour, and Saturation (on, Drive, Saturation Position). Threshold and Ratio are out, because editing them turns Comp Follow off (ADR 0004). Dry and Wet, Output Gain, Auto Gain and the Limiter are out.

**Sources.** Manufacturer manuals only, each cited by URL on every claim:

- hardware classes, through the manufacturers' own plug-in manuals of them: Universal Audio's 1176, Teletronix LA-2A, SSL 4000 G Bus Compressor, dbx 160 and API 2500 manuals;
- software with simplified or few-control UIs: Waves Renaissance Vox and Renaissance Compressor, FabFilter Pro-C 3's online help, iZotope Neutron 4's Assistant documentation, Apple's Logic Pro Compressor documentation, and Klanghelm's DC1A3 manual.

No engineering text was needed; nothing below is a secondary source. Where a recommendation goes past what a manual says, it is marked **inference**.

**What the sources can and can't say.** A manual says which controls a unit exposes, which ones its maker tells users to set, and in what order. None gives usage statistics, and none was found that measures which controls users adjust most. "Most adjusted" below therefore means "exposed on the front panel and named in the manufacturer's operating guidance", never a measured frequency. Forum and blog claims about habits were not used.

**Mapping hardware to Styles.** ADR 0002 names Styles generically; hardware names appear here only as evidence. The Characters map to classes as the glossary and ADR 0002 describe them: Clean to clean digital compressors (Pro-C's Clean, Logic's Platinum Digital), VCA to VCA bus and console compressors (SSL 4000 G, API 2500, dbx 160), FET to the 1176, and Opto to the LA-2A. The Source Styles are judged by their job (vocal, instrument, bus, drums) and by the Character each plays in `engine/include/comp1/Style.h`: Vocal plays Clean, Instrument Opto, Bus VCA, Drums FET.

**Not found.** Pro-C 3's online help documents no knob or expert mode: its only reduced layout is Compact, which hides the level and knee displays and keeps every control ([Displays](https://www.fabfilter.com/help/pro-c/using/displays), [Overview](https://www.fabfilter.com/help/pro-c/using/overview)). iZotope's Nectar 4 Assistant documentation could not be retrieved (403 from iZotope's documentation host), so Nectar is not cited. Softube's manuals were not consulted.

## What the simplified UIs keep

The pattern across every few-control UI found, before the per-Style sections:

| UI | Amount | Other compression controls kept | Tone |
|---|---|---|---|
| Waves Renaissance Vox | Compression (threshold and ratio in one fader) | Gate threshold; Gain (limiter ceiling) | none |
| Klanghelm DC1A3 | Input | Deep (detection high-pass) and Relaxed (longer time constants, less saturation), both toggles; Negative, Dual Mono; Mix, Output | none separate; Relaxed lowers it |
| iZotope Neutron 4 Assistant, Punch | Amount | Attack, Sustain | separate Distort intent (Drive) |
| Teletronix LA-2A (UA) | Peak Reduction | Compress/Limit; Emphasis (detection high-frequency shelf) | none; the colour is the circuit's |
| UA 1176 | Input | Attack, Release, Ratio buttons | none; All Button mode and fastest timing give distortion |
| dbx 160 (UA) | Threshold | Compression (ratio); detection filter toggle | none |
| FabFilter Pro-C 3, Vocal style | Threshold | knee and ratio automatic | Character section, separate |

Sources: [Renaissance Vox](https://www.waves.com/1lib/pdf/plugins/renaissance-vox.pdf), [DC1A3 manual](https://klanghelm.com/docs/DC1A3-manual.pdf), [Neutron 4 Assistant](https://s3.amazonaws.com/izotopedownloads/docs/neutron4/en/assistant/index.html), [LA-2A](https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual), [1176](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual), [dbx 160](https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual), [Pro-C 3 styles](https://www.fabfilter.com/help/pro-c/using/styleandcharacter).

Three things recur. Timing (attack, release, or a time-constant switch) is the most common second control. A detection high-pass is the most common third, on the VCA and opto units and on DC1A. Hold, Knee and Range appear on none of these simplified UIs; Knee is exposed only on full-panel units (API 2500's Soft/Medium/Hard switch, Logic, Pro-C).

## Clean

**What the class exposes.** Pro-C 3's Clean style is "an allround, low distortion, feedforward, program dependent style" with the full control set: Threshold, Ratio, Knee, Range, Attack, Release, Auto Release, Hold, Lookahead ([Styles](https://www.fabfilter.com/help/pro-c/using/styleandcharacter), [Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols), [Dynamics controls](https://www.fabfilter.com/help/pro-c/using/dynamicscontrols)). Logic's Platinum Digital is "a clean-sounding compression type with a fast transient response", with Threshold, Ratio, Knee, Attack, Release and an Auto Release button ([Logic Compressor main parameters](https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac)).

**What users adjust.** Pro-C's help ties Attack to the material: "for transient-rich program material like drums, fast attack times are needed to minimize overshoot", while "for other program material, too short attack times may dull the sound or introduce audible distortion". With Auto Release on, "the Release knob will adjust the overall effect of the auto release feature on the release time" ([Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols)). Waves suggests keeping its auto release (ARC) "engaged at all times, unless you want a very specific type of limiter behavior", with Release as its scaling factor ([Renaissance Compressor](https://www.waves.com/1lib/pdf/plugins/renaissance-compressor.pdf), Release Mode). No source singles out Knee, Range or Hold as a first adjustment.

**Recommendation.** Slot A: **Attack**. Slot B: **Release + Auto Release**. Clean has no Colour (glossary, "Colour"), so a tone slot here would need Saturation, which no clean-digital source surfaces near the timing. Confidence: **medium**. The timing pair is the generic default every full-panel unit shares; nothing specific to clean compressors favours it over another pair.

## VCA

**What the class exposes.** The class varies more than any other:

- SSL 4000 G: Threshold, Ratio (2:1, 4:1, 10:1), Attack (six steps, 0.1 to 30 ms), Release (100 ms to 1.2 s, and Auto), Make Up; UA's plug-in adds a continuously variable detection high-pass (its "Sidechain Filter"), Mix and Headroom ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual)).
- API 2500: Threshold, Attack (seven steps), Ratio, Release (fixed steps or variable), Knee (Soft/Medium/Hard), Thrust (a detection high-pass, Normal/Medium/Loud), Type (feedforward or feedback) and Link ([API 2500](https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual)).
- dbx 160: Threshold, Compression (ratio) and Output Gain only, no Attack or Release at all; UA's plug-in adds a detection filter toggle and Mix ([dbx 160](https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual)).

Logic describes VCA compressors as able to "respond slowly or quickly to incoming transients" and tending "towards a clean tone" ([Logic Compressor main parameters](https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac)).

**What users adjust.** On the SSL, "attack and release times allow the compression behavior to be 'tuned' to the tempo and feel of the song", and "the Auto setting provides a program-dependent, multi-stage release for the greatest degree of transparency". UA added the detection high-pass because "if your bottom-end makes the G Bus Compressor pump, you've got too much bottom" ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual)). API's Attack note: slower attacks let "a signal's attack transients (for example, the pluck of a string)" through, "which can produce a punchier sound" ([API 2500](https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual)). All three UA VCA plug-ins carry a detection high-pass in some form (SSL's filter, API's Thrust, dbx's filter), the strongest recurrence of any control in this research.

**Recommendation.** Slot A: **Attack**. Slot B: **Release + Auto Release**. Runner-up: **High-pass** in place of Release, or as Slot A's smaller control if the slot shape is two controls. Confidence: **medium**. Attack and Release with Auto are what the SSL and API expose and what their manuals tell users to tune; the dbx 160 exposes neither, so the class does not speak with one voice.

## FET

**What the class exposes.** The 1176: Input, which "simultaneously sets compression threshold and the level of the signal entering the 1176"; Output; Attack (20 to 800 µs); Release (50 ms to 1.1 s); four Ratio buttons; and All Button mode. "Using an 1176 is a study in simplicity" ([1176](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual)). Its release is already "heavily program-dependent", with fast, slow and transition times built in (same manual, "About Program-Dependent Release"). Logic's FET circuits "are known for fast transient responses" and "can be pushed toward a 'crunchy' tone on transients" ([Logic Compressor main parameters](https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac)).

**What users adjust.** Beyond the amount (Input) and Output, Attack and Release are the 1176's only continuous controls. The manual tells users to "adjust Release gradually ... small adjustments can make a significant difference", and describes slower attack letting transients pass. Its distortion comes from timing and All Button mode: "turning the attack and release up all the way to their fastest settings ... has the audible effect of adding compression distortion"; in All Button mode "distortion increases radically" (same manual, "Grit" and "All Button Mode").

**Recommendation.** Slot A: **Attack**. Slot B: **Release**. FET's default Auto Release is off (`Style.h`), and the hardware's release behaviour lives in the Character, not a switch, so a toggle in Slot B adds little. Runner-up: **Colour** as Slot B's smaller control, the counterpart of All Button grit (inference: All Button mode also changes ratio and timing, which Colour doesn't). Confidence: **high** for Attack and Release.

## Opto

**What the class exposes.** The LA-2A: Peak Reduction and Gain, a Compress/Limit switch, the Emphasis control, and on the LA-2 a Mix control. No Attack, Release or Ratio knob. "No compressor is as easy to operate" ([LA-2A](https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual)). Emphasis is "a shelf filter circuit in the compressor's sidechain input": turning it "gradually reduces the lower frequency content of the sidechain signal, resulting in compression that is less sensitive to those frequencies". The colour is the circuit's: users can "isolate the 'color' of the output amplifier by turning off the Peak Reduction control" (same manual). Timing is chosen by choosing a unit: the three models "offer distinct variations in time constants", Silver being fast enough for drums and the LA-2 the slowest, for "legato tempos and vocal sources" (same manual). Waves' Opto behaviour is a release rule (slower as gain reduction nears 0 dB), not a control ([Renaissance Compressor](https://www.waves.com/1lib/pdf/plugins/renaissance-compressor.pdf), Release Behavior).

**What users adjust.** The manual's operating order is Peak Reduction, then Gain, adjusted independently. Emphasis and Compress/Limit are the only other front-panel controls; of comp1's slot candidates, Emphasis maps to the High-pass (both make detection less sensitive to lows), and Compress/Limit is Ratio, which is excluded.

**Recommendation.** Slot A: **Colour**. Slot B: **High-pass**. This confirms #196's first guess. Runner-up: **Release**, since comp1's Opto honours Release around its two-stage release (ADR 0002) and LA-2A users pick a unit by its speed (inference). Confidence: **medium**. That the class exposes no timing knob is well sourced. That users want Colour as a control is inference: the LA-2A's colour is fixed, not adjustable.

## Vocal

**What the class exposes.** Renaissance Vox is the Compact View's model (glossary, "comp1"). It has three controls: Compression, which "combines in one fader the traditional threshold and ratio controls"; Gain, the limiter's ceiling; and Gate, a gentle downward expander. Its suggested order is Compression, then Gain, then Gate, "fine-tuning it, as the last step" ([Renaissance Vox](https://www.waves.com/1lib/pdf/plugins/renaissance-vox.pdf), Basic Adjustment). Pro-C 3's Vocal style "works with automatic knee and ratio settings", leaving threshold as the main control ([Styles](https://www.fabfilter.com/help/pro-c/using/styleandcharacter)). Logic recommends Opto for vocals and FET for "drums, vocals, guitars" ([Logic Compressor main parameters](https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac)).

**What users adjust.** In the one product designed as a vocal one-knob, the only control besides the amount and the output is the gate. Neither Renaissance Vox nor Pro-C's Vocal style exposes timing. Renaissance Vox's limiter-drive texture ("note how the texture of the sound changes when you send more or less signal to the limiter") is an output function, excluded here.

**Recommendation.** Slot A: **Gate on + Gate Threshold** (well sourced). Slot B: **Release + Auto Release** (inference: no vocal simplified UI found exposes it; it is the timing control Vocal's default Auto Release already engages). Alternative for Slot B: **High-pass**, for low rumble and plosives pushing the compressor (inference, no vocal-specific source found). Confidence: **high** for Slot A, **low** for Slot B.

## Instrument

**What the class exposes.** No simplified "instrument compressor" UI was found. Instrument plays Opto (`Style.h`). The sources assign instruments across all classes: the 1176 for "snare, vocal, or guitar"; the LA-2A for "vocals, bass, strings or horns"; the dbx 160 for "bass guitar, drum buses, synths" and "acoustic guitars"; Logic's VCA for "bass guitars and low-frequency signals" ([1176](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual), [LA-2A](https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual), [dbx 160](https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual), [Logic Compressor main parameters](https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac)).

**What users adjust.** Several manuals describe Attack in terms of instrument transients: API's "pluck of a string" passing with slower attack ([API 2500](https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual)); Waves' "longer attack times let more transients pass uncompressed" ([Renaissance Compressor](https://www.waves.com/1lib/pdf/plugins/renaissance-compressor.pdf)); the 1176's slower attack allowing "transients (or partial transients) to pass" ([1176](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual)).

**Recommendation.** Slot A: **Attack**. Slot B: **Release**. Runner-up for Slot B: **High-pass**, for bass, where the LA-2A's Emphasis and the dbx and SSL filters all aim to reduce low-frequency pumping (inference for this Style). This keeps Instrument's own slots, not its Character's: Opto's Colour and High-pass pair answers a front panel without timing, while an instrument's pick or pluck is a timing question. Confidence: **low to medium**.

## Bus

**What the class exposes.** The SSL 4000 G and API 2500 are designed for the stereo bus (see VCA). Bus plays VCA with Auto Release on (`Style.h`). Pro-C 3's Bus style is "especially great for bus processing, or for adding a pleasant glue to your drums, mixes or tracks", and Smooth is "especially suitable for gluing with low ratio and longer times" ([Styles](https://www.fabfilter.com/help/pro-c/using/styleandcharacter)).

**What users adjust.** The SSL manual is the most explicit operating guide found for any class: set the threshold for "1-2 dB average gain reduction", tune attack and release "to the tempo and feel of the song", use Auto for "the greatest degree of transparency", and high-pass the detection signal against bass pumping ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual)). API's Thrust exists to reduce "low frequency pumping" while keeping punch ([API 2500](https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual)).

**Recommendation.** Slot A: **Attack**, with **High-pass** as its smaller control if the slot shape is two controls. Slot B: **Release + Auto Release**. Confidence: **medium to high**. All three controls are named in the SSL's own operating guidance. If a slot holds one control only, choosing between High-pass and Release is the open call; the SSL text supports both.

## Drums

**What the class exposes.** Drums plays FET (`Style.h`). Drum use appears in every class's manual, and it is the use most often tied to distortion: the 1176's All Button mode is "typically" used "on drums or on ambience or room mics" ([1176](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual)); on drum buses the SSL takes "a more aggressive approach" where "fast attacks and releases will give the most audible sound" ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual)); the dbx 160 gives snare "in-your-face 'thwack'" ([dbx 160](https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual)); DC1A reaches "heavy pumping with a nice crunchy saturation and punchy enough to treat drums with" ([DC1A3 manual](https://klanghelm.com/docs/DC1A3-manual.pdf)); Pro-C 3's Pumping style is "great for drum processing or EDM" ([Styles](https://www.fabfilter.com/help/pro-c/using/styleandcharacter)). Neutron's Punch intent, which works on "musical hits", exposes Amount, Attack and Sustain ([Neutron 4 Assistant](https://s3.amazonaws.com/izotopedownloads/docs/neutron4/en/assistant/index.html)).

**What users adjust.** Attack is named for drums in Pro-C's help ("for transient-rich program material like drums, fast attack times are needed", [Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols)) and in Neutron's Punch ("lower settings accentuate the punch of musical hits"). Grit is the second recurring theme.

**Recommendation.** Slot A: **Attack**. Slot B: **Saturation on + Drive**, confirming #196's first guess. Two cautions. First, the hardware grit the sources describe is compression distortion, comp1's Colour, not a separate stage; Colour is the closer analogue (inference). Second, choosing a Style never writes Saturation (ADR 0002), so the Drums slot shows whatever Saturation state the user left, while Colour arrives at Drums' default. Runner-up for Slot B: **Release**, from the SSL's "fast attacks and releases" and Neutron's Sustain. Confidence: **medium** for Attack plus a tone slot; **low** on Saturation over Colour.

## Summary

| Style | Slot A | Slot B | Confidence |
|---|---|---|---|
| Clean | Attack | Release + Auto Release | medium |
| VCA | Attack | Release + Auto Release | medium |
| FET | Attack | Release | high |
| Opto | Colour | High-pass | medium |
| Vocal | Gate on + Gate Threshold | Release + Auto Release | high (A), low (B) |
| Instrument | Attack | Release | low to medium |
| Bus | Attack (+ High-pass as smaller control) | Release + Auto Release | medium to high |
| Drums | Attack | Saturation on + Drive (or Colour) | medium (A), low (B choice) |

Against #196's first guesses, the research changes FET's Slot B to plain Release, adds the High-pass to Bus, and flags Colour as a candidate for Drums. It confirms Opto, Vocal's Slot A, and the Attack-and-Release default.

## Should a tone slot be core?

**Finding: no. Keep tone Style-driven.**

- **The simplified UIs mostly have no tone control.** Renaissance Vox, the 1176, the LA-2A and the dbx 160 have none; their colour is fixed by the circuit or reached through the amount and timing ([Renaissance Vox](https://www.waves.com/1lib/pdf/plugins/renaissance-vox.pdf), [1176](https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual), [LA-2A](https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual), [dbx 160](https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual)). DC1A's only tone influence is Relaxed, which also lengthens the time constants ([DC1A3 manual](https://klanghelm.com/docs/DC1A3-manual.pdf)).
- **Where tone is present, it is a separate section or mode, not beside the amount.** Pro-C 3 has a Character section (mode, Drive, Pre/Post), its Drive "reverse-linked with the Threshold knob" to "'trade' compression for saturation in one movement" ([Styles](https://www.fabfilter.com/help/pro-c/using/styleandcharacter)). Neutron's Assistant gives tone its own Distort intent, apart from Punch ([Neutron 4 Assistant](https://s3.amazonaws.com/izotopedownloads/docs/neutron4/en/assistant/index.html)). Logic puts Distortion in the Output section, not the main panel ([Logic Compressor output parameters](https://support.apple.com/guide/logicpro/compressor-output-parameters-lgcec5af48de/mac)). Waves' Warm/Smooth is the exception: it tells users to set it first, with ARC and Electro/Opto, "to set up a 'feel' for the compressor" ([Renaissance Compressor](https://www.waves.com/1lib/pdf/plugins/renaissance-compressor.pdf)), but it is a two-way switch, not a continuous control.
- **A core Colour would do nothing on two Styles.** Clean has no Colour (glossary, "Colour"), and Vocal plays Clean (`Style.h`). A core Colour knob would sit inert on both, against the spirit of "show, never disable" (ADR 0002). A core Saturation would work everywhere, but no source found puts a standalone saturation stage beside a one-knob compressor's amount.

Tone earns a slot on the Styles whose class manuals tie colour to their use: Opto (Colour) and Drums (Saturation or Colour).

## Findings for #196's other open questions

**Slot shape.** The sources pair a toggle with a knob often, and sometimes fold the toggle into the knob:

- Auto Release is a button beside Release in Pro-C 3, Logic and Waves ([Time controls](https://www.fabfilter.com/help/pro-c/using/timecontrols), [Logic Compressor main parameters](https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac), [Renaissance Compressor](https://www.waves.com/1lib/pdf/plugins/renaissance-compressor.pdf)); the SSL makes Auto one stop on the Release selector ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual)).
- The detection high-pass is off at its knob's lowest setting on the SSL, with an "OFF" label to toggle it; a toggle on the dbx 160 and DC1A ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual), [dbx 160](https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual), [DC1A3 manual](https://klanghelm.com/docs/DC1A3-manual.pdf)).
- Renaissance Vox's Gate is one fader with no on switch; its range runs to minus infinity ([Renaissance Vox](https://www.waves.com/1lib/pdf/plugins/renaissance-vox.pdf)).
- Attack is a stepped selector on the SSL (six values) and API (seven), which supports #196's chip-row idea for Attack ([SSL 4000 G](https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual), [API 2500](https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual)).

Per Style, from the recommendations: Clean, VCA, Bus and Vocal want a knob and a toggle in Slot B (Release + Auto Release); Vocal's Slot A is a toggle and a knob (Gate); Drums' Slot B is a toggle and a knob (Saturation); FET, Instrument and Opto want one control per slot. A fixed "66 px knob plus 30 px control" shape fits every Style only if the one-control Styles fill the small position with a runner-up (FET: Colour; Opto: Release; Instrument: High-pass; Bus: High-pass). That is a design call, not a sourced one.

**Is the Gate core?** No source but Renaissance Vox, the vocal processor, puts a gate in a simplified compressor UI. DC1A, the LA-2A, the 1176, the dbx 160, the SSL, Pro-C's styles and Neutron's Punch have none ([sources above](#what-the-simplified-uis-keep)). This supports #196's "Gate is a slot, not core".

**The hero's label.** Simplified UIs name the amount control differently: "Compression" (Renaissance Vox), "Input" (1176, DC1A), "Peak Reduction" (LA-2A), "Threshold" (dbx 160, SSL, API), "Amount" (Neutron Punch). None changes the label with a mode. Nothing found argues for a per-Style label.

**The Comp Follow-off cue.** No primary source was found that shows how another UI signals that a combined control has stopped driving the controls it combines. The nearest precedent is Neutron 4's Target Library: a Target "will enter a dirty state when the original settings for the Target have been modified in the Detailed view", meaning "changes have been made that are outside assistant-recommended settings", and selecting the Target's button "will restore the original settings" ([Neutron 4 Assistant](https://s3.amazonaws.com/izotopedownloads/docs/neutron4/en/assistant/index.html)). That is a modified-state mark plus a one-click way back, close to comp1's Comp Follow toggle (ADR 0004), but the documentation doesn't say how the state is drawn. Renaissance Vox's Compression fader has no separate Threshold and Ratio to edit, so it never unlinks ([Renaissance Vox](https://www.waves.com/1lib/pdf/plugins/renaissance-vox.pdf)). Pro-C 3's Drive is "reverse-linked" to Threshold, but the help says nothing about a cue ([Dynamics controls](https://www.fabfilter.com/help/pro-c/using/dynamicscontrols)). Neutron's Intent controls "are linked to deeper controls in the Detailed View", and the documentation doesn't say what an Intent control shows when those are edited directly ([Neutron 4 Assistant](https://s3.amazonaws.com/izotopedownloads/docs/neutron4/en/assistant/index.html)). The cue's form remains a design question for grilling.

## Sources

- Apple, Logic Pro for Mac, Compressor main parameters: https://support.apple.com/guide/logicpro/compressor-main-parameters-lgcead9636ef/mac
- Apple, Logic Pro for Mac, Compressor output parameters: https://support.apple.com/guide/logicpro/compressor-output-parameters-lgcec5af48de/mac
- FabFilter, Pro-C 3 online help, Style and character: https://www.fabfilter.com/help/pro-c/using/styleandcharacter
- FabFilter, Pro-C 3 online help, Time controls: https://www.fabfilter.com/help/pro-c/using/timecontrols
- FabFilter, Pro-C 3 online help, Dynamics controls: https://www.fabfilter.com/help/pro-c/using/dynamicscontrols
- FabFilter, Pro-C 3 online help, Overview: https://www.fabfilter.com/help/pro-c/using/overview
- FabFilter, Pro-C 3 online help, Displays and metering: https://www.fabfilter.com/help/pro-c/using/displays
- iZotope, Neutron 4 Help, Assistant: https://s3.amazonaws.com/izotopedownloads/docs/neutron4/en/assistant/index.html
- Klanghelm, DC1A3 user guide: https://klanghelm.com/docs/DC1A3-manual.pdf
- Klanghelm, DC1A product page: https://klanghelm.com/contents/products/DC1A.html
- Universal Audio, 1176 Classic FET Compressor Manual: https://help.uaudio.com/hc/en-us/articles/34530260482324-1176-Classic-FET-Compressor-Manual
- Universal Audio, Teletronix LA-2A Leveler Collection Manual: https://help.uaudio.com/hc/en-us/articles/4419496124180-Teletronix-LA-2A-Leveler-Collection-Manual
- Universal Audio, SSL 4000 G Bus Compressor Manual: https://help.uaudio.com/hc/en-us/articles/30847649785748-SSL-4000-G-Bus-Compressor-Manual
- Universal Audio, dbx 160 Compressor Manual: https://help.uaudio.com/hc/en-us/articles/18741620620692-dbx-160-Compressor-Manual
- Universal Audio, API 2500 Bus Compressor Manual: https://help.uaudio.com/hc/en-us/articles/4419505673620-API-2500-Bus-Compressor-Manual
- Waves, Renaissance Vox user guide: https://www.waves.com/1lib/pdf/plugins/renaissance-vox.pdf
- Waves, Renaissance Compressor user guide: https://www.waves.com/1lib/pdf/plugins/renaissance-compressor.pdf
