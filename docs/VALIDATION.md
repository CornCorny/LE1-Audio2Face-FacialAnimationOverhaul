# Validation status — 0.2.7

The original candidate passed eleven CTests; four 48-target morph PCC round trips;
all thirty clips over ten head LODs using the serialized sparse morph data;
combined-displacement, protected-zero-displacement and preservation checks; and
player DLL import inspection with no AI runtime dependency.

The Sync8 review covered fifteen recorded speeches (Joker 10, Kaidan 3, Shepard 2).
Logs reported 7274 custom applications and 7283 ownership calls/restores, with no
critical/restoration/activation/handover faults. All 36 installed file hashes
matched the accepted build. Reviewed NPC rendering was clean and playback reached
gameplay. One withheld pose/two refusal-counter increments occurred at Kaidan's
final-line end around an LOD change, after zero weights, followed by clean release.
This was not a speaking activation failure. One Joker map rebuild restored and
revalidated safely.

Visual improvement is subjective and uneven. Joker shows the clearest articulation;
Kaidan's changes can be subtle and a brief restrained entry expression remains.
Exact recording A/V offset and phoneme accuracy have not been measured independently.

Not yet established: other four Shepard alternatives in gameplay, all unchosen
NPC branches, repeated skip/restart, frame-rate combinations, longer gameplay,
other mods/storefronts and broader human/asari coverage. Offline interpolation tests
at varying frame rates are not a substitute for these live checks. Clay GPU
previews omit final shading and vanilla contributions and cannot prove live safety.

Useful reports: exact build/storefront, default/custom/female player, subtitle/line,
choices, frame rate, other installed mods, whether the effect persists after the
scene, and a short recording. Attach a reviewed/redacted runtime log when useful.
