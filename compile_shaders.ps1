Param(
  [string]$SrcDir = "shaders",
  [string]$OutDir = "shaders_spv",
  [string]$TargetEnv = "vulkan1.2",
  [string]$Opt = "-O",     #
  [string]$Debug = "",     #
  [string[]]$Includes = @("shaders/include"),
  [string[]]$Defines  = @("MAX_LIGHTS=8"),
  [string[]]$ExtraFlags = @()
)

$glslc = Get-Command glslc -ErrorAction SilentlyContinue
if (-not $glslc) { Write-Error "glslc is not in PATH."; exit 1 }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$flags = @()
if ($Opt)   { $flags += $Opt }
if ($Debug) { $flags += $Debug }
$flags += "--target-env=$TargetEnv"
$flags += ($Includes | ForEach-Object { "-I$_" })
$flags += ($Defines  | ForEach-Object { "-D$_" })
$flags += $ExtraFlags

$files = Get-ChildItem -Path $SrcDir -Recurse -Include *.vert,*.frag,*.comp -File
$rebuilt = 0

foreach ($f in $files) {
  $relPath = $f.FullName.Substring($f.FullName.IndexOf((Resolve-Path $SrcDir)))

  $rel = Resolve-Path $f.FullName -Relative  #
  $rel = $rel -replace "^\.\\" , ""          #
  $rel = $rel.Substring($SrcDir.Length + 1)  #

  $out = Join-Path $OutDir ($rel + ".spv")
  New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null

  $needBuild = -not (Test-Path $out)
  if (-not $needBuild) {
    $needBuild = ($f.LastWriteTime -gt (Get-Item $out).LastWriteTime)
  }

  if ($needBuild) {
    Write-Host "[build] $($f.FullName) -> $out"
    & glslc @flags $f.FullName -o $out
    if ($LASTEXITCODE -ne 0) { throw "glslc failure: $($f.FullName)" }
    $rebuilt++
  } else {
    Write-Host "[skip ] $($f.FullName) (up to date)"
  }
}

Write-Host ("completed: {0} / {1}. output: {2}" -f $files.Count, $rebuilt, $OutDir)
