$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

# Schema table dump around 0x1A87F38
$start = 0x1A87E00
$sub = $text.Substring($start, 2000)
$clean = -join ($sub.ToCharArray() | ForEach-Object { if ([char]::IsLetterOrDigit($_) -or $_ -eq '_' -or $_ -eq ' ' -or $_ -eq ':') { $_ } else { ' ' } })
Write-Host $clean
