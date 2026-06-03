$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$boot = [System.IO.File]::ReadAllBytes("$dir\boot.bin")
$kernel = [System.IO.File]::ReadAllBytes("$dir\kernel.bin")
$sz = $kernel.Length
$boot[442] = $sz -band 0xFF
$boot[443] = ($sz -shr 8) -band 0xFF
$boot[444] = ($sz -shr 16) -band 0xFF
$boot[445] = ($sz -shr 24) -band 0xFF
[System.IO.File]::WriteAllBytes("$dir\boot.bin", $boot)
$img = $boot + $kernel
[System.IO.File]::WriteAllBytes("$dir\cortexos.img", $img)
Write-Output "Patched: kernel=$sz, img=$($img.Length)"
