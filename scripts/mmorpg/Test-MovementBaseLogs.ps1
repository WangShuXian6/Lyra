param([Parameter(Mandatory)][string[]]$Logs)
$ErrorActionPreference='Stop'
$mmoExpected=@('MMOZone_Floor','MMOZone_WallEast','MMOZone_WallWest','MMOZone_WallNorth','MMOZone_WallSouth','MMOZone_Obstacle')
$mmoObservations=@()
foreach($mmoLog in $Logs) {
    # The dedicated server is still writing final session messages. Read a shared byte snapshot
    # and hash those exact observed bytes; do not request a file handle that denies its writer.
    $mmoStream=[IO.File]::Open($mmoLog,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    $mmoSnapshot=[IO.MemoryStream]::new()
    try {$mmoStream.CopyTo($mmoSnapshot);$mmoBytes=$mmoSnapshot.ToArray()}
    finally {$mmoStream.Dispose();$mmoSnapshot.Dispose()}
    $mmoText=[Text.Encoding]::UTF8.GetString($mmoBytes)
    $mmoFailures=@([regex]::Matches($mmoText,'(?im)^.*(?:Unable to resolve default guid from client: ObjectName: (?:StaticMeshActor_|MMOZone_)|could not resolve the new relative movement base actor).*$'))
    if($mmoFailures.Count){throw "Unresolved network movement-base references remain in $mmoLog"}
    $mmoGeometry=@([regex]::Matches($mmoText,'MMO_ZONE_GEOMETRY name=(MMOZone_[A-Za-z]+) stable=([01]) mobility=Static') | ForEach-Object {
        [pscustomobject]@{name=$_.Groups[1].Value;stable=($_.Groups[2].Value -eq '1')}
    })
    if(-not $mmoGeometry.Count -or @($mmoGeometry|Where-Object {-not $_.stable}).Count){throw 'The world did not report stable network identities for its fixed zone geometry.'}
    foreach($mmoName in $mmoExpected){if(-not @($mmoGeometry|Where-Object name -eq $mmoName).Count){throw "Required stable geometry observation is absent: $mmoName"}}
    if(@($mmoGeometry|Where-Object name -notin $mmoExpected).Count){throw 'Unexpected fixed-geometry identity in the native observations.'}
    $mmoObservations+=@{log=$mmoLog;snapshotSHA256=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($mmoBytes)).ToLowerInvariant();snapshotBytes=$mmoBytes.Length;observedNames=$mmoExpected;geometryObservationCount=$mmoGeometry.Count;unresolvedBaseMessages=0}
}
[pscustomobject]@{passed=$true;scope='Native stable names for deterministic fixed zone geometry and absence of unresolved CharacterMovement base references';logs=$mmoObservations}
