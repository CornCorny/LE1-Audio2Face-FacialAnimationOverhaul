# Restore the stock effects-user Outer relationship, without rebuilding shaders.
$ErrorActionPreference='Stop'
. "$PSScriptRoot/Lex-ReadOnly.ps1"
. "$PSScriptRoot/ShaderCacheMetadata.ps1"
[LegendaryExplorerCore.Packages.PackageSaver]::PackageSaveFailedCallback=[Action[string]]{param($message) throw $message}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$baseline="$root/output/single-line-bridge/release/MEF-SingleLineBridge-0.1.14/Game/ME1/BioGame/CookedPCConsole/MEFFaceShaders.pcc"
$out="$root/output/single-line-bridge/assets/MEFFaceShaders.pcc"
$evidence="$root/research/single-line/try15"
New-Item -ItemType Directory -Force $evidence | Out-Null
if((Get-FileHash $baseline).Hash -ne '27AB70A6EE26003BA2383D8FCCF8BBB383A50BAFA2A3EBA62EE53290CA2CDF35'){throw 'Unexpected 0.1.14 shader baseline'}
function Hash-Bytes([byte[]]$Bytes){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes))}
function Import-Snapshot($Package){
    @($Package.Imports | ForEach-Object {
        [ordered]@{Index=$_.UIndex;Path=$_.InstancedFullPath;Class=$_.ClassName;ClassPackage=$_.PackageFile;Outer=$_.idxLink}
    }) | ConvertTo-Json -Depth 6 -Compress
}
$scene=Open-GamePackage 'BIOA_NOR10_01_DS1.pcc'
$sceneHash=(Get-FileHash $scene.FilePath).Hash
$stock=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($scene.FindExport('SeekFreeShaderCache'),$null,$null)
$pkg=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($baseline,$null,$true,$false,$null)
$owner=$pkg.FindExport('NPCFaceUser')
if(!$owner -or $owner.ClassName -ne 'RvrEffectsMaterialUser'){throw 'Invalid private effects owner'}
$ownerProperties=(Get-ExportRecord $owner).Properties
if($ownerProperties.m_bSupportsStaticMeshes){throw 'Unexpected static-mesh support; native exclusion would not apply'}
$effects=@($pkg.Exports | Where-Object {$_.ClassName -eq 'Material' -and $_.ObjectName.Name.StartsWith('NPC_CombinedEffect')})
if($effects.Count -ne 17){throw 'Unexpected private effects inventory'}
$cache=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($pkg.FindExport('SeekFreeShaderCache'),$null,$null)
Assert-ShaderCacheMetadata $cache $stock
$cacheHash=Hash-Bytes ($cache.ToBytes($pkg,0))
$originalImports=Import-Snapshot $pkg
$originalNames=@($pkg.Names) | ConvertTo-Json -Compress
$originalFlags=$pkg.Flags
$snapshots=@{}
foreach($e in $pkg.Exports){
    $snapshots[$e.UIndex]=@{Header=Hash-Bytes $e.GenerateHeader();Data=Hash-Bytes $e.Data;Path=$e.InstancedFullPath;Outer=$e.idxLink}
}
$changes=@()
foreach($e in $effects){
    if($e.idxLink -ne 0){throw 'Baseline effect is already parented; reassess experiment'}
    $source=$scene.FindExport(('EffectsMaterials.Users.HMM_HED_PRONPC_MASTER_FACE_MAT_USER.'+$e.ObjectName.Name.Substring(4)))
    if(!$source -or $scene.GetEntry($source.idxLink).ClassName -ne 'RvrEffectsMaterialUser'){throw 'Missing stock effects-user ownership evidence'}
    $sourceOwner=$scene.GetEntry($source.idxLink)
    if((Get-ExportRecord $sourceOwner).Properties.m_bSupportsStaticMeshes){throw 'Unexpected stock static-mesh support'}
    $material=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($e,$null,$null)
    $map=@($cache.MaterialShaderMaps | Where-Object {$_.Value.ID -eq $material.SM3MaterialResource.ID})
    if($map.Count -ne 1){throw 'Missing private shader map'}
    $factories=@($map[0].Value.MeshShaderMaps | ForEach-Object {$_.VertexFactoryType.Name})
    if($factories.Count -ne 2 -or 'FGPUSkinVertexFactory' -notin $factories -or 'FGPUSkinMorphVertexFactory' -notin $factories){throw 'Unexpected effect factories'}
    $before=$e.InstancedFullPath
    $e.idxLink=$owner.UIndex
    $changes+= [ordered]@{Index=$e.UIndex;Before=$before;After=$e.InstancedFullPath;OldOuter=0;NewOuter=$owner.UIndex;StockPath=$source.InstancedFullPath;StockOwnerClass=$sourceOwner.ClassName;Factories=$factories;ShaderMapID=$material.SM3MaterialResource.ID.ToString()}
}
# Capture intended headers after changing ONLY idxLink. Save/reopen must retain
# these headers byte-for-byte (outer changes do not alter serialized data size).
$expectedHeaders=@{}
foreach($e in $pkg.Exports){$expectedHeaders[$e.UIndex]=Hash-Bytes $e.GenerateHeader()}
[LegendaryExplorerCore.Packages.PackageSaver]::Save($pkg,$out,$false,$false,$false,$null)
$reopened=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($out,$null,$true,$false,$null)
if($reopened.Exports.Count -ne $snapshots.Count){throw 'Export count changed'}
foreach($e in $reopened.Exports){
    if((Hash-Bytes $e.GenerateHeader()) -ne $expectedHeaders[$e.UIndex]){throw "Export header changed unexpectedly: $($e.InstancedFullPath)"}
    if((Hash-Bytes $e.Data) -ne $snapshots[$e.UIndex].Data){throw "Export payload changed: $($e.InstancedFullPath)"}
}
foreach($change in $changes){
    $e=$reopened.GetUExport($change.Index)
    if($e.InstancedFullPath -ne $change.After -or $e.idxLink -ne $owner.UIndex){throw 'Ownership round trip failed'}
}
if((Import-Snapshot $reopened) -cne $originalImports){throw 'Imports changed'}
if((@($reopened.Names) | ConvertTo-Json -Compress) -cne $originalNames){throw 'Names changed'}
if($reopened.Flags -ne $originalFlags){throw 'Package flags changed'}
$actual=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($reopened.FindExport('SeekFreeShaderCache'),$null,$null)
Assert-ShaderCacheMetadata $actual $stock
if((Hash-Bytes ($actual.ToBytes($reopened,0))) -ne $cacheHash){throw 'Shader cache changed'}
if((Get-FileHash $scene.FilePath).Hash -ne $sceneHash){throw 'Stock source changed'}
$report=[ordered]@{Baseline=$baseline;BaselineSHA256=(Get-FileHash $baseline).Hash;Output=$out;OutputSHA256=(Get-FileHash $out).Hash;StockSource=$scene.FilePath;StockSHA256=$sceneHash;Changes=$changes;EffectOwnerSupportsStaticMeshes=$false;AllExportPayloadsUnchanged=$true;OnlyEffectOuterHeadersChanged=$true;ImportsNamesFlagsUnchanged=$true;NormalizedShaderCacheSHA256=$cacheHash;ShaderMaps=$actual.MaterialShaderMaps.Count;Shaders=$actual.Shaders.Count;ShaderTypeCRCs=$actual.ShaderTypeCRCMap.Count;VertexFactoryCRCs=$actual.VertexFactoryTypeCRCMap.Count;RoundTripVerified=$true;RuntimeValidated=$false}
$report | ConvertTo-Json -Depth 8 | Set-Content "$evidence/ownership-repair.json"
[pscustomobject]$report | Select-Object OutputSHA256,AllExportPayloadsUnchanged,OnlyEffectOuterHeadersChanged,ShaderMaps,Shaders,RoundTripVerified,RuntimeValidated | ConvertTo-Json
$reopened.Dispose();$pkg.Dispose();$scene.Dispose()
