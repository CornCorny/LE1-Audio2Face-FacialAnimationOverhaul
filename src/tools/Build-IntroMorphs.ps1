. "$PSScriptRoot/Lex-ReadOnly.ps1"
[LegendaryExplorerCore.Packages.PackageSaver]::PackageSaveFailedCallback=[Action[string]]{param($message) throw $message}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function N([string]$s){[LegendaryExplorerCore.Unreal.NameReference]::new($s,0)}
function Hash-Bytes([byte[]]$b){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($b))}
$reports=@()
foreach($actor in @('Shepard','Joker','Kaidan','Average')){
    $name=if($actor -eq 'Shepard'){'MEFSingleLineBridge'}else{"MEF${actor}Bridge"}
    $baseline="$root/output/single-line-bridge/release/MEF-SingleLineBridge-0.1.15/Game/ME1/BioGame/CookedPCConsole/$name.pcc"
    $rig=Get-Content "$root/output/intro-a2f/meshes/$actor-rig.json" -Raw | ConvertFrom-Json
    $channelCount=@(Get-Content "$root/output/intro-a2f/channels.json" -Raw | ConvertFrom-Json).Count
    if($channelCount -lt 38 -or $channelCount -gt 64 -or $rig.Targets.Count -ne $channelCount){throw 'Unexpected facial channel count'}
    $pkg=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($baseline,$null,$true,$false,$null)
    $set=$pkg.FindExport("${actor}BridgeSet")
    if((Get-ExportRecord $set).Properties.BaseSkelMesh.Path -ne $rig.Mesh){throw 'Mesh path mismatch'}
    $targetList=[LegendaryExplorerCore.Unreal.ArrayProperty[LegendaryExplorerCore.Unreal.ObjectProperty]]::new((N 'Targets'))
    $hashes=@{};$audit=@()
    for($c=0;$c -lt $rig.Targets.Count;$c++){
        $shape=$rig.Targets[$c]
        $target=if($c -eq 0){$pkg.FindExport("${actor}BridgeSet.MEF_SL_CornerPull")}else{[LegendaryExplorerCore.Packages.ExportCreator]::CreateExport($pkg,(N $shape.Name),'MorphTarget',$set,$null,$false,$false,$null,$null)}
        if(!$target){throw 'Missing baseline target'}
        $target.ObjectName=N $shape.Name
        $target.ObjectFlags=$target.ObjectFlags -bor [LegendaryExplorerCore.Unreal.UnrealFlags+EObjectFlags]::Public
        $binary=[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget]::Create()
        $lods=@()
        foreach($lod in $shape.LODs){
            $model=[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget+MorphLODModel]::new()
            $model.NumBaseMeshVerts=$lod.NumVertices
            $vertices=[Collections.Generic.List[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget+MorphVertex]]::new()
            $seen=[Collections.Generic.HashSet[int]]::new()
            foreach($v in $lod.Vertices){
                if(!$seen.Add([int]$v[0]) -or $v[0] -ge $lod.NumVertices -or $v[0] -lt 0 -or $v[0] -gt 65535){throw 'Invalid render vertex ID'}
                $delta=[Numerics.Vector3]::new($v[1],$v[2],$v[3])
                if(![float]::IsFinite($delta.Length()) -or $delta.Length() -gt 1.601){throw 'Unsafe displacement'}
                $mv=[LegendaryExplorerCore.Unreal.BinaryConverters.MorphTarget+MorphVertex]::new()
                $mv.SourceIdx=[uint16]$v[0];$mv.PositionDelta=$delta;$mv.TangentZDelta=[LegendaryExplorerCore.Unreal.BinaryConverters.PackedNormal]::new(128,128,128,128)
                $vertices.Add($mv)
            }
            $model.Vertices=$vertices.ToArray();$lods+=$model
        }
        $binary.MorphLODModels=$lods
        if($binary.BoneOffsets.Length){throw 'Unexpected bone changes'}
        $target.WriteBinary($binary,$null);$hashes[$target.UIndex]=Hash-Bytes $target.GetBinaryData()
        $targetList.Add([LegendaryExplorerCore.Unreal.ObjectProperty]::new($target.UIndex,$null))
        $audit+=@{Channel=$shape.Channel;Target=$target.InstancedFullPath;Index=$target.UIndex;LODVertices=@($lods | ForEach-Object {$_.Vertices.Length})}
    }
    $props=$set.GetProperties();$props.AddOrReplaceProp($targetList);$set.WriteProperties($props)
    $dest="$root/output/single-line-bridge/assets/$name.pcc"
    [LegendaryExplorerCore.Packages.PackageSaver]::Save($pkg,$dest,$false,$false,$false,$null)
    $r=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($dest,$null,$true,$false,$null)
    $rs=$r.FindExport("${actor}BridgeSet");$rr=(Get-ExportRecord $rs).Properties
    if($rr.BaseSkelMesh.Path -ne $rig.Mesh -or $rr.Targets.Count -ne $channelCount){throw 'Set linkage round trip failed'}
    for($i=0;$i -lt $channelCount;$i++){
        $target=$r.GetUExport($rr.Targets[$i].Index)
        $binary=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($target,$null,$null)
        if((Hash-Bytes $target.GetBinaryData()) -ne $hashes[$target.UIndex] -or $binary.BoneOffsets.Length -or $target.ObjectName.Name -ne $rig.Targets[$i].Name){throw 'Morph payload/order round trip failed'}
    }
    if(@($r.Exports | Where-Object ClassName -in @('SkeletalMesh','Material','MaterialInstanceConstant')).Count){throw 'Unexpected embedded base mesh/material'}
    $reports+=@{Actor=$actor;Output=$dest;OutputSHA256=(Get-FileHash $dest).Hash;Mesh=$rig.Mesh;Targets=$audit;BinaryRoundTripIdentical=$true;BoneOffsets=0;OriginalMeshSkeletonMaterialsUnmodified=$true;RuntimeValidated=$false}
    $r.Dispose();$pkg.Dispose()
}
$reports | ConvertTo-Json -Depth 10 | Set-Content "$root/research/intro-a2f/morph-roundtrip.json"
$reports | ForEach-Object {[pscustomobject]@{Actor=$_.Actor;Channels=$_.Targets.Count;BinaryRoundTripIdentical=$_.BinaryRoundTripIdentical;SHA256=$_.OutputSHA256}} | ConvertTo-Json
