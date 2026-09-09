$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

$idx = $text.IndexOf("CEntitySpawner<class C_StattrakModule>::Spawn")
Write-Host ("String at 0x{0:X}" -f $idx)

# Search for xrefs to this string
for ($i = 0; $i -lt $bytes.Length - 7; $i++) {
    if ($bytes[$i] -eq 0x48 -and $bytes[$i+1] -eq 0x8D) {
        $disp = [System.BitConverter]::ToInt32($bytes, $i + 3)
        if (($i + 7 + $disp) -eq $idx) {
            Write-Host ("Found xref to spawner string at 0x{0:X}" -f $i)
        }
    }
}
