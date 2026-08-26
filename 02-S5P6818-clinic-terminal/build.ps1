param(
    [ValidateSet('host', 'build-all', 'test', 'board', 'board-clean', 'all', 'clean')]
    [string]$Target = 'host'
)

$ErrorActionPreference = 'Stop'

$makeCommand = Get-Command make -CommandType Application -ErrorAction SilentlyContinue
if ($null -eq $makeCommand) {
    throw 'GNU Make is required for the canonical build entry. Run the target from the Ubuntu shared directory or install GNU Make in this PowerShell environment.'
}

$makeArguments = @('-C', $PSScriptRoot)
switch ($Target) {
    'test' {
        $makeArguments += @('-B', 'test')
    }
    default {
        $makeArguments += $Target
    }
}

& $makeCommand.Source @makeArguments
if ($LASTEXITCODE -ne 0) {
    throw "make failed for target '$Target' with exit code $LASTEXITCODE"
}

Write-Host "Build target '$Target' completed."
