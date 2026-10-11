# Offline GPU authoring source

This repository includes the project-authored inference exporter, retargeting,
conditioning, residual fitting, morph serialization and audit scripts for developer
inspection. They are research tooling, not yet a portable one-command asset build.
Historical iteration scripts refer to baseline and intermediate artifacts that
are not bundled; running them in order without reconstructing those inputs fails.

Native runtime compilation and its included tests are independent of this stack.

Authoring used NVIDIA Audio2Face-3D SDK, Mark diffusion (v3), Audio2Emotion-v2.2,
CUDA 12.9, standard TensorRT 10.13, Python 3.10/PyTorch CUDA, Legendary Explorer,
and Blender/OptiX for previews. These are not player requirements. Obtain SDK/model
files from NVIDIA and accept their terms independently. No credentials, SDK/model
weights, extracted dialogue, game recordings or private workstation logs are bundled.

Source entry points:

- `src/authoring/a2f-export/main.cpp`: GPU inference/export and emotion input.
- `tools/Prepare-A2E.py`: local emotion model preparation/inference tooling.
- `Condition-IntroEmotionsGPU.py`: shared temporal emotion conditioning.
- `Fit-SemanticRigGPU.py`, `Correct-MouthApertureGPU.py`: shared rig/lip work.
- `Fit-DetailRigGPU.py`: 0.2.7 residual detail fit and generated curve preparation.
- `src/tools/Extract-IntroScene.ps1`: read game dialogue identities/audio.
- `src/tools/Build-IntroMorphs.ps1`: serialize and round-trip MorphTarget data.
- `src/tools/Repair-BridgeEffectOwnership.ps1`: historical shader ownership repair.

Legendary Explorer tooling expects `LEGENDARY_EXPLORER_DIR` and `LE_INSTALL_DIR`
environment variables in the exported helper. Obtain a legitimate LE installation.
Other authoring tools retain workspace-relative layouts and historical baselines;
their paths and inputs must be reviewed before running. No authoring script is an
installer. The packaged precomputed assets allow runtime work without regenerating
the NVIDIA outputs. Next work should parameterize inputs and consolidate the
successful stages, not rerun all failed historical experiments.
