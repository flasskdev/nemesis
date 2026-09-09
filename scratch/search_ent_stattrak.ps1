$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

function SearchNear($term) {
    Write-Host ("=== Searching for " + $term + " ===")
    $idx = 0
    while (($idx = $text.IndexOf($term, $idx)) -ge 0) {
        $start = [Math]::Max(0, $idx - 100)
        $len = [Math]::Min(300, $text.Length - $start)
        $sub = $text.Substring($start, $len)
        $clean = -join ($sub.ToCharArray() | ForEach-Object { if ([char]::IsLetterOrDigit($_) -or $_ -eq '_' -or $_ -eq ' ' -or $_ -eq ':') { $_ } else { '.' } })
        Write-Host ("0x{0:X}: {1}" -f $idx, $clean)
        $idx += $term.Length
    }
}

SearchNear("ent_stattrak")
SearchNear("CEntityStattrakDisplayValueRenderAttribCallback")
