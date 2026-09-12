$bytes = [System.IO.File]::ReadAllBytes('C:\Users\stass\OneDrive\Documents\koch.wav')
$sb = [System.Text.StringBuilder]::new("// Auto-generated from koch.wav`n#pragma once`n#include <cstddef>`nnamespace features::misc::sounds {`ninline constexpr unsigned char g_koch_wav[] = {`n")
for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($i -gt 0 -and $i % 24 -eq 0) {
        [void]$sb.Append("`n")
    }
    [void]$sb.Append('0x' + $bytes[$i].ToString('x2') + ', ')
}
[void]$sb.Append("`n};`ninline constexpr std::size_t g_koch_wav_size = sizeof(g_koch_wav);`n}`n")
[System.IO.File]::WriteAllText('c:\Users\stass\OneDrive\Documents\OneTap\gaycity\mintaly-cs2\project\core\features\misc\koch_sound.hpp', $sb.ToString())
Write-Host "Generated successfully, byte count: $($bytes.Length)"
