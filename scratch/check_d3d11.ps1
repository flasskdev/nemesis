$dllPath = 'C:\Windows\System32\d3d11.dll'
# Check if d3d11.dll can be read
if (Test-Path $dllPath) {
    Write-Host "d3d11.dll exists"
}
