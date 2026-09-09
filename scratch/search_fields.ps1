$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

# Search schema fields of C_CSWeaponBase
$idx = $text.IndexOf("C_CSWeaponBase`0")
if ($idx -lt 0) { $idx = $text.IndexOf("C_CSWeaponBase") }
Write-Host ("C_CSWeaponBase at 0x{0:X}" -f $idx)

# Find all field names containing "module" or "Module" or "Stattrak" or "StatTrak"
$regex = [System.Text.RegularExpressions.Regex]::new('m_[a-zA-Z0-9_]*(module|Module|stattrak|StatTrak|Stattrak)[a-zA-Z0-9_]*')
$matches = $regex.Matches($text)
$found = @{}
foreach ($m in $matches) {
    $found[$m.Value] = $true
}
$found.Keys | Sort-Object | ForEach-Object { Write-Host $_ }
