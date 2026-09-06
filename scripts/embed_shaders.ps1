# embed_shaders.ps1 - generate embedded_shaders.cpp from compiled .spv files.
# Replicates cmake/embed_shaders.cmake behaviour (byte arrays + lookup table).
param(
    [Parameter(Mandatory = $true)][string]$InputDir,
    [Parameter(Mandatory = $true)][string]$OutCpp
)

$shaders = @(
    @{ Name = 'pbr_vert';     File = 'pbr_vert.spv' },
    @{ Name = 'pbr_frag';     File = 'pbr_frag.spv' },
    @{ Name = 'pbr_instanced_vert'; File = 'pbr_instanced_vert.spv' },
    @{ Name = 'picking_vert'; File = 'picking_vert.spv' },
    @{ Name = 'picking_frag'; File = 'picking_frag.spv' },
    @{ Name = 'outline_vert'; File = 'outline_vert.spv' },
    @{ Name = 'outline_frag'; File = 'outline_frag.spv' },
    @{ Name = 'shadow_vert';  File = 'shadow_vert.spv' },
    @{ Name = 'ui_vert';      File = 'ui_vert.spv' },
    @{ Name = 'ui_frag';      File = 'ui_frag.spv' }
)

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('// Auto-generated. Do not edit.')
[void]$sb.AppendLine('#include "renderer/embedded_shaders.h"')
[void]$sb.AppendLine('#include <cstddef>')
[void]$sb.AppendLine('#include <cstring>')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('namespace Atlas::EmbeddedShaders {')
[void]$sb.AppendLine('')

$entries = @()
foreach ($s in $shaders) {
    $path = Join-Path $InputDir $s.File
    if (-not (Test-Path $path)) {
        Write-Error "embed_shaders.ps1: input not found: $path"
        exit 1
    }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -eq 0) {
        Write-Error "embed_shaders.ps1: empty input: $path"
        exit 1
    }
    $hex = ($bytes | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
    $sym = $s.Name -replace '[^A-Za-z0-9_]', '_'
    [void]$sb.AppendLine("alignas(4) static const unsigned char ${sym}[] = { $hex };")
    [void]$sb.AppendLine('')
    $entries += , @($s.Name, $sym, $bytes.Length)
}

[void]$sb.AppendLine('struct Entry { const char* name; const unsigned char* data; size_t size; };')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('static const Entry kEntries[] = {')
foreach ($e in $entries) {
    [void]$sb.AppendLine("    { `"$($e[0]).spv`", $($e[1]), sizeof($($e[1])) },")
    [void]$sb.AppendLine("    { `"shaders/$($e[0]).spv`", $($e[1]), sizeof($($e[1])) },")
}
[void]$sb.AppendLine('};')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('const ShaderData* find(const char* name) {')
[void]$sb.AppendLine('    static ShaderData out;')
[void]$sb.AppendLine('    if (!name) return nullptr;')
[void]$sb.AppendLine('    for (const auto& e : kEntries) {')
[void]$sb.AppendLine('        if (std::strcmp(e.name, name) == 0) {')
[void]$sb.AppendLine('            out.data = e.data;')
[void]$sb.AppendLine('            out.size = e.size;')
[void]$sb.AppendLine('            return &out;')
[void]$sb.AppendLine('        }')
[void]$sb.AppendLine('    }')
[void]$sb.AppendLine('    return nullptr;')
[void]$sb.AppendLine('}')
[void]$sb.AppendLine('')
[void]$sb.AppendLine('}')

$outDir = Split-Path -Parent $OutCpp
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
}
[System.IO.File]::WriteAllText($OutCpp, $sb.ToString())
Write-Host "embed_shaders.ps1: wrote $OutCpp"
