$path = 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\rendersystemdx11.dll'
$b = [System.IO.File]::ReadAllBytes($path)
$c33 = 0
$c34 = 0
for ($i = 0; $i -lt $b.Length - 6; $i++) {
    if ($b[$i] -eq 0xFF -and ($b[$i+1] -band 0xF8) -eq 0x90 -and $b[$i+2] -eq 0x08 -and $b[$i+3] -eq 0x01 -and $b[$i+4] -eq 0x00 -and $b[$i+5] -eq 0x00) {
        $c33++
    }
    if ($b[$i] -eq 0xFF -and ($b[$i+1] -band 0xF8) -eq 0x90 -and $b[$i+2] -eq 0x10 -and $b[$i+3] -eq 0x01 -and $b[$i+4] -eq 0x00 -and $b[$i+5] -eq 0x00) {
        $c34++
    }
}
Write-Host "Vtable 33 calls: $c33"
Write-Host "Vtable 34 calls: $c34"
