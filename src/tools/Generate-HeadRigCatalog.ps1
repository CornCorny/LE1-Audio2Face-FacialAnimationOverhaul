. "$PSScriptRoot/Lex-ReadOnly.ps1"
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$study="$root/research/intro-a2f/correction-0.2.8"
$inventory=Get-Content "$study/head-inventory.json" -Raw | ConvertFrom-Json
$profiles=@()
foreach($file in Get-ChildItem "$root/output/single-line-bridge/assets" -Filter 'MEF*Bridge.pcc'){
    $p=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($file.FullName,$null,$true,$false,$null)
    try {
        foreach($set in @($p.Exports | Where-Object ClassName -eq 'MorphTargetSet')){
            $record=Get-ExportRecord $set;$mesh=$record.Properties.BaseSkelMesh.Path
            $sources=@($inventory | Where-Object Mesh -eq $mesh)
            if(!$sources.Count -or @($sources.GeometrySHA256 | Sort-Object -Unique).Count -ne 1){throw "Unknown/ambiguous geometry for $mesh"}
            if($record.Properties.Targets.Count -ne 48){throw 'Unexpected channel schema'}
            $entry=$sources[0];$player=$file.BaseName -eq 'MEFSingleLineBridge'
            $profiles+= [pscustomobject]@{Object=$file.BaseName+'.'+$set.InstancedFullPath;BaseSkelMesh=$mesh;Bones=$entry.Bones.Count;LODs=$entry.LODVertices.Count;MaxLOD=[Math]::Min(1,$entry.LODVertices.Count-1);PlayerOnly=$player;GeometrySHA256=$entry.GeometrySHA256}
        }
    } finally {$p.Dispose()}
}
$profiles=@($profiles | Sort-Object @{Expression='PlayerOnly';Descending=$true},Object)
if($profiles.Count -ne 5){throw 'Unexpected scoped profile count; review package coverage before expanding'}
$lines=@('#pragma once','// Generated from authored MorphTargetSets and verified mesh geometry, not NPC locations.','inline constexpr TestRig TestRigs[] = {')
foreach($profile in $profiles){$flag=if($profile.PlayerOnly){'true'}else{'false'};$lines+='    {L"'+$profile.Object+'", '+$profile.MaxLOD+', '+$profile.Bones+', '+$profile.LODs+', '+$flag+'},'}
$lines+='};';$lines | Set-Content "$root/src/asi/single-line-bridge/GeneratedHeadRigs.hpp"
$profiles | ConvertTo-Json -Depth 4 | Set-Content "$study/head-rig-catalog.json"
$profiles | Select-Object Object,Bones,LODs,MaxLOD,PlayerOnly | ConvertTo-Json
