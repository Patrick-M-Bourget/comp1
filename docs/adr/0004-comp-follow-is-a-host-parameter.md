# Comp Follow is a host parameter, written only inside the user's gestures

Comp sets Threshold and Ratio along the current Style's Comp Curve. Whether it does is Comp Follow, which is a host parameter rather than saved editor state, so a session where the user switched it mid-song plays back the same way every time. The plugin writes parameters only inside the user's own gestures in the editor, never on its own. Host Automation is the user's.

## Rules

- **While Comp Follow is on**, the Engine derives Threshold and Ratio from Comp. The stored Threshold and Ratio parameters are ignored, Host Automation of them has no effect, and the editor shows the derived values.
- **Turning it off from the editor** (by editing Threshold or Ratio) writes the derived Threshold and Ratio into their parameters in the same gesture, so the sound doesn't change.
- **Turning it on** (by grabbing Comp) puts Comp at the position nearest the current Threshold. The Engine then glides Threshold and Ratio onto the Comp Curve over a fixed time (about 300 ms, set in the lab). When Host Automation turns it on or off, the Engine glides the same way.
- **The editor's Comp Follow toggle** turns it on and off the same two ways: on as grabbing Comp does, off as editing Threshold or Ratio does, never by writing Comp Follow alone. So turning it off from the toggle never changes the sound, and turning it on starts from the Comp nearest the stored Threshold.
- **Auto Gain is independent of Comp Follow.** The Engine plays the stored Auto Gain, on or off, whether Comp Follow is on or not, and the editor's Auto Gain toggle can turn it on and off at any time: the user wants to be able to turn Auto Gain off whenever they choose, following or not. Choosing a Style writes Auto Gain on with its other defaults (ADR 0002), so a Style still starts out level-compensated.

## Considered Options

- **Comp Follow holding Auto Gain on**, the stored Auto Gain ignored while following (this ADR's first version). Rejected: the user couldn't turn Auto Gain off without first leaving Comp Follow.
- **Comp Follow as saved state, not a parameter** (the spec's first draft). Rejected: Host Automation of Comp around an unfollow can't be reproduced, and turning it back on moves Comp, which is a parameter write anyway.
- **The plugin writing the derived Threshold and Ratio continuously.** Rejected: it would record over the user's automation lanes, and each write would itself turn Comp Follow off.
- **A glide over the knob's travel, as in the spec's first draft.** Rejected: it depends on where a gesture starts, which Host Automation doesn't record, so playback couldn't reproduce it. A glide over time can.
