# LE1 Audio2Face Facial Animation Overhaul

An experimental facial-animation mod for **Mass Effect Legendary Edition 1**.
NVIDIA Audio2Face and Audio2Emotion process the game's existing dialogue offline;
a small native ASI plays the resulting animation through enhanced UE3 morph targets.
The original actors' dialogue audio is unchanged. No AI inference runs during gameplay.

> **AI-assisted development: OpenAI Codex was used extensively throughout this project.**
> Codex contributed substantially to writing and revising code, investigating LE1
> integration, debugging problems and developing the offline animation pipeline.
> CornCorny provided project direction, creative decisions and in-game testing.
> NVIDIA Audio2Face/Audio2Emotion generated the facial animation offline on a local GPU.

**Current developer prototype: 0.2.7.** This is an opening-scene demonstration,
not a finished overhaul of the whole game.

## How it works

- **Use the original dialogue.** The game's prerecorded English dialogue is extracted for offline authoring. The game still plays the original actors' audio unchanged.
- **Estimate emotion on the GPU.** NVIDIA Audio2Emotion analyses the delivery and supplies emotion signals, which are smoothed and moderated before use.
- **Generate facial motion.** NVIDIA Audio2Face uses the dialogue and emotion signals to generate changing lip, jaw, cheek and brow movements.
- **Adapt the motion to LE1's faces.** GPU fitting transfers that movement onto custom UE3 morph targets: shapes that deform parts of the existing heads while preserving their neutral appearance.
- **Store the finished animation.** The motion is smoothed and bounded, then saved as timed weights for 48 facial controls in `.mefc` files. These files contain animation data, not audio or AI models.
- **Match it to the live dialogue.** A small native ASI plugin identifies the supported speaker and exact dialogue line, selects its clip and samples it using the game's dialogue audio clock.
- **Preserve the rest of the performance.** Selected vanilla facial controls blend into the custom animation and return afterward. Eyes/gaze, blinking, head/body motion, gestures, cameras and cinematic timing remain controlled by the game.
- **Play back without the AI stack.** Players install the plugin, morph assets and precomputed clips. All NVIDIA inference happens during authoring; the current prototype covers only the scoped opening Normandy conversation.

## Download and installation

[Download the 0.2.7 developer package](https://github.com/CornCorny/LE1-Audio2Face-FacialAnimationOverhaul/releases/download/v0.2.7/LE1-Audio2Face-FacialAnimationOverhaul-0.2.7-developer.zip)

Read [installation and removal](docs/INSTALL.md) first. This is a **manual-install
developer build**, not yet a ME3Tweaks Mod Manager library installer. ME3Tweaks
provides the required LE1 ASI loader and AutoTOC; those are not bundled.

Players do **not** need CUDA, TensorRT, Python, PyTorch, Blender or NVIDIA models.
Those are authoring dependencies only. A Windows x64 C++ runtime is required.

## What it currently covers

- Opening Normandy conversation only, English/INT audio.
- Default/iconic **male Shepard**, Joker and Kaidan.
- Thirty precomputed line assets, including all six male Shepard reply variations.
- Nihlus/the turian and other characters/scenes are excluded.
- Custom male Shepard's head is excluded. NPC playback with custom male Shepard
  has not been validated. A female player currently disables custom playback for
  the entire target conversation, including the NPCs.

The mod preserves vanilla eyes/gaze, blinking, head/neck/body movement, gestures,
cameras, dialogue branching, audio and cinematic timing. It replaces selected
facial surface motion, rather than generating new voices or filtering video.

## Status and limitations

The recorded default-male route shows animation on all three characters with clean
reviewed NPC rendering and no logged restoration/activation failures. Joker's
changes are most apparent; some Kaidan line-start expression settling remains.
All thirty assets pass offline checks, but not every dialogue variation has been
visually tested. Skipping/restarting, other frame rates, extended play and other
mods still need compatibility testing. This is not a universal human/asari solution.

**Initial implementation scope:** opening Normandy scene, default male Shepard,
Joker and Kaidan only. Do not expand until this sequence is fully working,
accurately synchronized, visually validated and successfully packaged as an
installable mod. Broader human/asari coverage is a future design goal.

## For developers

- [Build the native runtime and run its eleven regression tests](docs/BUILD.md)
- [Architecture, clip format and rendering lessons](docs/ARCHITECTURE.md)
- [Offline authoring source and current reproducibility limits](docs/AUTHORING.md)
- [Validation and useful feedback](docs/VALIDATION.md)
- [Credits and rights](NOTICE.md)

Native runtime source is included. GPU authoring scripts are research tooling:
they still depend on intermediate data and separately obtained game/model assets.
The native build is standalone; the full authoring workspace is not bundled.

Local GPU inference, numerical fitting and preview rendering were used.
This is an independent fan project, not affiliated with EA, BioWare or NVIDIA.

Feedback on facial quality, transitions, rendering safety and a reusable human/asari
rig pipeline is welcome. Please include the build, dialogue choice and a short
recording when reporting a problem; inspect logs for personal information first.
