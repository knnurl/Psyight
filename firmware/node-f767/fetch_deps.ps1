# Fetches the pinned ST driver and middleware sources into third_party/ (gitignored).
# BSD-3-Clause (HAL, lwIP port, LAN8742) and Apache-2.0 (CMSIS); see each repository's LICENSE.md.
# Run from anywhere: powershell -ExecutionPolicy Bypass -File firmware/node-f767/fetch_deps.ps1

$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot "third_party"
New-Item -ItemType Directory -Force $root | Out-Null

# Repository, commit. Bump deliberately, one at a time, and rebuild and re-test on the board.
$pins = @(
    @("cmsis_core",           "01d18147527e30ed69ace1cb03b818de78bc7062"),
    @("cmsis_device_f7",      "2352e888e821aa0f4fe549bd5ea81d29c67a3222"), # v1.2.10
    @("stm32f7xx_hal_driver", "e860c4ff226d0e4ac2837b31960649898b29ee88"),
    @("stm32_mw_lwip",        "07680c8172fa5fc135adefa3f7711c14f4938dc9"), # lwIP 2.2.1
    @("stm32-lan8742",        "1ce7c19488f0f6d44a2c1dc7033a23e88cd0bbc8")
)

foreach ($pin in $pins) {
    $name = $pin[0]
    $commit = $pin[1]
    $dir = Join-Path $root $name
    if (-not (Test-Path (Join-Path $dir ".git"))) {
        git init --quiet $dir
        git -C $dir remote add origin "https://github.com/STMicroelectronics/$name.git"
    }
    $have = (git -C $dir rev-parse --verify --quiet HEAD) 2>$null
    if ($have -ne $commit) {
        Write-Host "Fetching $name at $commit"
        git -C $dir fetch --quiet --depth 1 origin $commit
        git -C $dir checkout --quiet --detach FETCH_HEAD
    }
    Write-Host ("{0,-22} {1}" -f $name, (git -C $dir rev-parse HEAD))
}
