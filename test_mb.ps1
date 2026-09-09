$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)

$names = @("motion_blur", "motionblur", "mat_motion_blur", "m_flMotionBlur", "MotionBlur", "csm_motion_blur")
foreach ($n in $names) {
    $idx = $text.IndexOf($n)
    Write-Host ("{0}: {1}" -f $n, $idx)
}
