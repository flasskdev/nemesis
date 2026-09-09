$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

# Search for schema fields in C_StattrakModule and C_CS2WeaponModuleBase
function DumpSchema($className) {
    Write-Host ("=== Schema for " + $className + " ===")
    $idx = 0
    while (($idx = $text.IndexOf($className, $idx)) -ge 0) {
        $start = [Math]::Max(0, $idx - 50)
        $len = [Math]::Min(300, $text.Length - $start)
        $sub = $text.Substring($start, $len)
        $clean = -join ($sub.ToCharArray() | ForEach-Object { if ([char]::IsLetterOrDigit($_) -or $_ -eq '_' -or $_ -eq ' ' -or $_ -eq ':' -or $_ -eq '<' -or $_ -eq '>') { $_ } else { ' ' } })
        Write-Host $clean
        $idx += $className.Length
    }
}

DumpSchema("C_CS2WeaponModuleBase")
DumpSchema("C_StattrakModule")
