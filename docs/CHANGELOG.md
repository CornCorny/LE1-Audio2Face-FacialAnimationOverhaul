# 0.2.8 — LE1 Community Patch compatibility

- Supports LE1 Community Patch 2.0 in the tested opening Normandy scene.
- Resolves speakers through live conversation participants instead of fixed NPC
  package paths, retaining exact conversation and dialogue/audio checks.
- Selects compatible authored rigs by active mesh identity, skeleton, LODs and
  channel checks. Adds a GPU-transferred private rig for the patched Kaidan head.
- Preserves effective inherited face material parameters and the accepted GPU
  morph/effects lifecycle. Does not replace Community Patch or stock head meshes.
- Keeps the 30 existing animation clips, 48-control schema and protected vanilla
  eyes/blinks/head/body/audio/cinematic behavior. This is a compatibility release,
  not a new facial-performance generation pass or whole-game expansion.
- Twelve regression tests pass; the recorded Community Patch route shows active
  animation on all three characters and clean reviewed rendering.

Manual-install experimental developer prerelease. See INSTALL.md and VALIDATION.md.
