# Architecture and developer notes

`original LE1 dialogue -> offline GPU Audio2Emotion/Audio2Face -> .mefc clips -> native LE1 ASI -> UE3 MorphTargets`

## Offline

Mark diffusion with emotion conditioning generates source weights and neural skin
geometry. Shared semantic skin-influence fields transfer facial movement to the
head bases. The current rig has 48 channels: the prior 38 plus ten signed residual
brow/mouth/cheek controls. These are paired positive/negative morphs with bounded
nonnegative weights. Neutral mesh identity, skeleton and materials are preserved.
The residual basis was fitted to this scene; its adequacy for the whole game is
not established. No tongue deformation is implemented.

## Runtime

`Bridge.cpp` restricts activation to conversation resource 205 and
`nor10_joker_D.nor10_joker_dlg`, the eligible actor/head and an exact voiced-line
identity from `GeneratedScene.hpp`. Female-player conversations are excluded.
`LineBinding.hpp` contains the head/rig allowlist. The supported NPC path uses
the shared Average base; the shipped historical NPC packages are retained.

`FacialClip.hpp` validates expected schema, file hash, dimensions, timestamps,
bounds and neutral endpoints. It samples using the dialogue audio clock with
linear interpolation, rather than advancing a fixed number of frames per tick.
Missing/invalid inputs should be refused by guards, not guessed.

`FaceOwnership*` and `FaceTransition*` limit suppression/blending to verified named
facial outputs. Vanilla and custom motion share an audio-relative handover:
160 ms for speech, 240 ms for brows. Current-call snapshots and guarded restoration
avoid persistent ownership. Protected eye/blink/head/body outputs are not authored.

`NativeMorph.hpp`, `FaceRouting.hpp`, `FaceRebuildNative.hpp` and resource tracing
manage native morph evaluation and identity/lifetime checks. This path is specific
to the inspected LE1 executable; safe guards do not make arbitrary builds portable.

## Rendering lessons

Earlier NPC experiments produced persistent skin shading corruption and crashes.
The accepted path preserves the successful GPU morph/material infrastructure and
the effects ownership/Outer relationship fix. Do not revert to the earlier CPU
renderer or blindly replace ActiveMorph arrays. Do not infer that the absence of
log failures proves visual correctness. The private shader PCC must accompany
the matching runtime and rigs; test LOD changes, cleanup and return to gameplay.

## Clip format (version 1)

Little-endian, 32-byte header followed by frames:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | uint32 | magic 0x4346454d (`MEFC`) |
| 4 | uint32 | version 1 |
| 8 | uint32 | channel count (48 for this build) |
| 12 | uint32 | frame count |
| 16 | uint32 | audio sample rate (16000) |
| 20 | uint32 | audio sample count |
| 24 | uint64 | ordered channel-schema FNV-1a hash |

Each frame is uint32 audio-sample timestamp, then channel-count float32 weights.
Weights must be finite and in [0,1]; timestamps strictly increase from zero to
audio length; first and final weights are zero. File FNV-1a hashes are pinned in
generated C++ metadata. These are integrity/schema checks, not cryptographic
authentication. The downloadable package additionally includes SHA-256 hashes.

This format contains animation only, no dialogue audio. Changes to channel order,
rigs or content require regeneration of matching metadata, clips and packages.
