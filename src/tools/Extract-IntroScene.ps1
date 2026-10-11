. "$PSScriptRoot/Lex-ReadOnly.ps1"
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out="$root/output/intro-a2f"
New-Item -ItemType Directory -Force "$out/audio","$out/meshes","$root/research/intro-a2f" | Out-Null
$p=Open-GamePackage 'BIOA_NOR10_01_DSG_LOC_INT.pcc'
$hash=(Get-FileHash $p.FilePath).Hash
$c=$p.FindExport('nor10_joker_D.nor10_joker_dlg')
$record=Get-ExportRecord $c
$record | ConvertTo-Json -Depth 70 | Set-Content "$root/research/intro-a2f/conversation.json"
$tlk=[LegendaryExplorerCore.TLK.ME1.ME1TalkFile]::new($p.FindExport('nor10_joker_D.tlk_M'))
$texts=@{};foreach($t in $tlk.StringRefs){$texts[$t.StringID]=$t.Data}
$lines=@()
$silent=@()
$reachableEntries=[Collections.Generic.HashSet[int]]::new()
$reachableReplies=[Collections.Generic.HashSet[int]]::new()
$queue=[Collections.Generic.Queue[int]]::new();$queue.Enqueue(1);$queue.Enqueue(25)
while($queue.Count){
    $entry=$queue.Dequeue();if(!$reachableEntries.Add($entry)){continue}
    foreach($reply in $record.Properties.m_EntryList[$entry].ReplyListNew){
        $r=[int]$reply.nIndex;$reachableReplies.Add($r) | Out-Null
        foreach($next in $record.Properties.m_ReplyList[$r].EntryList){$queue.Enqueue([int]$next)}
    }
}
foreach($kind in @('Entry','Reply')){
    $list=$record.Properties.$('m_'+$kind+'List')
    for($i=0;$i -lt $list.Count;$i++){
        if(($kind -eq 'Entry' -and !$reachableEntries.Contains($i)) -or ($kind -eq 'Reply' -and !$reachableReplies.Contains($i))){continue}
        $x=$list[$i]
        if($x.bNonTextLine){continue}
        # -1 is this exact conversation's owner (Joker). Explicit speakers use
        # tags, including entry5's hench_pilot; their numeric slot is not identity.
        $tag=if($kind -eq 'Reply'){'player'}elseif($x.nSpeakerIndex -eq -1){'hench_pilot'}elseif($x.nSpeakerIndex -ge 0 -and $x.nSpeakerIndex -lt $record.Properties.m_SpeakerList.Count){$record.Properties.m_SpeakerList[$x.nSpeakerIndex].sSpeakerTag.Name}else{throw 'Invalid speaker index'}
        $actor=switch($tag){'player'{'Shepard'}'hench_pilot'{'Joker'}'hench_humanmale'{'Kaidan'}default{$null}}
        if(!$actor){continue}
        $id=[int]$x.srText
        $cue=$p.FindExport("nor10_joker_N.VO_${id}_M")
        $wave=$p.FindExport("nor10_joker_N.nor10_joker:VO_${id}_M")
        if(!$cue -and !$wave -and [string]::IsNullOrWhiteSpace($texts[$id])){
            $silent+=@{Kind=$kind;Index=$i;StringRef=$id;Reason='No spoken text and no voice exports; silent graph transition'}
            continue
        }
        if(!$cue -or $cue.ClassName -ne 'SoundCue' -or !$wave -or $wave.ClassName -ne 'SoundNodeWave'){throw "Missing exact voice exports: $kind $i $id"}
        $lines+= [ordered]@{Actor=$actor;Kind=$kind;Index=$i;StringRef=$id;ExportId=$x.nExportID;Cue=$cue.InstancedFullPath;WaveIndex=$wave.UIndex;Text=$texts[$id];Audio="audio/VO_${id}_M.wav";Clip="VO_${id}_M.mefc";Pawn=$(if($actor -eq 'Joker'){'BIOA_NOR10_01_DS1.TheWorld.PersistentLevel.BioPawn_0'}elseif($actor -eq 'Kaidan'){'BIOA_NOR10_01_DS1.TheWorld.PersistentLevel.BioPawn_2'}else{''})}
    }
}
$streaming=@($lines | ForEach-Object {(Get-ExportRecord $p.GetUExport($_.WaveIndex)).Properties.BioStreamingData.Index} | Sort-Object -Unique)
if($streaming.Count -ne 1){throw 'Expected one scene streaming bank'}
$data=$p.GetUExport($streaming[0]).GetBinaryData()
$banks=[LegendaryExplorerCore.Sound.ISACT.ISACTHelper]::GetPairedBanks($data[4..($data.Length-1)])
$chunks=$banks.ICBBank.GetAllBankChunks()
$indices=($chunks | Where-Object {$_.GetType().Name -eq 'ContentIndexBankChunk'}).IndexPages.IndexEntries
$samples=$banks.ISBBank.GetAllBankChunks()
$isb=Join-Path (Split-Path $GameContent) 'Content/Packages/ISACT/nor10_joker.isb'
$isbHash=(Get-FileHash $isb).Hash
$stream=[IO.File]::OpenRead($isb)
try{
    foreach($line in $lines){
        $title="VO_$($line.StringRef)_M"
        $idx=@($indices | Where-Object Title -eq $title)
        if($idx.Count -ne 1){throw "Ambiguous index $title"}
        $event=@($chunks | Where-Object {$_.GetType().Name -eq 'ISACTListBankChunk' -and ($_.GetAllSubChunks() | Where-Object {$_.ChunkName -eq 'indx' -and $_.Value -eq $idx[0].ObjectIndex})})
        if($event.Count -ne 1){throw "Ambiguous event $title"}
        $tracks=@($event[0].GetAllSubChunks() | Where-Object {$_.GetType().Name -match '^SoundEventSoundTracks'}).SoundTracks
        if($tracks.Count -ne 1){throw "Unexpected tracks $title"}
        $sampleIndex=$tracks[0].BufferIndex -band 0xffff
        $sample=@($samples | Where-Object {$_.GetType().Name -eq 'ISACTListBankChunk' -and $_.ObjectType -eq 'samp' -and ($_.GetAllSubChunks() | Where-Object {$_.ChunkName -eq 'indx' -and $_.Value -eq $sampleIndex})})
        if($sample.Count -ne 1 -or $sample[0].CompressionInfo.CurrentFormat.ToString() -ne 'OGGVORBIS'){throw "Unexpected sample $title"}
        $s=$sample[0];$stream.Position=$s.SampleOffset;$bytes=[byte[]]::new($s.CompressionInfo.TotalSize);$stream.ReadExactly($bytes)
        [IO.File]::WriteAllBytes("$out/audio/$title.ogg",$bytes)
        & "$root/tools/ffmpeg/ffmpeg-9.0.2-essentials_build/bin/ffmpeg.exe" -hide_banner -loglevel error -y -i "$out/audio/$title.ogg" -ac 1 -ar 16000 -c:a pcm_s16le "$out/audio/$title.wav"
        if($LASTEXITCODE -ne 0){throw "Decode failed $title"}
        $line.SourceSampleIndex=$sampleIndex;$line.SourceSampleOffset=$s.SampleOffset;$line.CompressedSHA256=(Get-FileHash "$out/audio/$title.ogg").Hash
    }
}finally{$stream.Dispose()}
if((Get-FileHash $p.FilePath).Hash -ne $hash -or (Get-FileHash $isb).Hash -ne $isbHash){throw 'Original audio source changed'}
@{Conversation=$c.InstancedFullPath;ResRef=205;Language='INT';OpeningStartEntries=@(1,25);ReachableEntries=@($reachableEntries | Sort-Object);ReachableReplies=@($reachableReplies | Sort-Object);SourcePackageSHA256=$hash;AudioBankSHA256=$isbHash;Lines=$lines;SilentTransitions=$silent;ExcludedSpeakers=@('nor10_nihlus','nor10_captain','nor10_jenkins')} | ConvertTo-Json -Depth 12 | Set-Content "$out/scene-manifest.json"
$p.Dispose()
foreach($actor in @('Shepard','Joker','Kaidan','Average')){
    $source=Open-GamePackage $(if($actor -eq 'Shepard'){'BIOG_Male_Player_C.pcc'}else{'BIOA_NOR10_01_DS1.pcc'})
    $asset=switch($actor){'Shepard'{'Sheppard'}'Kaidan'{'Kaiden'}default{$actor}}
    $e=$source.FindExport("BIOG_HMM_HED_PROMorph.$asset.HMM_HED_PRO${asset}_MDL")
    $mesh=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($e,$null,$null)
    $lods=@()
    foreach($lod in $mesh.LODModels){
        $vertices=@($null)*$lod.NumVertices
        foreach($chunk in $lod.Chunks){
            for($i=$chunk.BaseVertexIndex;$i -lt $chunk.BaseVertexIndex+$chunk.NumRigidVertices+$chunk.NumSoftVertices;$i++){
                $v=$lod.VertexBufferGPUSkin.VertexData[$i];$weights=@{}
                for($j=0;$j -lt 4;$j++){if($v.InfluenceWeights[$j]){$name=$mesh.RefSkeleton[$chunk.BoneMap[$v.InfluenceBones[$j]]].Name.Name;$weights[$name]=[double]$v.InfluenceWeights[$j]/255}}
                $vertices[$i]=@{Id=$i;Position=@($v.Position.X,$v.Position.Y,$v.Position.Z);Weights=$weights}
            }
        }
        if(@($vertices | Where-Object {$null -eq $_}).Count){throw 'Incomplete render vertices'}
        $lods+=,@{Vertices=$vertices;NumVertices=$lod.NumVertices}
    }
    @{Actor=$actor;Mesh=$e.InstancedFullPath;SourcePackageSHA256=(Get-FileHash $source.FilePath).Hash;SourceExportSHA256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($e.Data));LODs=$lods} | ConvertTo-Json -Depth 12 -Compress | Set-Content "$out/meshes/$actor.json"
    $source.Dispose()
}
[pscustomobject]@{Lines=$lines.Count;Shepard=@($lines | Where-Object Actor -eq 'Shepard').Count;Joker=@($lines | Where-Object Actor -eq 'Joker').Count;Kaidan=@($lines | Where-Object Actor -eq 'Kaidan').Count;OriginalAudioUnchanged=$true} | ConvertTo-Json
