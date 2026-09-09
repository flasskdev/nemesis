$vs = Get-Content 'core\features\misc\motion_blur_shaders.hpp' -TotalCount 169
$ps = Get-Content 'scratch\motion_blur_test_ps.h'
$out = [System.Collections.Generic.List[string]]::new()
foreach ($l in $vs) { $out.Add($l) }
$out.Add("")
foreach ($l in $ps) { $out.Add($l) }
$out.Add("} // namespace features::misc::shaders")
[System.IO.File]::WriteAllLines('core\features\misc\motion_blur_shaders.hpp', $out)
Write-Host "Updated motion_blur_shaders.hpp successfully!"
