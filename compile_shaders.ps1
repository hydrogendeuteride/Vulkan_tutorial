param(
  [string]$ShaderDir = "shaders",
  [string]$OutDir    = "shaders",
  [string]$Glslc     = "glslc",
  [string[]]$ExtraArgs = @(),
  [switch]$Clean
)
$ErrorActionPreference = "Stop"

function Get-RelativePath([string]$BasePath, [string]$TargetPath) {
  $baseUri   = [System.Uri](Resolve-Path $BasePath).Path
  $targetUri = [System.Uri](Resolve-Path $TargetPath).Path
  $relUri = $baseUri.MakeRelativeUri($targetUri)
  return [System.Uri]::UnescapeDataString($relUri.ToString()).Replace('/', [System.IO.Path]::DirectorySeparatorChar)
}

$glslcCmd = Get-Command $Glslc -ErrorAction SilentlyContinue
if (-not $glslcCmd) {
  Write-Error "glslc not found. Please ensure glslc is in your PATH."
  exit 127
}

if ($Clean -and (Test-Path $OutDir)) {
  Write-Host "Cleaning: $OutDir"
  Remove-Item -Recurse -Force $OutDir
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$stagePattern = '^\.(vert|frag|comp|geom|tesc|tese|mesh|task|rgen|rint|rahit|rchit|rmiss|rcall)$'

$shaderRoot = (Resolve-Path $ShaderDir).Path
$files = Get-ChildItem -Path $shaderRoot -Recurse -File |
        Where-Object { $_.Extension -match $stagePattern }

if (-not $files) {
  Write-Host "No shaders to compile in $ShaderDir"
  exit 0
}

$compiled = 0
$failed   = 0

foreach ($src in $files) {
  $rel = Get-RelativePath $shaderRoot $src.FullName
  $destDirRel = Split-Path $rel -Parent
  $destDir = if ([string]::IsNullOrWhiteSpace($destDirRel)) { $OutDir } else { Join-Path $OutDir $destDirRel }
  New-Item -ItemType Directory -Force -Path $destDir | Out-Null

  $outFile = Join-Path $destDir ($src.BaseName + ".spv")
  Write-Host ("Compiling: {0} -> {1}" -f $src.FullName, $outFile)

  & $Glslc @ExtraArgs $src.FullName -o $outFile
  if ($LASTEXITCODE -ne 0) {
    Write-Warning ("Failed: {0}" -f $src.FullName)
    $failed++
  } else {
    $compiled++
  }
}

Write-Host ("Complete: {0} succeeded, {1} failed" -f $compiled, $failed)
if ($failed -gt 0) { exit 1 } else { exit 0 }