$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

$regex = [System.Text.RegularExpressions.Regex]::new('[a-zA-Z0-9_]*[Mm]odule[a-zA-Z0-9_]*')
$matches = $regex.Matches($text)
$found = @{}
foreach ($m in $matches) {
    if ($m.Value.Length -gt 6) {
        $found[$m.Value] = $true
    }
}
$found.Keys | Sort-Object | ForEach-Object { Write-Host $_ }
