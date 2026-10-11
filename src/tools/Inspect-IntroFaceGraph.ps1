. "$PSScriptRoot/Lex-ReadOnly.ps1"
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
New-Item -ItemType Directory -Force "$root/research/intro-a2f/ownership" | Out-Null
Add-Type -TypeDefinition @'
using System;using System.Collections;using System.Collections.Generic;using System.Reflection;using System.Text.Json;
public static class MEFFXInspect {
 static object Value(object o,int depth){if(o==null||depth>15)return null;var t=o.GetType();if(t.IsPrimitive||o is string||o is decimal)return o;if(t.IsEnum)return o.ToString();if(o is IEnumerable e){var l=new List<object>();foreach(var v in e)l.Add(Value(v,depth+1));return l;}var d=new Dictionary<string,object>();foreach(var f in t.GetFields(BindingFlags.Public|BindingFlags.Instance))d[f.Name]=Value(f.GetValue(o),depth+1);if(d.Count==0)foreach(var p in t.GetProperties(BindingFlags.Public|BindingFlags.Instance))if(p.CanRead&&p.GetIndexParameters().Length==0&&p.Name!="Export")d[p.Name]=Value(p.GetValue(o),depth+1);return d;}
 public static string Dump(object o){return JsonSerializer.Serialize(Value(o,0),new JsonSerializerOptions{NumberHandling=System.Text.Json.Serialization.JsonNumberHandling.AllowNamedFloatingPointLiterals});}
}
'@
$result=@()
foreach($actor in @('Shepard','Average','Joker','Kaidan')){
 $p=Open-GamePackage $(if($actor -eq 'Shepard'){'BIOG_Male_Player_C.pcc'}else{'BIOA_NOR10_01_DS1.pcc'})
 $key=switch($actor){'Shepard'{'Sheppard'}'Kaidan'{'Kaiden'}default{$actor}}
 $e=$p.FindExport("BIOG_HMM_HED_PROMorph.$key.HMM_HED_PRO${key}_MDL")
 $m=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($e,$null,$null)
 $fx=$p.FindExport('BIOG_FaceFX_Assets.SFX_HumanMale_FaceFX')
 if($fx -isnot [LegendaryExplorerCore.Packages.ExportEntry]){throw "Face asset is import: $($fx.InstancedFullPath)"}
 $f=[LegendaryExplorerCore.Unreal.BinaryConverters.ObjectBinary]::From($fx,$null,$null)
 $raw=[MEFFXInspect]::Dump($f) | ConvertFrom-Json
 $nodes=@();for($i=0;$i -lt $raw.CombinerNodes.Count;$i++){$n=$raw.CombinerNodes[$i];$nodes+=@{Index=$i;Name=$raw.Names[$n.Name];Data=$n}}
 $bones=@();foreach($b in $raw.BoneNodes){$bones+=@{Name=$raw.Names[$b.BoneName];Data=$b}}
 $lods=@();foreach($lod in $m.LODModels){$lods+=,@{Indices=$lod.IndexBuffer;NumVertices=$lod.NumVertices}}
 $r=@{Actor=$actor;Mesh=$e.InstancedFullPath;Asset=$fx.InstancedFullPath;AssetExport=$fx.UIndex;Names=$f.Names;Nodes=$nodes;Bones=$bones;Skeleton=$m.RefSkeleton;LODs=$lods}
 $r | ConvertTo-Json -Depth 25 -Compress | Set-Content "$root/research/intro-a2f/ownership/$actor-facegraph.json"
 $result+=@{Actor=$actor;Asset=$fx.InstancedFullPath;NodeCount=$nodes.Count;BoneCount=$bones.Count}
 $p.Dispose()
}
$result | ConvertTo-Json
