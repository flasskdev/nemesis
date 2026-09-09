$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

$idx = $text.IndexOf("m_schema_modules")
while ($idx -ge 0) {
    $start = [Math]::Max(0, $idx - 50)
    $len = [Math]::Min(200, $text.Length - $start)
    $sub = $text.Substring($start, $len)
    $clean = -join ($sub.ToCharArray() | ForEach-Object { if ([char]::IsLetterOrDigit($_) -or $_ -eq '_' -or $_ -eq ' ' -or $_ -eq ':') { $_ } else { '.' } })
    Write-Host ("0x{0:X}: {1}" -f $idx, $clean)
    $idx = $text.IndexOf("m_schema_modules", $idx + 1)
}
