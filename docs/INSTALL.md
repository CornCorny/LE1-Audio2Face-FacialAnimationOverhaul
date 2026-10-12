# Install / remove the developer prototype

For Windows Mass Effect Legendary Edition **LE1**, English/INT. Tested against the
EA installation. Other storefront/build combinations have not been established.
LE1 Community Patch 2.0 is supported in the tested opening-scene route.
Other mod combinations remain unvalidated. Use a backup and keep existing saves. Do not mix files from different mod versions.

1. Close LE1 and the Legendary Edition launcher.
2. Use ME3Tweaks Mod Manager to prepare the LE1 Bink/ASI loader and AutoTOC.
   These prerequisites are separate; this ZIP is not a Mod Manager library mod.
3. Extract the developer ZIP. Copy its `Game` directory into the Legendary Edition
   installation (the directory already containing `Game`), preserving the layout.
   Back up any existing files with the same private mod filenames first.
4. Refresh the LE1 file table using the usual Mod Manager AutoTOC process before
   testing. Start a new career with default/iconic male Shepard.
5. Play the opening Normandy conversation. The recorded review route selected
   **I agree**, then **You made him mad**, and continued into gameplay.

The package installs 37 private files:

```
Game/ME1/Binaries/Win64/ASI/MEFSingleLineBridge.asi
Game/ME1/Binaries/Win64/ASI/MEFIntroClips/    (30 .mefc files)
Game/ME1/BioGame/CookedPCConsole/MEFSingleLineBridge.pcc
Game/ME1/BioGame/CookedPCConsole/MEFJokerBridge.pcc
Game/ME1/BioGame/CookedPCConsole/MEFKaidanBridge.pcc
Game/ME1/BioGame/CookedPCConsole/MEFAverageBridge.pcc
Game/ME1/BioGame/CookedPCConsole/MEFFaceShaders.pcc
Game/ME1/BioGame/CookedPCConsole/MEFHumanMale2266Bridge.pcc
```

The internal MEF filenames are historical implementation names. Do not rename
them: the runtime references these package and clip identities.

No stock audio/dialogue/mesh package is replaced. A C++ runtime may be needed:
[Microsoft Visual C++ Redistributable x64](https://aka.ms/vs/17/release/vc_redist.x64.exe).
No NVIDIA inference software or model download is required by players.

## Disable / remove

With game and launcher closed, move `MEFSingleLineBridge.asi` completely outside
the ASI directory to disable playback. For complete removal, also remove the
thirty clips and six private PCCs listed above. Restore your backups if replacing
another version, then refresh AutoTOC. Do not remove other mods' files, shared
ASI-loader files or AutoTOC just to disable this mod.

The runtime also creates `MEFSingleLineBridge.log` beside the ASI. It is not a
required mod file and may be removed while the game is closed. Preserve a copy
after a test before relaunching if reporting a failure. Check for private paths.

The expected runtime log identifies **0.2.8, 30 clips, 48 channels**. If an executable
guard refuses the game build, report it rather than bypassing the guard.
