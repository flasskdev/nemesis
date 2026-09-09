$dllPath = 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\rendersystemdx11.dll'
$bytes = [System.IO.File]::ReadAllBytes($dllPath)
$count108 = 0
$count110 = 0
for ($i = 0; $i -lt $bytes.Length - 6; $i++) {
    if ($bytes[$i] -eq 0xFF -and ($bytes[$i+1] -band 0xF8) -eq 0x90) {
        $off = [System.BitConverter]::ToUInt32($bytes, $i+2)
        if ($off -eq 0x108) { $count108++ }
        elseif ($off -eq 0x110) { $count110++ }
    }
}
Write-Host "Calls to 0x108 (OMSetRenderTargets): $count108"
Write-Host "Calls to 0x110 (OMSetRenderTargetsAndUnorderedAccessViews): $count110"
