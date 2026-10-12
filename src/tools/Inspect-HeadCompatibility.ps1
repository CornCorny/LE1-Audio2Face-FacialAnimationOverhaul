. "$PSScriptRoot/Lex-ReadOnly.ps1"
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out="$root/research/intro-a2f/correction-0.2.8"
New-Item -ItemType Directory -Force $out | Out-Null
$cp=Join-Path (Split-Path $GameContent) 'DLC/DLC_MOD_LE1CP/CookedPCConsole'
$sources=@((Join-Path $GameContent 'BIOA_NOR10_01_DS1.pcc'), (Join-Path $GameContent 'BIOG_Male_Player_C.pcc'))
$sources+=@(Get-ChildItem -LiteralPath $cp -Filter 'BIONPC*.pcc' -Recurse | Where-Object {$_.Name -notmatch '_LOC_' -and $_.Name -match 'Joker|Kaidan'} | ForEach-Object FullName)
$records=@()
foreach($path in $sources){
    $p=[LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage($path,$null,$true,$false,$null)
    try {
        foreach($e in @($p.Exports | Where-Object {$_.ClassName -eq 'SkeletalMesh' -and $_.InstancedFullPath -match 'HED_PRO'})){
            $m=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($e,$null,$null)
            $canonical=[ordered]@{Skeleton=$m.RefSkeleton;Origin=$m.Origin;RotOrigin=$m.RotOrigin;LODs=@(foreach($lod in $m.LODModels){[ordered]@{Vertices=$lod.VertexBufferGPUSkin.VertexData;Indices=$lod.IndexBuffer;NumVertices=$lod.NumVertices;Sections=$lod.Sections;Chunks=@(foreach($chunk in $lod.Chunks){[ordered]@{BoneMap=$chunk.BoneMap;BaseVertexIndex=$chunk.BaseVertexIndex;NumRigidVertices=$chunk.NumRigidVertices;NumSoftVertices=$chunk.NumSoftVertices}})}})}
            $geometryHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($canonical | ConvertTo-Json -Depth 18 -Compress))))
            $record=[ordered]@{Package=$path;Mesh=$e.InstancedFullPath;Index=$e.UIndex;ExportSHA256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($e.Data));Bones=@($m.RefSkeleton | ForEach-Object {$_.Name.Name});LODVertices=@($m.LODModels.NumVertices);Materials=@($m.Materials | ForEach-Object {$p.GetEntry($_).InstancedFullPath});MeshFields=@($m.GetType().GetFields().Name)}
            $records+=$record
            $record.GeometrySHA256=$geometryHash
            if($path -match 'BIONPC_Kaidan_Normandy'){
                $lods=@()
                foreach($lod in $m.LODModels){
                    $vertices=@($null)*$lod.NumVertices
                    foreach($chunk in $lod.Chunks){for($i=$chunk.BaseVertexIndex;$i -lt $chunk.BaseVertexIndex+$chunk.NumRigidVertices+$chunk.NumSoftVertices;$i++){
                        $v=$lod.VertexBufferGPUSkin.VertexData[$i];$weights=@{}
                        for($j=0;$j -lt 4;$j++){if($v.InfluenceWeights[$j]){$name=$m.RefSkeleton[$chunk.BoneMap[$v.InfluenceBones[$j]]].Name.Name;$weights[$name]=[double]$v.InfluenceWeights[$j]/255}}
                        $vertices[$i]=@{Id=$i;Position=@($v.Position.X,$v.Position.Y,$v.Position.Z);Weights=$weights}
                    }}
                    $lods+=,@{Vertices=$vertices;NumVertices=$lod.NumVertices}
                }
                @{Actor='HumanMale2266';Mesh=$e.InstancedFullPath;SourcePackageSHA256=(Get-FileHash $path).Hash;SourceExportSHA256=$record.ExportSHA256;LODs=$lods;Indices=@($m.LODModels[0].IndexBuffer);Bones=$record.Bones} | ConvertTo-Json -Depth 12 -Compress | Set-Content "$out/HumanMale2266.json"
                $m.RefSkeleton | ConvertTo-Json -Depth 6 | Set-Content "$out/ref-skeleton.json"
            }
        }
        if($path -match 'BIONPC'){
            $p.Exports | Where-Object {$_.ClassName -eq 'BioPawn'} | ForEach-Object {Get-ExportRecord $_} | ConvertTo-Json -Depth 18 | Set-Content "$out/$([IO.Path]::GetFileNameWithoutExtension($path))-pawns.json"
            $p.Exports | Where-Object {$_.ClassName -match 'Material|SkeletalMeshComponent'} | ForEach-Object {Get-ExportRecord $_} | ConvertTo-Json -Depth 20 | Set-Content "$out/$([IO.Path]::GetFileNameWithoutExtension($path))-materials.json"
        }
    } finally {$p.Dispose()}
}
$records | ConvertTo-Json -Depth 8 | Set-Content "$out/head-inventory.json"
$records | ForEach-Object {[pscustomobject]@{Package=[IO.Path]::GetFileName($_.Package);Mesh=$_.Mesh;LODs=$_.LODVertices -join ','}} | ConvertTo-Json
