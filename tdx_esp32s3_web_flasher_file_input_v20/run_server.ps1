$appDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$appName = Split-Path -Leaf $appDir
Set-Location (Split-Path -Parent $appDir)
Write-Host "Open this URL in Chrome or Edge:"
Write-Host "http://localhost:8000/$appName/index.html"
python -m http.server 8000
