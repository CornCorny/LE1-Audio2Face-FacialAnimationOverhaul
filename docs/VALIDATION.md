# Validation status — 0.2.8

## LE1 Community Patch support

The opening Normandy scene was tested with **LE1 Community Patch 2.0**, default
male Shepard, Joker and Kaidan. The recorded route selected **I agree**, then
**You made him mad**, and continued into gameplay and a later conversation.
All fifteen recorded intro speeches activated (Joker 10, Kaidan 3, Shepard 2).
Reviewed NPC rendering was clean, without the earlier persistent colour corruption.

Logs reported 6461 custom morph applications and 6469 ownership calls matched by
6469 restorations. No withheld poses, activation refusals, critical faults,
restoration failures or handover refusals were recorded. One Joker map rebuild
was restored, revalidated and reapplied. All 37 installed mod files matched the
candidate. Playback released at the scene boundary; later dialogue stayed vanilla.
These are observations of one recorded run, not proof of universal reliability.

Twelve native regression tests pass. The thirty clips and five existing private
PCCs are byte-identical to 0.2.7. The additional 48-target head rig passed package
round trips and local CUDA checks across all thirty clips, including zero protected
displacement. Twenty-four offline OptiX preview frames were reviewed. Offline
previews do not establish gameplay rendering or synchronization by themselves.

The waveform checks support dialogue identity and alignment, but recording A/V
offset and frame-accurate phoneme synchronization have not been independently
calibrated. Facial quality remains subjective and Kaidan's changes are restrained.

Unvalidated: the four unchosen Shepard replies and other dialogue branches in
gameplay, skips/restarts, frame-rate combinations, extended sessions, other
storefront builds, other mods, custom/female Shepard and broader human/asari
coverage. No AI inference runs during gameplay. Original game audio is unchanged.

For reports include the build, dialogue choice, other installed mods and a short
recording. Review/redact runtime logs for private paths before sharing them.
