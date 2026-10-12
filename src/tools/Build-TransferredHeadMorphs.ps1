param([string]$Profile='HumanMale2266')
. "$PSScriptRoot/Lex-ReadOnly.ps1"
[LegendaryExplorerCore.Packages.PackageSaver]::PackageSaveFailedCallback=[Action[string]]{param($message) throw $message}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$study="$root/research/intro-a2f/correction-0.2.8"
$rig=Get-Content "$study/rig/$Profile-rig.json" -Raw | ConvertFrom-Json
$baseline="$root/output/intro-a2f/release/MEFaces-IntroA2F-0.2.7/Game/ME1/BioGame/CookedPCConsole/MEFAverageBridge.pcc"
function N([string]$s){[LegendaryExplorerCore.Unreal.NameReference]::new($s,0)}
$pkg=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($baseline,$null,$true,$false,$null)
try {
    $set=$pkg.FindExport('AverageBridgeSet');$props=$set.GetProperties()
    $baseProp=$props.GetProp[LegendaryExplorerCore.Unreal.ObjectProperty]('BaseSkelMesh')
    $base=$pkg.GetEntry($baseProp.Value)
    if($base.ClassName -ne 'SkeletalMesh' -or $base.UIndex -ge 0){throw 'Expected imported base mesh'}
    $segments=$rig.Mesh.Split('.')
    if($segments.Length -ne 3 -or $base.Parent.Parent.InstancedFullPath -ne $segments[0]){throw 'Unsupported import hierarchy; do not guess linkage'}
    $base.Parent.ObjectName=N $segments[1];$base.ObjectName=N $segments[2]
    $set.ObjectName=N ($Profile+'BridgeSet')
    $targetList=(Get-ExportRecord $set).Properties.Targets
    if($targetList.Count -ne 48 -or $rig.Targets.Count -ne 48){throw 'Unexpected schema'}
    $payloadHashes=@()
    for($c=0;$c -lt 48;$c++){
        $target=$pkg.GetUExport($targetList[$c].Index);$shape=$rig.Targets[$c]
        if($target.ObjectName.Name -ne $shape.Name){throw 'Target order mismatch'}
        $binary=[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget]::Create()
        $lods=@()
        foreach($lod in $shape.LODs){
            $model=[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget+MorphLODModel]::new();$model.NumBaseMeshVerts=$lod.NumVertices
            $vertices=[Collections.Generic.List[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget+MorphVertex]]::new();$seen=[Collections.Generic.HashSet[int]]::new()
            foreach($v in $lod.Vertices){
                if(!$seen.Add([int]$v[0]) -or $v[0] -ge $lod.NumVertices -or $v[0] -lt 0 -or $v[0] -gt 65535){throw 'Invalid vertex index'}
                $delta=[Numerics.Vector3]::new($v[1],$v[2],$v[3])
                if(![float]::IsFinite($delta.Length()) -or $delta.Length() -ge 1.6){throw 'Invalid displacement'}
                $mv=[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget+MorphVertex]::new();$mv.SourceIdx=[uint16]$v[0];$mv.PositionDelta=$delta
                $mv.TangentZDelta=[LegendaryExplorerCore.Unreal.BinaryConverters.PackedNormal]::new(128,128,128,128);$vertices.Add($mv)
            }
            $model.Vertices=$vertices.ToArray();$lods+=$model
        }
        $binary.MorphLODModels=$lods;$target.WriteBinary($binary,$null)
        $payloadHashes+=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($target.GetBinaryData()))
    }
    $dest="$root/output/single-line-bridge/assets/MEF${Profile}Bridge.pcc"
    [LegendaryExplorerCore.Packages.PackageSaver]::Save($pkg,$dest,$false,$false,$false,$null)
}finally{$pkg.Dispose()}
$r=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($dest,$null,$true,$false,$null)
try {
    $rs=$r.FindExport($Profile+'BridgeSet');$rr=(Get-ExportRecord $rs).Properties
    if($rr.BaseSkelMesh.Path -ne $rig.Mesh -or $rr.Targets.Count -ne 48){throw 'Base mesh/schema roundtrip failed'}
    for($c=0;$c -lt 48;$c++){
        $e=$r.GetUExport($rr.Targets[$c].Index);$b=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($e,$null,$null)
        if([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($e.GetBinaryData())) -ne $payloadHashes[$c] -or $b.BoneOffsets.Length){throw 'Payload roundtrip failed'}
        if($b.MorphLODModels.Length -ne $rig.Targets[$c].LODs.Count){throw 'LOD roundtrip failed'}
        for($l=0;$l -lt $b.MorphLODModels.Length;$l++){if($b.MorphLODModels[$l].NumBaseMeshVerts -ne $rig.Targets[$c].LODs[$l].NumVertices){throw 'Vertex count roundtrip failed'}}
    }
    if(@($r.Exports | Where-Object ClassName -in @('SkeletalMesh','Material','MaterialInstanceConstant')).Count){throw 'Unexpected embedded geometry/material'}
    $audit=@{Profile=$Profile;BaseSkelMesh=$rig.Mesh;Channels=48;RoundTripVerified=$true;SourceMeshEmbedded=$false;BoneOffsets=0;Path=$dest;SHA256=(Get-FileHash $dest).Hash;RuntimeValidated=$false}
    $audit | ConvertTo-Json | Set-Content "$study/morph-roundtrip.json";$audit | ConvertTo-Json
}finally{$r.Dispose()}
