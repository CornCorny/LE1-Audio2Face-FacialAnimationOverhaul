$ErrorActionPreference = 'Stop'
$LexDirectory = $env:LEGENDARY_EXPLORER_DIR
if (!$LexDirectory) { throw 'Set LEGENDARY_EXPLORER_DIR to the Legendary Explorer application directory' }
$GameContent = Join-Path $env:LE_INSTALL_DIR 'Game/ME1/BioGame/CookedPCConsole'
[System.Runtime.InteropServices.NativeLibrary]::Load((Join-Path $LexDirectory 'runtimes\win-x64\native\oo2core_8_win64.dll')) | Out-Null
[Reflection.Assembly]::LoadFrom((Join-Path $LexDirectory 'LegendaryExplorerCore.dll')) | Out-Null
[LegendaryExplorerCore.LegendaryExplorerCoreLib]::InitLib([Threading.Tasks.TaskScheduler]::Default, $null, $null, [LegendaryExplorerCore.Packages.MEGame[]]@([LegendaryExplorerCore.Packages.MEGame]::LE1), $false)
function Open-GamePackage([string]$Name) {
    [LegendaryExplorerCore.Packages.MEPackageHandler]::OpenMEPackage((Join-Path $GameContent $Name))
}
function Convert-UEProperty($Property, $Package) {
    $type = $Property.GetType().Name
    if ($type -eq 'StructProperty') {
        $value = [ordered]@{}
        foreach ($child in $Property.Properties) { $value[$child.Name.ToString()] = Convert-UEProperty $child $Package }
        return $value
    }
    if ($type -like 'ArrayProperty*') {
        $values = @()
        foreach ($child in $Property) { $values += ,(Convert-UEProperty $child $Package) }
        return ,$values
    }
    if ($type -eq 'ObjectProperty') {
        $entry = $Package.GetEntry($Property.Value)
        return @{Index=$Property.Value; Path=$entry.InstancedFullPath; Class=$entry.ClassName}
    }
    return $Property.Value
}
function Get-ExportRecord($Export) {
    $properties = [ordered]@{}
    foreach ($prop in $Export.GetProperties()) { $properties[$prop.Name.ToString()] = Convert-UEProperty $prop $Export.FileRef }
    return [ordered]@{Package=$Export.FileRef.FilePath; Index=$Export.UIndex; Path=$Export.InstancedFullPath; Class=$Export.ClassName; Properties=$properties}
}
