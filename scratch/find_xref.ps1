$clientPath = "C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"
$bytes = [System.IO.File]::ReadAllBytes($clientPath)

# Offset of "weapons/models/shared/stattrak/stattrak_module.vmdl" is 0x1A96C0F (RVA)
# Let's search for RIP-relative LEA to 0x1A96C0F in the .text section
$strRva = 0x1A96C0F

Write-Host ("Searching for xrefs to 0x{0:X}..." -f $strRva)
for ($i = 0; $i -lt $bytes.Length - 7; $i++) {
    # 48 8D ?? ?? ?? ?? ?? (lea reg, [rip + disp32])
    # 48 8D 15 (lea rdx, [rip + disp])
    # 48 8D 0D (lea rcx, [rip + disp])
    if ($bytes[$i] -eq 0x48 -and $bytes[$i+1] -eq 0x8D) {
        $disp = [System.BitConverter]::ToInt32($bytes, $i + 3)
        $target = $i + 7 + $disp
        if ($target -eq $strRva) {
            Write-Host ("Found xref at 0x{0:X} (bytes: {1:X2} {2:X2} {3:X2})" -f $i, $bytes[$i], $bytes[$i+1], $bytes[$i+2])
            # Print around $i
            $funcStart = [Math]::Max(0, $i - 64)
            $hex = -join ($bytes[$funcStart..($i+64)] | ForEach-Object { "{0:X2} " -f $_ })
            Write-Host ("Bytes around: " + $hex)
        }
    }
}
