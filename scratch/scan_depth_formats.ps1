$dllPath = 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\rendersystemdx11.dll'
$bytes = [System.IO.File]::ReadAllBytes($dllPath)

# Check for DXGI formats used with D3D11_BIND_DEPTH_STENCIL (0x40)
# DXGI_FORMAT_R32_TYPELESS = 39 (0x27)
# DXGI_FORMAT_D32_FLOAT = 40 (0x28)
# DXGI_FORMAT_R24G8_TYPELESS = 44 (0x2C)
# DXGI_FORMAT_D24_UNORM_S8_UINT = 45 (0x2D)
# DXGI_FORMAT_R32G8X24_TYPELESS = 19 (0x13)
# DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20 (0x14)
# DXGI_FORMAT_R16_TYPELESS = 53 (0x35)
# DXGI_FORMAT_D16_UNORM = 54 (0x36)

Write-Host "Scanning rendersystemdx11 for depth formats..."
$f32 = 0; $f24 = 0; $f32s8 = 0; $f16 = 0
for ($i = 0; $i -lt $bytes.Length - 4; $i++) {
    $v = [System.BitConverter]::ToUInt32($bytes, $i)
    if ($v -eq 39 -or $v -eq 40) { $f32++ }
    if ($v -eq 44 -or $v -eq 45) { $f24++ }
    if ($v -eq 19 -or $v -eq 20) { $f32s8++ }
    if ($v -eq 53 -or $v -eq 54) { $f16++ }
}
Write-Host "Format matches - R32: $f32, R24: $f24, R32G8X24: $f32s8, R16: $f16"
