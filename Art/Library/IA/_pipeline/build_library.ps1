# Regenera la biblioteca de borradores Art/Library/IA (FBX, .blend, láminas, manifest.json e INDEX.md).
#   powershell -File Art/Library/IA/_pipeline/build_library.ps1 [-Only slug1,slug2] [-Category puzzles]
# Sale con código distinto de 0 si algún asset no pasa la validación.
param(
    [string]$Only = '',
    [string]$Category = '',
    [string]$Blender = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
)
$ErrorActionPreference = 'Continue'  # Blender escribe avisos en stderr; el fallo se lee del código de salida
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$Extra = @('--')
if ($Only) { $Extra += @('--only', $Only) }
if ($Category) { $Extra += @('--category', $Category) }
& $Blender -b --factory-startup --python-exit-code 1 --python (Join-Path $Here 'build.py') @Extra 2>&1 |
    Select-String -Pattern '^\[ia\]|Error|Traceback|File "'
exit $LASTEXITCODE
