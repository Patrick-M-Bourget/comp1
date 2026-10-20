# Characters carry the DSP; Source Styles carry only defaults

The eight Styles share one picker, but only the four Characters (Clean, VCA, FET, Opto) carry DSP. Each Character owns how it detects (feedforward or feedback, peak or RMS), how its timing and knee respond, and its Colour. The four Source Styles (Vocal, Instrument, Bus, Drums) each name a Character and add a set of defaults and their own Comp Curve, with no DSP of their own. That keeps the DSP to four lab-tuned designs, while the picker still offers a starting point for each job.

## Rules that follow

- **Every control works on every Character**, as in Pro-C. A Character shapes how the controls behave but never disables one. Opto's material-dependent attack and two-stage release vary around the user's Attack and Release. Auto Release replaces Opto's fast stage, and its slow stage always applies.
- **Choosing a Style writes its defaults, as one undo step:** the Character, Colour, Threshold, Ratio, Knee, Attack, Release, Auto Release, Hold, Range, the Gate and Limiter settings, and Comp Follow on. It never touches routing or levels: the Sidechain and Detection EQ, Stereo Placement, Stereo Link, Processing Mode, Saturation, Dry, Wet and Output Gain.
- **Style is a stored, versioned parameter that can't be automated.** If it could be automated, a Style would either arrive without its defaults or make the plugin write many parameters during playback.
- **Detection keys from the Detection Source, never from a feedback path.** The Gate and the Detection Level hear the input or the Sidechain after the Detection EQ. With an External Detection Source, the feedback Characters (FET, Opto) detect feedforward from the Sidechain and keep their timing and Colour. With Lookahead, they detect from the undelayed input times the current gain, which predicts the delayed output.
- **Names are generic.** Styles are never named after hardware or brands.

## Considered Options

- **Each Style its own DSP.** Rejected: eight designs to tune and test, when Vocal, Instrument, Bus and Drums differ from a Character only in starting point.
- **Characters disabling controls they don't have in hardware** (an LA-2A has no Attack or Ratio). Rejected: switching Character would silently change which knobs work, and the Comp Curve sets Ratio on every Style.
