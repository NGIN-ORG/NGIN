$ErrorActionPreference = "Stop"

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "../.."))
$excludedPathParts = @(
    "/.git/",
    "/build/",
    "/node_modules/",
    "/.vscode-test/",
    "/Packages/NGIN.UI/assets/fonts/"
)

$markdownFiles = Get-ChildItem -LiteralPath $repoRoot -Recurse -File -Filter "*.md" |
    Where-Object {
        $path = $_.FullName.Replace('\', '/')
        -not ($excludedPathParts | Where-Object { $path.Contains($_) })
    }

$failures = [System.Collections.Generic.List[string]]::new()

foreach ($file in $markdownFiles) {
    $relativePath = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName)
    $isHistorical = $relativePath -match "(?i)(migration|release|changelog)"
    $inFence = $false
    $lineNumber = 0

    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        ++$lineNumber
        if ($line -match '^\s*```') {
            $inFence = -not $inFence
            continue
        }
        if ($inFence) {
            continue
        }

        foreach ($match in [regex]::Matches($line, '(?<!\!)\[[^\]]+\]\(([^)]+)\)')) {
            $target = $match.Groups[1].Value.Trim()
            $wasAngleWrapped = $target.StartsWith('<') -and $target.EndsWith('>')
            if ($wasAngleWrapped) {
                $target = $target.Substring(1, $target.Length - 2)
            }
            if ((-not $wasAngleWrapped -and $target.Contains(' ')) -or
                $target.StartsWith('#') -or
                $target -match '^[A-Za-z][A-Za-z0-9+.-]*:') {
                continue
            }

            $pathPart = $target.Split('#')[0]
            if ([string]::IsNullOrWhiteSpace($pathPart)) {
                continue
            }

            $decoded = [uri]::UnescapeDataString($pathPart)
            $resolved = [System.IO.Path]::GetFullPath((Join-Path $file.DirectoryName $decoded))
            if (-not (Test-Path -LiteralPath $resolved)) {
                $failures.Add("${relativePath}:${lineNumber}: broken link '${target}'")
            }
        }

        if (-not $isHistorical) {
            $retiredPatterns = @(
                '\bNGIN V[234]\b',
                '\bpost-V4\b',
                '\bconfiguration-first\b',
                '`--configuration`',
                '`workspace sync`',
                'docs/(specs|plans|reviews|api-drafts|proposals)/'
            )
            foreach ($pattern in $retiredPatterns) {
                if ($line -match $pattern) {
                    $failures.Add("${relativePath}:${lineNumber}: retired documentation term '$($Matches[0])'")
                }
            }
        }
    }
}

if ($failures.Count -gt 0) {
    $failures | Sort-Object -Unique | ForEach-Object { Write-Host $_ -ForegroundColor Red }
    exit 1
}

Write-Output "Documentation check passed for $($markdownFiles.Count) Markdown files."
