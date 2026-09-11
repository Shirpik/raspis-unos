param(
    [int]$Port = 4173,
    [int]$ApiPort = 8080,
    [string]$BackendPath = ''
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$frontendIndex = Join-Path $projectRoot 'frontend\dist\index.html'
if (!(Test-Path -LiteralPath $frontendIndex -PathType Leaf)) {
    throw 'Frontend is not built. Run npm run build in the frontend directory.'
}
if ($BackendPath) {
    $resolvedBackend = [IO.Path]::GetFullPath($BackendPath)
    if (!(Test-Path -LiteralPath $resolvedBackend -PathType Leaf)) { throw "Backend was not found: $resolvedBackend" }
    $env:SITE_BACKEND_PATH = $resolvedBackend
}
$env:SITE_PORT = [string]$Port
$env:SITE_API_PORT = [string]$ApiPort
Set-Location -LiteralPath $projectRoot
node (Join-Path $projectRoot 'site\server.mjs')
