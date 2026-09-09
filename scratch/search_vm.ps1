$clientDll = 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll'
$bytes = [System.IO.File]::ReadAllBytes($clientDll)

# Search for strings related to viewmodel depth or hudmodel
$str1 = [System.Text.Encoding]::ASCII.GetBytes("HudModel")
$str2 = [System.Text.Encoding]::ASCII.GetBytes("viewmodel")
$str3 = [System.Text.Encoding]::ASCII.GetBytes("DepthStencil")

function Find-String($bytes, $pattern) {
    $matches = 0
    for ($i = 0; $i -lt $bytes.Length - $pattern.Length; $i += 4) {
        $found = $true
        for ($j = 0; $j -lt $pattern.Length; $j++) {
            if ($bytes[$i+$j] -ne $pattern[$j]) { $found = $false; break }
        }
        if ($found) {
            $matches++
            Write-Host "Found at 0x$($i.ToString('X'))"
            if ($matches -ge 5) { break }
        }
    }
}

Write-Host "Searching HudModel..."
Find-String $bytes $str1
