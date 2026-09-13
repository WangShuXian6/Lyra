#Requires -Version 7.4
<#
.SYNOPSIS
Read-only release candidate audit. Only the independent JSON report is written.
.EXAMPLE
pwsh -NoProfile -File scripts/audit-release-files.ps1
.NOTES
Exit 1 means findings or an incomplete audit; exit 0 may still include review warnings.
Never prints credential values, matched text, captured helper errors or credential hashes.
Run again after changing assets, inventories, archives, staged files or release content.
#>
param(
    [string]$Repository=(Join-Path $PSScriptRoot '..'),
    [string]$ReportPath,
    [string]$SecretPath,
    [string]$AssetInventoryPath,
    [ValidateRange(1,99)][int]$LargeFileWarningMiB=10,
    [ValidateRange(100,1000)][int]$LargeFileErrorMiB=100
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath($Repository).TrimEnd('/','\')
if(-not $ReportPath){$ReportPath=Join-Path $repo 'verification/release-files-audit.json'}
if(-not $SecretPath){$SecretPath=Join-Path $repo 'examples/MMORPG/.local/backend.secrets.clixml'}
if(-not $AssetInventoryPath){$AssetInventoryPath=Join-Path $repo 'verification/mmorpg-assets.json'}
$ReportPath=[IO.Path]::GetFullPath($ReportPath)
if($ReportPath -eq (Join-Path $repo 'verification/release-audit.json')){throw 'This independent audit must not replace release-audit.json.'}
if(-not $ReportPath.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'The report must remain inside this repository.'}
[IO.Directory]::CreateDirectory((Split-Path $ReportPath -Parent))|Out-Null

# Captured subprocess output is deliberately never printed, including helper errors.
function New-GitProcess([string[]]$Arguments) {
    $start=[Diagnostics.ProcessStartInfo]::new()
    $start.FileName='git';$start.WorkingDirectory=$repo;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $start.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
    $start.Environment['GIT_TERMINAL_PROMPT']='0';$start.Environment['GCM_INTERACTIVE']='never'
    $start.Environment['GCM_GUI_PROMPT']='false';$start.Environment['GCM_MODAL_PROMPT']='false'
    $start.Environment['GIT_OPTIONAL_LOCKS']='0';$start.Environment['GIT_NO_LAZY_FETCH']='1'
    foreach($argument in $Arguments){$start.ArgumentList.Add($argument)}
    [Diagnostics.Process]::Start($start)
}
function Get-GitText([string[]]$Arguments,[string]$InputText='',[int]$TimeoutMs=15000) {
    $process=New-GitProcess $Arguments
    try {
        $outTask=$process.StandardOutput.ReadToEndAsync();$errTask=$process.StandardError.ReadToEndAsync()
        if($InputText){$process.StandardInput.Write($InputText)}
        $process.StandardInput.Close()
        if(-not $process.WaitForExit($TimeoutMs)){$process.Kill();$process.WaitForExit();return @{success=$false;text='';reason='timeout'}}
        $output=$outTask.GetAwaiter().GetResult();$null=$errTask.GetAwaiter().GetResult()
        return @{success=($process.ExitCode-eq 0);text=$output;reason=if($process.ExitCode-eq 0){'available'}else{'unavailable'}}
    } finally {$process.Dispose()}
}
$secretValues=[Collections.Generic.List[object]]::new()
$findings=[Collections.Generic.List[object]]::new()
$findingKeys=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
$stats=@{filesScanned=0;bytesScanned=[long]0;archiveEntriesScanned=0;indexBlobsScanned=0;unreadable=0;assetsChecked=0}
$credentialCoverage=[ordered]@{dpapiDatabase='unavailable';dpapiServerSecret='unavailable';githubGcm='unavailable-noninteractive';valuesOrSecretHashesRecorded=$false}
$report=[ordered]@{
    schemaVersion=1;startedAt=[DateTime]::UtcNow.ToString('o');finishedAt=$null;status='running';passed=$false;releaseReady=$false
    scope='Read-only Git cached/non-ignored untracked working-tree candidates, indexed blobs, and in-memory tar.gz/zip members. This audit does not establish gameplay, builds, deployment or complete release acceptance.'
    enumeration='git ls-files --cached --others --exclude-standard -z';candidateCount=0;candidateSetChangedDuringAudit=$false
    assetInventory=[ordered]@{path='verification/mmorpg-assets.json';declaredCount=$null;recordCount=0;candidateAssetCount=0;status='not-read'}
    credentials=$credentialCoverage;statistics=$stats;largeFileWarningMiB=$LargeFileWarningMiB;largeFileErrorMiB=$LargeFileErrorMiB
    findings=@();limitations=@('Known local credentials are compared only in memory; unavailable noninteractive GCM credentials cannot be compared.','Pattern rules cover selected credential formats and do not prove absence of every secret.','Binary asset originality is established only against the inventory snapshot and matching bytes; compilation alone is not provenance.','Archives are read without extraction, with 256 MiB per-member and 1 GiB expanded limits; nested archives are flagged for separate review.','This is a snapshot while other work may continue; re-run once final files and inventories settle.')
}
$report|ConvertTo-Json -Depth 10|Set-Content -LiteralPath $ReportPath -Encoding utf8

function Safe-Path([string]$Path) {
    foreach($entry in $secretValues){if($Path.Contains($entry.value,[StringComparison]::Ordinal)){$Path=$Path.Replace($entry.value,'[redacted]',[StringComparison]::Ordinal)}}
    [regex]::Replace($Path,'(?:gh[pousr]_[A-Za-z0-9_]{20,255}|github_pat_[A-Za-z0-9_]{40,255})','[redacted]')
}
function Add-Finding([string]$Path,[string]$Rule,[string]$Severity='error',[string]$Version='working-tree') {
    $safe=Safe-Path $Path
    if($findingKeys.Add("$Version|$Rule|$safe")){$findings.Add([ordered]@{path=$safe;rule=$Rule;severity=$Severity;version=$Version})}
}
function Is-Placeholder([string]$Value) {
    if([string]::IsNullOrWhiteSpace($Value)){return $true}
    $valueText=$Value.Trim()
    return $valueText -match '^<[^>]+>$|^\$(?:env:|[A-Za-z_][A-Za-z0-9_:]*(?:[.\[]|$)|\()|^\{\{.*\}\}$|^%[^%]+%$|^(?:REDACTED|CHANGEME|REPLACE[_-].*|YOUR[_-].*|EXAMPLE[_-].*)$|^\*+$' -or
        $valueText -match '^(?:process\.env\.|os\.environ|Environment\.GetEnvironmentVariable|GetEnvironmentVariable\()'
}
function Check-PathPolicy([string]$Path,[string]$Version,[string]$DisplayPath=$Path) {
    $normal=$Path.Replace('\','/')
    if($normal -match '(^|/)(?:node_modules|\.next|out|\.source|\.local|\.deps|Binaries|Intermediate|Saved|DerivedDataCache|\.vs|\.idea|__pycache__|artifacts|test-results|playwright-report|\.playwright-cli)(/|$)' -or
        $normal -match '\.(?:pyc|tsbuildinfo|sln|slnx)$') {Add-Finding $DisplayPath 'forbidden-cache-or-build-path' 'error' $Version}
    $name=[IO.Path]::GetFileName($normal)
    if(($name -match '^\.env(?:\..*)?$' -and $name-ne '.env.example') -or
        $name -match '^(?:credentials?(?:\..*)?|id_(?:rsa|ed25519|ecdsa)(?:\.pub)?|backend\.secrets\..*)$' -or
        $name -match '\.(?:pfx|p12|key|keystore|clixml)$') {Add-Finding $DisplayPath 'forbidden-credential-file' 'error' $Version}
    if($normal -match '\.(?:dll|exe|lib|obj|pdb|pch|so|dylib|a|pak|utoc|ucas|target)$') {Add-Finding $DisplayPath 'compiled-library-or-game-artifact' 'error' $Version}
    if($normal -match '(^|/)ThirdParty/' -and $name -notmatch '\.Build\.cs$|^(?:README(?:\.md)?|NOTICE(?:\.md)?|LICENSE(?:\..*)?|\.gitkeep)$') {
        Add-Finding $DisplayPath 'unreviewed-third-party-vendored-file' 'error' $Version
    }
    if($normal -match '^/|^[A-Za-z]:|(^|/)\.\.(/|$)'){Add-Finding $DisplayPath 'path-outside-release-root' 'error' $Version;return $false}
    return $true
}
$regexOptions=[Text.RegularExpressions.RegexOptions]::IgnoreCase -bor [Text.RegularExpressions.RegexOptions]::Multiline
$patterns=@(
    @{rule='private-key-material';regex=[regex]::new('-----BEGIN (?:RSA |EC |DSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----',$regexOptions)},
    @{rule='github-token-literal';regex=[regex]::new('\b(?:gh[pousr]_[A-Za-z0-9_]{20,255}|github_pat_[A-Za-z0-9_]{40,255})\b')},
    @{rule='url-password-literal';regex=[regex]::new('\b(?:postgres(?:ql)?|mysql|mariadb|redis|https?|ftp)://[^/\s:@]+:(?<value>[^@\s/"''\\]+)@',$regexOptions)},
    @{rule='environment-secret-literal';regex=[regex]::new('\b(?:PGPASSWORD|MMO_SERVER_SECRET)\b["'']?\s*(?:=|:)\s*(?:"(?<value>[^"\r\n]*)"|''(?<value>[^''\r\n]*)''|(?<value>[A-Za-z0-9_./+=:-]+)(?=\s*(?:[;,}]|$)))',$regexOptions)},
    # Exact protocol key spelling avoids treating a Designer's "Password": "EditableTextBox"
    # field-type declaration as a credential. Actual local values are always compared above,
    # independently of key name. Unknown password examples still require human review.
    @{rule='credential-json-literal';regex=[regex]::new('["''](?:access_token|accessToken|refresh_token|refreshToken|serverSecret)["'']\s*:\s*["''](?<value>[^"''\r\n]+)["'']')},
    @{rule='password-json-literal-requires-review';severity='warning';regex=[regex]::new('["'']password["'']\s*:\s*["''](?<value>[^"''\r\n]+)["'']')},
    @{rule='authorization-bearer-literal';regex=[regex]::new('\bAuthorization\s*:\s*Bearer\s+(?<value>[A-Za-z0-9_.-]{24,})',$regexOptions)}
)
function Scan-Text([string]$Text,[string]$Path,[string]$Version) {
    foreach($entry in $secretValues){if($Text.Contains($entry.value,[StringComparison]::Ordinal)){Add-Finding $Path $entry.rule 'error' $Version}}
    foreach($pattern in $patterns){foreach($match in $pattern.regex.Matches($Text)) {
        if(-not $match.Groups['value'].Success -or -not(Is-Placeholder $match.Groups['value'].Value)){
            $severity=if($pattern.severity){$pattern.severity}else{'error'}
            Add-Finding $Path $pattern.rule $severity $Version;break
        }
    }}
}
$assetRecords=@{}
function Scan-Stream([IO.Stream]$Stream,[long]$Length,[string]$Path,[string]$Version,[string]$LogicalPath=$Path) {
    if($Length -ge $LargeFileErrorMiB*1MB){Add-Finding $Path 'file-at-or-over-github-size-limit' 'error' $Version}
    elseif($Length -ge $LargeFileWarningMiB*1MB){Add-Finding $Path 'large-release-file' 'warning' $Version}
    $isAsset=$LogicalPath -match '\.(?:uasset|umap)$'
    $hash=if($isAsset){[Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)}else{$null}
    try {
        $buffer=[byte[]]::new(73728);$carry=0;$remaining=$Length
        while($remaining-gt 0) {
            $read=$Stream.Read($buffer,$carry,[int][Math]::Min(65536,$remaining))
            if($read-le 0){throw 'Premature stream end'}
            if($hash){$hash.AppendData($buffer,$carry,$read)}
            $total=$carry+$read
            Scan-Text ([Text.Encoding]::UTF8.GetString($buffer,0,$total)) $Path $Version
            # Check both alignments for UE/Windows UTF-16 strings embedded in binary data.
            foreach($encoding in @([Text.Encoding]::Unicode,[Text.Encoding]::BigEndianUnicode)) {
                Scan-Text ($encoding.GetString($buffer,0,$total)) $Path $Version
                if($total-gt 1){Scan-Text ($encoding.GetString($buffer,1,$total-1)) $Path $Version}
            }
            $remaining-=$read;$stats.bytesScanned+=$read
            $carry=[Math]::Min(8192,$total);[Array]::Copy($buffer,$total-$carry,$buffer,0,$carry)
        }
        if($isAsset) {
            $stats.assetsChecked++
            $entry=$assetRecords[$LogicalPath.Replace('\','/')]
            if(-not $entry){Add-Finding $Path 'binary-asset-not-in-original-inventory' 'error' $Version}
            else {
                if($entry.origin-notmatch '^Original tutorial\b'){Add-Finding $Path 'binary-asset-origin-not-established' 'error' $Version}
                $actual=[Convert]::ToHexString($hash.GetHashAndReset()).ToLowerInvariant()
                if($entry.sha256-ne$actual -or [long]$entry.bytes-ne$Length){Add-Finding $Path 'binary-asset-inventory-bytes-mismatch' 'error' $Version}
            }
        }
    } finally {if($hash){$hash.Dispose()}}
}
function Check-ArchiveMember([string]$Name,[string]$Container,[string]$Version,[long]$Size) {
    $member=$Name.Replace('\','/');while($member.StartsWith('./')){$member=$member.Substring(2)}
    $display="$Container!/$member"
    $safe=Check-PathPolicy $member $Version $display
    if($Size-gt 256MB){Add-Finding $display 'archive-member-exceeds-audit-limit' 'error' $Version;return $null}
    if($member-match '\.(?:zip|tar|tgz|tar\.gz|7z|rar)$'){Add-Finding $display 'nested-archive-requires-review' 'error' $Version}
    if(-not $safe){return $null}
    # Release archives have exactly one top-level directory named after the archive.
    # Strip only that exact directory; unknown paths never gain an inventory exemption.
    $archiveRoot=[IO.Path]::GetFileName($Container) -replace '\.(?:tar\.gz|tgz|zip)$',''
    $logical=if($member.StartsWith($archiveRoot+'/',[StringComparison]::Ordinal)){$member.Substring($archiveRoot.Length+1)}else{$member}
    return @{logical=$logical;display=$display}
}
function Scan-Archive([IO.Stream]$Stream,[string]$Path,[string]$Version) {
    $expanded=[long]0
    if($Path-match '\.zip$') {
        $archive=[IO.Compression.ZipArchive]::new($Stream,[IO.Compression.ZipArchiveMode]::Read,$true)
        try {foreach($entry in $archive.Entries) {
            if($entry.FullName.EndsWith('/')){continue}
            $expanded+=$entry.Length;if($expanded-gt 1GB){Add-Finding $Path 'archive-expanded-size-exceeds-audit-limit' 'error' $Version;break}
            $member=Check-ArchiveMember $entry.FullName $Path $Version $entry.Length
            if((($entry.ExternalAttributes-shr 16)-band 0xF000)-eq 0xA000){Add-Finding "$Path!/$($entry.FullName)" 'archive-symlink' 'error' $Version;continue}
            if($member){$input=$entry.Open();try{Scan-Stream $input $entry.Length $member.display $Version $member.logical;$stats.archiveEntriesScanned++}finally{$input.Dispose()}}
        }}finally{$archive.Dispose()}
    } else {
        $gzip=[IO.Compression.GZipStream]::new($Stream,[IO.Compression.CompressionMode]::Decompress,$true)
        $tar=[System.Formats.Tar.TarReader]::new($gzip,$true)
        try {while($entry=$tar.GetNextEntry($false)) {
            if($entry.EntryType-eq [System.Formats.Tar.TarEntryType]::Directory){continue}
            $expanded+=$entry.Length;if($expanded-gt 1GB){Add-Finding $Path 'archive-expanded-size-exceeds-audit-limit' 'error' $Version;break}
            $member=Check-ArchiveMember $entry.Name $Path $Version $entry.Length
            if($entry.EntryType-notin @([System.Formats.Tar.TarEntryType]::RegularFile,[System.Formats.Tar.TarEntryType]::V7RegularFile)) {
                Add-Finding "$Path!/$($entry.Name)" 'archive-link-or-special-entry' 'error' $Version;continue
            }
            if($member -and $entry.DataStream){Scan-Stream $entry.DataStream $entry.Length $member.display $Version $member.logical;$stats.archiveEntriesScanned++}
        }}finally{$tar.Dispose();$gzip.Dispose()}
    }
}

try {
    # Import-Clixml decrypts user-scoped DPAPI SecureStrings. Never set environment variables.
    if(Test-Path -LiteralPath $SecretPath) {
        try {
            $localSecrets=Import-Clixml -LiteralPath $SecretPath
            foreach($pair in @(@('DatabasePassword','known-database-secret','dpapiDatabase'),@('ServerSecret','known-server-secret','dpapiServerSecret'))) {
                if($localSecrets.($pair[0]) -is [Security.SecureString]) {
                    $plain=[Net.NetworkCredential]::new('',$localSecrets.($pair[0])).Password
                    if($plain){$secretValues.Add(@{rule=$pair[1];value=$plain});$credentialCoverage[$pair[2]]='available-in-memory'}
                    $plain=$null
                }
            }
        } catch {$credentialCoverage.dpapiDatabase='unavailable';$credentialCoverage.dpapiServerSecret='unavailable'}
    }
    try {
        $gcm=Get-GitText @('-c','credential.interactive=never','-c','credential.guiPrompt=false','credential-manager','get') "protocol=https`nhost=github.com`n`n" 10000
        if($gcm.success) {
            foreach($line in $gcm.text.Split("`n")) {
                if($line.StartsWith('password=')){$token=$line.Substring(9).TrimEnd("`r");if($token){$secretValues.Add(@{rule='known-github-token';value=$token});$credentialCoverage.githubGcm='available-in-memory'};$token=$null}
            }
        }
        $gcm=$null;$line=$null
    } catch {$credentialCoverage.githubGcm='unavailable-noninteractive'}
    if($credentialCoverage.dpapiDatabase-ne 'available-in-memory' -or $credentialCoverage.dpapiServerSecret-ne 'available-in-memory') {
        Add-Finding 'local-DPAPI-credential-source' 'required-secret-comparison-unavailable'
    }

    try {
        $inventoryText=[IO.File]::ReadAllText([IO.Path]::GetFullPath($AssetInventoryPath))
        $inventory=$inventoryText|ConvertFrom-Json
        $report.assetInventory.declaredCount=$inventory.assetCount;$report.assetInventory.recordCount=@($inventory.assets).Count
        foreach($entry in $inventory.assets) {
            if($assetRecords.ContainsKey($entry.repoPath)){Add-Finding 'verification/mmorpg-assets.json' 'duplicate-asset-inventory-path'}
            $assetRecords[$entry.repoPath]=$entry
        }
        if($inventory.assetCount-ne @($inventory.assets).Count -or $inventory.officialBinaryAssetsIncluded-ne 0){Add-Finding 'verification/mmorpg-assets.json' 'asset-inventory-count-or-origin-summary-invalid'}
        $report.assetInventory.status='snapshot-read'
    } catch {Add-Finding 'verification/mmorpg-assets.json' 'asset-inventory-unreadable';$report.assetInventory.status='unreadable'}

    $enumeration=Get-GitText @('ls-files','--cached','--others','--exclude-standard','-z')
    if(-not $enumeration.success){throw 'Candidate enumeration failed'}
    $candidates=@($enumeration.text.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries)|Sort-Object -Unique)
    $report.candidateCount=$candidates.Count
    $report.assetInventory.candidateAssetCount=@($candidates|Where-Object{$_-match '\.(?:uasset|umap)$'}).Count
    foreach($assetPath in $assetRecords.Keys){if($assetPath-notin$candidates){Add-Finding $assetPath 'inventory-asset-not-a-git-candidate'}}
    $progress=0;$observedFiles=@{}
    foreach($relative in $candidates) {
        $progress++;if($progress%75-eq 0){Write-Host "Audited $progress / $($candidates.Count) candidate paths; sensitive findings are not printed."}
        if(-not(Check-PathPolicy $relative 'working-tree')){continue}
        $full=[IO.Path]::GetFullPath((Join-Path $repo $relative))
        if(-not $full.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){Add-Finding $relative 'path-outside-release-root';continue}
        try {
            $node=Get-Item -LiteralPath $full -Force
            $hasLink=$false;$ancestor=$node
            while($ancestor -and $ancestor.FullName-ne $repo) {
                if($ancestor.Attributes-band [IO.FileAttributes]::ReparsePoint){$hasLink=$true;break}
                $ancestor=if($ancestor-is [IO.FileInfo]){$ancestor.Directory}else{$ancestor.Parent}
            }
            if($hasLink){Add-Finding $relative 'symlink-or-reparse-point';continue}
            if($node.PSIsContainer){Add-Finding $relative 'unexpected-directory-or-gitlink';continue}
            $before=$node.LastWriteTimeUtc;$stream=[IO.File]::Open($full,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
            $observedFiles[$relative]=@{length=$node.Length;lastWrite=$before}
            try {
                Scan-Stream $stream $node.Length $relative 'working-tree';$stats.filesScanned++
                if($relative-match '\.(?:tar\.gz|tgz|zip)$'){$stream.Position=0;Scan-Archive $stream $relative 'working-tree-archive'}
                elseif($relative-match '\.(?:7z|rar|tar)$'){Add-Finding $relative 'unsupported-archive-needs-content-audit'}
            } finally {$stream.Dispose()}
            $after=Get-Item -LiteralPath $full
            if($after.LastWriteTimeUtc-ne$before -or $after.Length-ne$node.Length){Add-Finding $relative 'file-changed-during-audit'}
        } catch {$stats.unreadable++;Add-Finding $relative 'unreadable-file-or-archive'}
    }

    # Index modes are checked independently. Batch reads stay in memory and never use checkout/show on disk.
    $index=Get-GitText @('ls-files','--stage','-z')
    if(-not $index.success){Add-Finding 'git-index' 'index-enumeration-unavailable'}
    else {
        $entries=@($index.text.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries))
        if($entries.Count) {
            $batch=New-GitProcess @('cat-file','--batch');$batchError=$batch.StandardError.ReadToEndAsync()
            try {foreach($raw in $entries) {
                if($raw-notmatch '^(?<mode>[0-9]{6}) (?<oid>[a-f0-9]+) (?<stage>[0-3])\t(?<path>[\s\S]+)$'){Add-Finding 'git-index' 'unrecognized-index-entry';continue}
                $mode=$Matches.mode;$oid=$Matches.oid;$relative=$Matches.path
                if($Matches.stage-ne'0'){Add-Finding $relative 'unmerged-index-stage' 'error' 'index';continue}
                if($mode-eq'120000'){Add-Finding $relative 'git-index-symlink' 'error' 'index';continue}
                if($mode-eq'160000'){Add-Finding $relative 'git-index-submodule' 'error' 'index';continue}
                $null=Check-PathPolicy $relative 'index'
                $batch.StandardInput.WriteLine($oid);$batch.StandardInput.Flush()
                $header=[Text.StringBuilder]::new()
                while(($byte=$batch.StandardOutput.BaseStream.ReadByte())-ne10){if($byte-lt0 -or $header.Length-gt200){throw 'Invalid batch header'};$null=$header.Append([char]$byte)}
                if($header.ToString()-notmatch '^[a-f0-9]+ blob (?<size>[0-9]+)$'){throw 'Indexed object is not a blob'}
                $length=[long]$Matches.size
                # Memory buffers also allow read-only archive inspection of indexed bytes.
                if($length-gt256MB){Add-Finding $relative 'index-blob-exceeds-audit-limit' 'error' 'index'}
                $memory=if($length-le256MB){[IO.MemoryStream]::new()}else{$null}
                try {
                    if($memory) {
                        $chunk=[byte[]]::new(65536);$remaining=$length
                        while($remaining-gt0){$read=$batch.StandardOutput.BaseStream.Read($chunk,0,[int][Math]::Min($remaining,$chunk.Length));if($read-le0){throw 'Incomplete index blob'};$memory.Write($chunk,0,$read);$remaining-=$read}
                        $memory.Position=0;Scan-Stream $memory $length $relative 'index';$stats.indexBlobsScanned++
                        if($relative-match '\.(?:tar\.gz|tgz|zip)$'){$memory.Position=0;Scan-Archive $memory $relative 'index-archive'}
                    } else {Scan-Stream $batch.StandardOutput.BaseStream $length $relative 'index';$stats.indexBlobsScanned++}
                } finally {if($memory){$memory.Dispose()}}
                if($batch.StandardOutput.BaseStream.ReadByte()-ne10){throw 'Invalid batch separator'}
            }} finally {
                $batch.StandardInput.Close();if(-not $batch.WaitForExit(10000)){$batch.Kill();$batch.WaitForExit()}
                $null=$batchError.GetAwaiter().GetResult();$batch.Dispose()
            }
        }
    }
    $finalEnumeration=Get-GitText @('ls-files','--cached','--others','--exclude-standard','-z')
    if($finalEnumeration.success) {
        $finalCandidates=@($finalEnumeration.text.Split([char]0,[StringSplitOptions]::RemoveEmptyEntries)|Sort-Object -Unique)
        $changed=@(Compare-Object $candidates $finalCandidates)
        if($changed.Count){$report.candidateSetChangedDuringAudit=$true;foreach($change in $changed){Add-Finding $change.InputObject 'candidate-set-changed-during-audit' 'warning'}}
    } else {Add-Finding 'git-candidates' 'final-candidate-enumeration-unavailable'}
    foreach($relative in $observedFiles.Keys) {
        try {
            $current=Get-Item -LiteralPath (Join-Path $repo $relative) -Force
            $observed=$observedFiles[$relative]
            if($current.Length-ne$observed.length -or $current.LastWriteTimeUtc-ne$observed.lastWrite){Add-Finding $relative 'file-changed-during-audit'}
        } catch {Add-Finding $relative 'file-disappeared-during-audit'}
    }
    $finalIndex=Get-GitText @('ls-files','--stage','-z')
    if(-not $finalIndex.success){Add-Finding 'git-index' 'final-index-enumeration-unavailable'}
    elseif($index.success -and $finalIndex.text-cne$index.text){Add-Finding 'git-index' 'index-changed-during-audit'}
    if($inventoryText -and [IO.File]::ReadAllText([IO.Path]::GetFullPath($AssetInventoryPath))-cne$inventoryText){Add-Finding 'verification/mmorpg-assets.json' 'asset-inventory-changed-during-audit'}
    $report.passed=@($findings|Where-Object{$_.severity-eq'error'}).Count-eq0
    $report.status=if($report.passed){if($findings.Count){'passed-with-warnings'}else{'passed-scoped-file-audit'}}else{'findings-require-review'}
} catch {Add-Finding 'audit-execution' 'audit-did-not-complete';$report.status='incomplete';$report.passed=$false}
finally {
    $report.finishedAt=[DateTime]::UtcNow.ToString('o')
    $report.findings=@($findings|Sort-Object path,rule,version)
    $report|ConvertTo-Json -Depth 14|Set-Content -LiteralPath $ReportPath -Encoding utf8
    $secretValues.Clear();$localSecrets=$null;$plain=$null;$token=$null;$gcm=$null
}
Write-Host "Release file audit: $($report.status); candidates=$($report.candidateCount), findings=$($findings.Count). Report: $ReportPath"
if(-not $report.passed){exit 1}
