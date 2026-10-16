<#
.SYNOPSIS
    Tags the current commit and publishes a GitHub release with the setup .exe and its SHA-256
    attached.

.DESCRIPTION
    The release is what the in-app update check reads: it asks
    api.github.com/repos/Lng999/airplay/releases/latest, takes tag_name as the version and the
    one .exe asset as the download. So the three things this script keeps in step are the
    version in app/CMakeLists.txt, the tag v<version>, and the attached installer.

    The installer's SHA-256 goes up beside it as AirPlay-Setup-<version>.exe.sha256
    ("<lower-case hex>  <file name>", the sha256sum format) and as a "SHA-256: <hex>" line at
    the end of the release body. The app (src/ui/updater.cpp) downloads the .sha256 asset,
    refuses an installer that does not match it, and skips a release that has none.

    Refuses to publish when the tree is dirty, when HEAD is not pushed, or when the tag already
    exists - a release whose tag points at code nobody can fetch is worse than no release.

    The repository must be public, or the update check gets HTTP 404 on every machine but this
    one: release assets on a private repo need an authenticated token to download.

.PARAMETER NotesFile
    Markdown file for the release body. Default installer\release-notes\v<version>.md; if that
    does not exist, the body is generated from the commits since the previous tag.

.PARAMETER Draft
    Create the release as a draft. The update check ignores drafts (/releases/latest skips
    them), so this is the way to stage one and publish it by hand later.

.PARAMETER SkipBuild
    Use the setup .exe already in dist\ instead of rebuilding it.

.EXAMPLE
    pwsh -File scripts\publish-release.ps1
    pwsh -File scripts\publish-release.ps1 -Draft
#>
[CmdletBinding()]
param(
    [string]$NotesFile,
    [switch]$Draft,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo

# --- version and preconditions ----------------------------------------------------------------

$m = [regex]::Match((Get-Content -LiteralPath (Join-Path $repo 'app\CMakeLists.txt') -Raw),
                    'project\s*\(\s*airplay_gui\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $m.Success) { throw 'could not read VERSION out of app/CMakeLists.txt' }
$version = $m.Groups[1].Value
$tag     = "v$version"
Write-Host "[version] $version -> tag $tag" -ForegroundColor Cyan

# Submodule dirt is expected (patches/ are applied in the working tree); our own files are not.
$dirty = git status --porcelain --ignore-submodules=all
if ($dirty) {
    Write-Host $dirty
    throw 'working tree is dirty - commit first'
}
if ((git rev-parse HEAD) -ne (git rev-parse '@{u}' 2>$null)) {
    throw 'HEAD is not what origin has - push first'
}
if (git tag --list $tag) { throw "tag $tag already exists - bump the version in app/CMakeLists.txt" }

$visibility = (gh repo view --json visibility | ConvertFrom-Json).visibility
if ($visibility -ne 'PUBLIC') {
    Write-Warning "repository is $visibility - release assets will not be downloadable without a token, and the in-app update check will fail with HTTP 404"
}

# --- installer ----------------------------------------------------------------------------------

$setup = Join-Path $repo "dist\AirPlay-Setup-$version.exe"
if ($SkipBuild) {
    if (-not (Test-Path -LiteralPath $setup)) { throw "-SkipBuild but $setup does not exist" }
} else {
    & (Join-Path $PSScriptRoot 'make-installer.ps1')
    if ($LASTEXITCODE) { throw "make-installer.ps1 failed ($LASTEXITCODE)" }
}
$setupName = Split-Path -Leaf $setup
Write-Host ("[asset] {0} ({1:N1} MB)" -f $setupName,
            ((Get-Item -LiteralPath $setup).Length / 1MB)) -ForegroundColor Cyan

# --- checksum --------------------------------------------------------------------------------------
# Written without a BOM and with a bare LF, so it reads the same to sha256sum -c as to the app.

$sha256  = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
$sumFile = "$setup.sha256"
[System.IO.File]::WriteAllText($sumFile, "$sha256  $setupName`n", [System.Text.UTF8Encoding]::new($false))
Write-Host "[asset] $(Split-Path -Leaf $sumFile) ($sha256)" -ForegroundColor Cyan

# --- notes ---------------------------------------------------------------------------------------

if (-not $NotesFile) {
    $candidate = Join-Path $repo "installer\release-notes\$tag.md"
    if (Test-Path -LiteralPath $candidate) { $NotesFile = $candidate }
}
if ($NotesFile) {
    Write-Host "[notes] $NotesFile" -ForegroundColor Cyan
    $body = (Get-Content -LiteralPath $NotesFile -Raw -Encoding utf8).TrimEnd()
} else {
    $prev = git describe --tags --abbrev=0 2>$null
    $range = if ($prev) { "$prev..HEAD" } else { 'HEAD' }
    $body = (git log --pretty='- %s' $range) -join "`n"
    Write-Host "[notes] generated from $range" -ForegroundColor Yellow
}
# The notes file in the repo is left as it is; the body that goes up is a temp copy with the hash.
$body += "`n`nSHA-256: $sha256`n"
$tmp = Join-Path $env:TEMP "airplay-notes-$tag.md"
[System.IO.File]::WriteAllText($tmp, $body, [System.Text.UTF8Encoding]::new($false))
$notesArgs = @('--notes-file', $tmp)

# --- tag and publish -------------------------------------------------------------------------------

git tag -a $tag -m "$tag"
git push origin $tag

$args = @('release', 'create', $tag, $setup, $sumFile,
          '--title', "AirPlay Alicisi $version",
          '--verify-tag') + $notesArgs
if ($Draft) { $args += '--draft' } else { $args += '--latest' }

gh @args
if ($LASTEXITCODE) { throw "gh release create failed ($LASTEXITCODE)" }

Write-Host "[done] $(gh release view $tag --json url --jq .url)" -ForegroundColor Green
