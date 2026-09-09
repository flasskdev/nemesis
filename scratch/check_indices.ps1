$lines = Get-Content 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um\d3d11.h'
$inVtbl = $false
$idx = 0
foreach ($l in $lines) {
    if ($l -match 'struct ID3D11DeviceContextVtbl') { $inVtbl = $true; continue }
    if ($inVtbl -and $l -match '^\s*\}\s*ID3D11DeviceContextVtbl;') { break }
    if ($inVtbl -and $l -match 'STDMETHODCALLTYPE\s*\*\s*(\w+)\s*\)') {
        $name = $matches[1]
        if ($name -match 'OM') { Write-Host "$idx : $name" }
        $idx++
    }
}
