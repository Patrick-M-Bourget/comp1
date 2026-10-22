# Auto Gain is estimated from the settings, never measured from the audio

Auto Gain makes up the level compression takes away, so that a change in compression is heard without a change in loudness. comp1 works it out from the settings alone, never from the audio playing through.

A gain measured from the audio would chase the material. On a loud chorus after a quiet verse it would rise and fall with the song's own dynamics, pumping on top of the compressor's movement, and over time it would make up exactly the dynamics the compressor took out, undoing what compression is for. An estimate from the settings is a fixed gain for a fixed set of settings: the same input always gives the same output, whatever came before it, so a session plays back the same every time and Host Automation of a setting moves Auto Gain at the moment the setting moves.

## What it reads

- **The Transfer Curve:** Threshold, Ratio, Knee and Range, the derived Threshold and Ratio while Comp Follow is on. They set how much Gain Reduction a level asks for.
- **The timing:** Attack, Release, Auto Release and Hold. They set how much of that Gain Reduction the detector actually holds on real material: a fast Attack with a slow Release holds it near the crests, a slow Attack with a fast Release lets it fall between them, and the level moves by several dB either way.
- **The Character.** Each Character's detector holds its Gain Reduction differently: VCA's RMS stage smooths the crests before the detector sees them, and FET's and Opto's loops hear their own Gain Reduction.

The estimate is the Transfer Curve's average Gain Reduction over a reference level distribution, each level read higher by a Timing Offset that the timing and the Character set, fitted in dyn-lab over a grid of settings (tools/dyn-lab/USAGE.md, "autogain"). It is a fixed amount of arithmetic, worked out when the settings change.

## What it doesn't read

Lookahead, the Detection EQ, Stereo Link, Stereo Placement, the Detection Source, Colour and Saturation. Each depends on the material or the routing rather than on how the compressor moves: what the Detection EQ hears, how two channels differ, what a Sidechain carries. An estimate can't know these from the settings, and guessing would make Auto Gain move for reasons the user can't see. With an External Detection Source the estimate is the Internal one. Colour and Saturation add distortion, whose loudness Auto Gain doesn't compensate. Lookahead moves the reference signal's level by under 0.1 dB.

## Considered Options

- **Measuring the output level and making it up.** Rejected: it chases the material, pumps, and undoes the compression, and the same input gives a different output depending on what played before it.
- **The Transfer Curve alone.** Rejected: on real material the error is 1 to 5 dB at common timings, quieter at a fast Attack and slow Release and louder at the reverse, so a Style with a fast Attack needed a looser tolerance of its own.
- **Simulating the detector on a synthetic signal at each settings change.** Rejected: it needs a worker thread and a hand-off to keep it off the audio thread, so a setting changed by Host Automation would move Auto Gain late, by however long the simulation took, and playback wouldn't be deterministic.
