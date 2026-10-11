# Native runtime build

Requirements: Windows x64, Git, CMake 3.27+, Visual Studio 2022 with Desktop
development with C++, MSVC v143 and a Windows SDK. No AI stack is used by this build.

From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

CMake fetches LExSDKv2 from https://github.com/llamathings/LExSDKv2 at commit
`3885be14113816ff1d2a0d941aa96bba6df9dc58`. Alternatively pass
`-DLESDK_SOURCE_DIR="C:/path/to/LExSDKv2"` when configuring. Use that pinned revision.
The upstream SDK is not bundled or relicensed; see NOTICE.md and its README.

Output: `build/src/asi/single-line-bridge/Release/MEFSingleLineBridge.asi`.
Keep it with the exact 0.2.7 clips and PCC packages. The published runtime binary
is the gameplay-tested original; recompilation changes binary metadata and is
not claimed byte-identical to that binary.

All eleven existing tests are included. The loader test loads the ASI into a test
process and checks rejection of an unsupported executable. It does not launch the
game or prove gameplay compatibility. The clip test reads all thirty included
animation fixtures and checks their generated hashes. The historical binding
fixture retains only the fields needed by that regression.

Only dependency/test-data paths were changed for this standalone export. Runtime
C++ and generated runtime metadata are unchanged from the accepted 0.2.7 archive.
