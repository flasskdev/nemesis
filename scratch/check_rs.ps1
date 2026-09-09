$dllPath = 'C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\rendersystemdx11.dll'
$bytes = [System.IO.File]::ReadAllBytes($dllPath)

# Look for vtable setup of rendersystemdx11 or direct wrappers
# Let's see what export or functions rendersystemdx11 has
Write-Host "rendersystemdx11 size: $($bytes.Length)"
