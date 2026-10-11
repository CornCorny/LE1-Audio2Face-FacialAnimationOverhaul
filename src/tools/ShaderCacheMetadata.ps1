# Use exact installed-game metadata, not the aggregate returned by
# GetAllShadersForMaterials: that aggregate can drop both CRC tables on merge.
function Get-CrcTableSignature($Table) {
    (@($Table | ForEach-Object { '{0}={1:X8}' -f $_.Key.ToString(),[uint32]$_.Value } | Sort-Object) -join "`n")
}
function Assert-ShaderCacheMetadata($Cache,$Stock) {
    foreach($field in @('ShaderTypeCRCMap','VertexFactoryTypeCRCMap')) {
        if(!$Stock.$field.Count){throw "Stock cache has no $field"}
        if((Get-CrcTableSignature $Cache.$field) -cne (Get-CrcTableSignature $Stock.$field)) {
            throw "Cache $field differs from installed-game metadata"
        }
    }
    $shaderTypes=@{}; $factoryTypes=@{}
    foreach($entry in $Cache.ShaderTypeCRCMap){$shaderTypes[$entry.Key.ToString()]=$true}
    foreach($entry in $Cache.VertexFactoryTypeCRCMap){$factoryTypes[$entry.Key.ToString()]=$true}
    function Require-Type($Type,$Table,[string]$Kind) {
        $name=$Type.ToString()
        if(!$Table.ContainsKey($name)){throw "Missing $Kind checksum: $name"}
    }
    foreach($entry in $Cache.Shaders) {
        Require-Type $entry.Value.ShaderType $shaderTypes 'shader'
        $vf=$entry.Value.VertexFactoryType
        if($vf.Name -and $vf.Name -ne 'None'){Require-Type $vf $factoryTypes 'factory'}
        $vp=$entry.Value.VertexFactoryParameters
        if($vp.VertexFactoryType.Name -and $vp.VertexFactoryType.Name -ne 'None') {
            Require-Type $vp.VertexFactoryType $factoryTypes 'factory parameter'
        }
    }
    foreach($entry in $Cache.MaterialShaderMaps) {
        foreach($ref in $entry.Value.Shaders) {
            Require-Type $ref.Key $shaderTypes 'map shader'
            Require-Type $ref.Value.ShaderType $shaderTypes 'map shader reference'
        }
        foreach($mesh in $entry.Value.MeshShaderMaps) {
            Require-Type $mesh.VertexFactoryType $factoryTypes 'mesh factory'
            foreach($ref in $mesh.Shaders) {
                Require-Type $ref.Key $shaderTypes 'mesh shader'
                Require-Type $ref.Value.ShaderType $shaderTypes 'mesh shader reference'
            }
        }
    }
}
function Set-StockShaderCacheMetadata($Cache,$Stock) {
    $Cache.ShaderTypeCRCMap=$Stock.ShaderTypeCRCMap
    $Cache.VertexFactoryTypeCRCMap=$Stock.VertexFactoryTypeCRCMap
    Assert-ShaderCacheMetadata $Cache $Stock
}
