# Regenera el buggy biplaza (Rally Tortuga): modelo + FBX + validación + lámina de revisión.
#   powershell -File Art/Source/Vehicles/Buggy/build_buggy.ps1
# Sale con código distinto de 0 si falla cualquier paso (incluida una comprobación de validate_buggy.py).
param([string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
$ErrorActionPreference = 'Continue'  # Blender escribe avisos en stderr; el fallo se lee del código de salida
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
foreach ($Step in 'build_buggy.py', 'validate_buggy.py', 'render_sheet.py') {
    & $Blender -b --factory-startup --python-exit-code 1 --python (Join-Path $Here $Step) 2>&1 |
        Select-String -Pattern '\[build_buggy\]|\[validate\]|\[render_sheet\]|Error|Traceback'
    if ($LASTEXITCODE -ne 0) { Write-Error "$Step falló (código $LASTEXITCODE)"; exit $LASTEXITCODE }
}
