# Offline package validation and crash-recoverable installation. Dot-sourced by
# the separately launched updater; nothing here downloads or executes a package.
$script:HotdVerifiedPayloads = @{}

function Assert-HotdPath([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^[A-Za-z]:\\' -or $full.Substring(2).Contains(':')) {
        throw 'The updater requires a local Windows drive path.'
    }
    $cursor = [IO.Path]::GetPathRoot($full)
    foreach ($part in $full.Substring($cursor.Length).Split('\')) {
        if (!$part) { continue }
        $cursor = [IO.Path]::Combine($cursor, $part)
        try { $attributes = [IO.File]::GetAttributes($cursor) }
        catch [IO.FileNotFoundException] { continue }
        catch [IO.DirectoryNotFoundException] { continue }
        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Linked paths cannot be updated: $cursor"
        }
    }
    if ($full.Length -eq 3) { return $full }
    return $full.TrimEnd('\')
}

function Assert-HotdRelative([string]$Path) {
    if (!$Path -or $Path.Length -gt 180 -or $Path.Contains('\') -or $Path -match '[\x00-\x1f\x7f<>:"|?*]') {
        throw 'Invalid package path.'
    }
    foreach ($part in $Path.Split('/')) {
        if (!$part -or $part -eq '.' -or $part -eq '..' -or $part.EndsWith('.') -or $part.EndsWith(' ') -or
            $part -match '^(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)') {
            throw "Unsafe package path: $Path"
        }
    }
    return $Path
}

function Test-HotdManaged([string]$Path) {
    # Deliberately do not broaden this to *.dll, *.ini or whole directories.
    # Saves, all hotd2re_* files, original game data and visual packs are owned
    # by the player, even when an archive or an old manifest lists them.
    return $Path -match '^(?i:hotd2\.exe|scenes/st1_play\.scene|README\.txt|BUILD\.txt|CREDITS\.txt|LICENSE-SDL\.txt|LICENSE-native-code\.txt|THIRD_PARTY_NOTICES\.txt|PACKAGE-MANIFEST\.json|SHA256SUMS\.txt|nvngx_dlss\.dll|nvngx_dlssg\.dll|amd_fidelityfx_upscaler_dx12\.dll|amd_fidelityfx_framegeneration_dx12\.dll|libxess\.dll|dxcompiler\.dll|dxil\.dll|(?:vendor-licenses/(?:nvidia|amd|intel)|dxc-licenses)/[A-Za-z0-9_./ -]+\.(?:txt|md))$'
}

function Get-HotdChild([string]$Root, [string]$Relative) {
    $null = Assert-HotdRelative $Relative
    $rootPath = Assert-HotdPath $Root
    $child = Assert-HotdPath ([IO.Path]::Combine($rootPath, $Relative.Replace('/', '\')))
    if (!$child.StartsWith($rootPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped its root.' }
    return $child
}

function Get-HotdHash([string]$Path) {
    $path = Assert-HotdPath $Path
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Copy-HotdDurable([string]$Source, [string]$Destination) {
    $source = Assert-HotdPath $Source
    $destination = Assert-HotdPath $Destination
    $inputStream = [IO.File]::Open($source, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        $outputStream = [IO.FileStream]::new($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None, 65536, [IO.FileOptions]::WriteThrough)
        try { $inputStream.CopyTo($outputStream); $outputStream.Flush($true) }
        finally { $outputStream.Dispose() }
    } finally { $inputStream.Dispose() }
}

function Get-HotdJson([string]$Path) {
    $path = Assert-HotdPath $Path
    $info = [IO.FileInfo]::new($path)
    if (!$info.Exists -or $info.Length -gt 2MB) { throw 'Missing or oversized updater metadata.' }
    try { return ([IO.File]::ReadAllText($path) | ConvertFrom-Json -ErrorAction Stop) }
    catch { throw 'Malformed updater metadata.' }
}

function Assert-HotdPe([string]$Path, [string]$Architecture, [bool]$Dll = $false) {
    $stream = [IO.File]::OpenRead((Assert-HotdPath $Path))
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($stream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'Invalid Windows executable.' }
        $stream.Position = 60
        $offset = $reader.ReadUInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 26) { throw 'Invalid PE header.' }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw 'Invalid PE signature.' }
        $machine = $reader.ReadUInt16()
        $sections = $reader.ReadUInt16()
        $stream.Position = $offset + 20
        $optionalSize = $reader.ReadUInt16()
        $flags = $reader.ReadUInt16()
        $magic = $reader.ReadUInt16()
        $wantedMachine = if ($Architecture -eq 'x64') { 0x8664 } else { 0x14c }
        $wantedMagic = if ($Architecture -eq 'x64') { 0x20b } else { 0x10b }
        $minimum = if ($Architecture -eq 'x64') { 112 } else { 96 }
        if ($machine -ne $wantedMachine -or $magic -ne $wantedMagic -or $sections -lt 1 -or $sections -gt 96 -or
            ($flags -band 2) -eq 0 -or (($flags -band 0x2000) -ne 0) -ne $Dll -or $optionalSize -lt $minimum -or
            $offset + 24 + $optionalSize + $sections * 40 -gt $stream.Length) { throw 'Executable architecture or PE headers are invalid.' }
    } finally { $reader.Dispose(); $stream.Dispose() }
}

function Test-HotdBuildLabel([string]$Path, [string]$Label) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $buffer = New-Object byte[] 65536
        $tail = ''
        $needle = $Label + [char]0
        while (($count = $stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $text = $tail + [Text.Encoding]::ASCII.GetString($buffer, 0, $count)
            if ($text.Contains($needle)) { return $true }
            $tail = $text.Substring([Math]::Max(0, $text.Length - $needle.Length))
        }
        return $false
    } finally { $stream.Dispose() }
}

function Get-HotdPayload([string]$Payload, [string]$Architecture, [string]$Tag) {
    if ($Architecture -notin @('x64', 'x86') -or $Tag -cnotmatch '^v(\d{4})\.(\d{2})\.(\d{2})-([a-z]{1,16})$') {
        throw 'Invalid release architecture or version.'
    }
    $year = $Matches[1]; $month = $Matches[2]; $day = $Matches[3]; $revision = $Matches[4].ToUpperInvariant()
    $null = [datetime]::new([int]$year, [int]$month, [int]$day)
    $expectedLabel = "$year $month $day $revision"
    $alternateLabel = "$year $month $day NS32 $revision"
    $manifest = Get-HotdJson (Get-HotdChild $Payload 'PACKAGE-MANIFEST.json')
    if ($manifest.format -ne 1 -or $manifest.packageKind -cne 'public' -or $manifest.originalGameDataIncluded -ne $false -or
        $manifest.executable.architecture -cne $Architecture -or
        ($manifest.executable.buildLabel -cne $expectedLabel -and !($Architecture -eq 'x86' -and $manifest.executable.buildLabel -ceq $alternateLabel)) -or
        $manifest.executable.sha256 -cnotmatch '^[0-9a-fA-F]{64}$') { throw 'Package metadata does not match this release.' }
    $files = @($manifest.files)
    if ($files.Count -lt 2 -or $files.Count -gt 512) { throw 'Invalid package file count.' }
    $records = @{}
    foreach ($entry in $files) {
        if ($entry.path -isnot [string]) { throw 'Malformed package entry.' }
        $name = Assert-HotdRelative $entry.path
        if ($records.ContainsKey($name) -or $name -ieq 'PACKAGE-MANIFEST.json' -or $name -ieq 'SHA256SUMS.txt' -or
            (!(Test-HotdManaged $name) -and $name -cne 'game/PUT_YOUR_GAME_FILES_HERE.txt')) { throw "Unexpected package file: $name" }
        if (($entry.bytes -isnot [int] -and $entry.bytes -isnot [long]) -or $entry.bytes -lt 0 -or $entry.bytes -gt 512MB -or
            $entry.sha256 -isnot [string] -or $entry.sha256 -cnotmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid package size or hash.' }
        $path = Get-HotdChild $Payload $name
        if (![IO.File]::Exists($path) -or ([IO.FileInfo]::new($path)).Length -ne $entry.bytes -or (Get-HotdHash $path) -ine $entry.sha256) {
            throw "Package file verification failed: $name"
        }
        if ($name -imatch '\.dll$') { Assert-HotdPe $path $Architecture $true }
        $records[$name] = $entry
    }
    foreach ($required in @('hotd2.exe', 'scenes/st1_play.scene')) {
        if (!$records.ContainsKey($required) -or $records[$required].bytes -eq 0) { throw "Required program file missing: $required" }
    }
    if ($records['hotd2.exe'].sha256 -ine $manifest.executable.sha256) { throw 'Executable hashes disagree.' }
    $exe = Get-HotdChild $Payload 'hotd2.exe'
    Assert-HotdPe $exe $Architecture
    if (!(Test-HotdBuildLabel $exe $manifest.executable.buildLabel)) { throw 'Executable build label does not match release metadata.' }
    $allowed = @{} + $records
    $allowed['PACKAGE-MANIFEST.json'] = $true; $allowed['SHA256SUMS.txt'] = $true
    foreach ($file in [IO.Directory]::EnumerateFiles($Payload, '*', [IO.SearchOption]::AllDirectories)) {
        $null = Assert-HotdPath $file
        $relative = $file.Substring($Payload.Length + 1).Replace('\', '/')
        if (!$allowed.ContainsKey($relative)) { throw "Unlisted package file: $relative" }
    }
    foreach ($metadata in @('PACKAGE-MANIFEST.json', 'SHA256SUMS.txt')) {
        $path = Get-HotdChild $Payload $metadata
        if (![IO.File]::Exists($path) -or ([IO.FileInfo]::new($path)).Length -gt 2MB) { throw 'Package checksums or manifest are missing.' }
        $records[$metadata] = [pscustomobject]@{ path = $metadata; bytes = ([IO.FileInfo]::new($path)).Length; sha256 = Get-HotdHash $path }
    }
    return $records
}

function Expand-VerifiedUpdate([string]$Archive, [string]$ExpectedSha256, [string]$Stage, [string]$Architecture, [string]$Tag) {
    $ErrorActionPreference = 'Stop'
    $archivePath = Assert-HotdPath $Archive
    $stagePath = Assert-HotdPath $Stage
    if ([IO.Path]::GetFileName($stagePath) -cne 'stage' -or [IO.Path]::GetFileName([IO.Path]::GetDirectoryName($stagePath)) -cne '.hotd2-update') {
        throw 'Unexpected update staging directory.'
    }
    if ($ExpectedSha256 -cnotmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid download checksum.' }
    if ([IO.File]::Exists($stagePath) -or [IO.Directory]::Exists($stagePath)) { throw 'Update staging directory already exists.' }
    if (([IO.FileInfo]::new($archivePath)).Length -gt 512MB) { throw 'Update archive is too large.' }
    Add-Type -AssemblyName System.IO.Compression
    $zipStream = [IO.File]::Open($archivePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $zip = $null
    try {
        # Keep the same read-only handle open from hashing through extraction;
        # another process cannot swap the archive after it has been verified.
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actualHash = ([BitConverter]::ToString($sha.ComputeHash($zipStream))).Replace('-', '').ToLowerInvariant() }
        finally { $sha.Dispose() }
        if ($actualHash -ine $ExpectedSha256) { throw 'Downloaded update checksum did not match GitHub.' }
        $zipStream.Position = 0
        $zip = [IO.Compression.ZipArchive]::new($zipStream, [IO.Compression.ZipArchiveMode]::Read, $true)
        if ($zip.Entries.Count -lt 1 -or $zip.Entries.Count -gt 600) { throw 'Invalid archive entry count.' }
        $seen = @{}; $fileNames = @{}; [long]$total = 0
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName
            $directory = $name.EndsWith('/')
            $checked = Assert-HotdRelative $name.TrimEnd('/')
            if ($seen.ContainsKey($checked) -or ($checked -cne 'hotd2re' -and !$checked.StartsWith('hotd2re/', [StringComparison]::Ordinal))) { throw 'Duplicate or incorrectly rooted ZIP entry.' }
            $seen[$checked] = $true
            # UNIX symlinks and DOS reparse attributes must not reach disk.
            $unixKind = (($entry.ExternalAttributes -shr 16) -band 0xf000)
            if ($unixKind -eq 0xa000 -or ($entry.ExternalAttributes -band 0x400) -ne 0 -or ($unixKind -notin @(0, 0x8000, 0x4000))) { throw 'Linked ZIP entry is not allowed.' }
            if ($entry.Length -lt 0 -or $entry.Length -gt 512MB -or ($directory -and $entry.Length -ne 0)) { throw 'ZIP entry exceeds its size limit.' }
            $total += $entry.Length
            if ($total -gt 1GB) { throw 'Unpacked update exceeds its size limit.' }
            if (!$directory) {
                if (!$checked.StartsWith('hotd2re/', [StringComparison]::Ordinal)) { throw 'Invalid file at archive root.' }
                $relative = $checked.Substring(8)
                if (!(Test-HotdManaged $relative) -and $relative -cne 'game/PUT_YOUR_GAME_FILES_HERE.txt') { throw "Archive contains user-owned or unknown files: $relative" }
                $fileNames[$checked] = $true
            }
        }
        foreach ($name in $seen.Keys) {
            $parent = $name
            while ($parent.Contains('/')) {
                $parent = $parent.Substring(0, $parent.LastIndexOf('/'))
                if ($fileNames.ContainsKey($parent)) { throw 'ZIP file and directory paths collide.' }
            }
        }
        $null = [IO.Directory]::CreateDirectory($stagePath)
        foreach ($entry in $zip.Entries) {
            $name = $entry.FullName.TrimEnd('/')
            $destination = Get-HotdChild $stagePath $name
            if ($entry.FullName.EndsWith('/')) { $null = [IO.Directory]::CreateDirectory($destination); continue }
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
            $inputStream = $entry.Open()
            try {
                $outputStream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
                try {
                    $buffer = New-Object byte[] 65536
                    [long]$copied = 0
                    while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
                        $copied += $count
                        if ($copied -gt $entry.Length) { throw 'ZIP expanded beyond its declared size.' }
                        $outputStream.Write($buffer, 0, $count)
                    }
                    if ($copied -ne $entry.Length) { throw 'Truncated ZIP entry.' }
                    $outputStream.Flush($true)
                } finally { $outputStream.Dispose() }
            } finally { $inputStream.Dispose() }
        }
    } finally { if ($zip) { $zip.Dispose() }; $zipStream.Dispose() }
    $payload = Get-HotdChild $stagePath 'hotd2re'
    $null = Get-HotdPayload $payload $Architecture $Tag
    $script:HotdVerifiedPayloads[$payload] = @{ architecture = $Architecture; tag = $Tag; manifestHash = Get-HotdHash (Get-HotdChild $payload 'PACKAGE-MANIFEST.json') }
    return $payload
}

function Get-HotdState([string]$Root) {
    $rootPath = Assert-HotdPath $Root
    if (![IO.Directory]::Exists($rootPath)) { throw 'Installation directory is missing.' }
    return Get-HotdChild $rootPath '.hotd2-update'
}

function Test-UpdatePending([string]$Root) {
    return [IO.File]::Exists((Get-HotdChild (Get-HotdState $Root) 'transaction.json'))
}

function Open-HotdLock([string]$State) {
    $null = [IO.Directory]::CreateDirectory((Assert-HotdPath $State))
    return [IO.File]::Open((Get-HotdChild $State 'transaction.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
}

function Save-HotdJournal([string]$State, $Journal) {
    $path = Get-HotdChild $State 'transaction.json'
    $temporary = Get-HotdChild $State ('journal-' + [guid]::NewGuid().ToString('N') + '.tmp')
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes(($Journal | ConvertTo-Json -Depth 8 -Compress))
    $stream = [IO.FileStream]::new($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None, 4096, [IO.FileOptions]::WriteThrough)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    if ([IO.File]::Exists($path)) { [IO.File]::Replace($temporary, $path, [NullString]::Value) }
    else { [IO.File]::Move($temporary, $path) }
}

function Get-HotdJournal([string]$Root, [string]$State) {
    $journal = Get-HotdJson (Get-HotdChild $State 'transaction.json')
    if ($journal.format -ne 1 -or $journal.root -isnot [string] -or $journal.root -ine $Root -or
        $journal.phase -cnotin @('installing', 'committed') -or $journal.id -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid recovery journal.' }
    $entries = @($journal.files)
    if ($entries.Count -lt 2 -or $entries.Count -gt 1024) { throw 'Invalid recovery entry count.' }
    $seen = @{}
    $backupRoot = Get-HotdChild $State ('backup-' + $journal.id)
    for ($i = 0; $i -lt $entries.Count; ++$i) {
        $entry = $entries[$i]
        if ($entry.path -isnot [string] -or !(Test-HotdManaged (Assert-HotdRelative $entry.path)) -or $seen.ContainsKey($entry.path) -or
            $entry.index -ne $i -or $entry.existed -isnot [bool] -or $entry.remove -isnot [bool] -or
            (!$entry.remove -and $entry.afterHash -cnotmatch '^[0-9a-f]{64}$') -or
            ($entry.existed -and $entry.beforeHash -cnotmatch '^[0-9a-f]{64}$') -or ($entry.remove -and !$entry.existed)) { throw 'Invalid recovery file record.' }
        $seen[$entry.path] = $true
        $null = Get-HotdChild $Root $entry.path
        if ($entry.existed) {
            $backup = Get-HotdChild $backupRoot ($i.ToString() + '.bak')
            if ((Get-HotdHash $backup) -cne $entry.beforeHash) { throw 'Recovery backup is missing or corrupt; it has been kept for manual recovery.' }
        }
    }
    if (!$seen.ContainsKey('hotd2.exe') -or $entries[-1].path -ine 'hotd2.exe' -or $entries[-1].remove) { throw 'Recovery journal is missing its executable.' }
    return $journal
}

function Remove-HotdTransaction([string]$State, $Journal) {
    # Invalidate the journal before deleting any backups. A crash may leave an
    # unused backup directory, but never a live journal without its backups.
    [IO.File]::Delete((Get-HotdChild $State 'transaction.json'))
    # Commit is already durable. A locked leftover backup must not turn that
    # success into an install failure after recovery has been invalidated.
    try { Remove-HotdBackups $State $Journal.id } catch { }
}

function Remove-HotdBackups([string]$State, [string]$Id) {
    if ($Id -cnotmatch '^[0-9a-f]{32}$') { throw 'Invalid backup directory.' }
    $backupRoot = Get-HotdChild $State ('backup-' + $Id)
    if (![IO.Directory]::Exists($backupRoot)) { return }
    $files = @([IO.Directory]::EnumerateFileSystemEntries($backupRoot))
    foreach ($file in $files) {
        $null = Assert-HotdPath $file
        if ([IO.Directory]::Exists($file) -or [IO.Path]::GetFileName($file) -cnotmatch '^\d+\.bak$') { throw 'Unexpected backup contents.' }
    }
    foreach ($file in $files) { [IO.File]::Delete($file) }
    [IO.Directory]::Delete($backupRoot)
}

function Set-HotdFile([string]$State, [string]$Source, [string]$Destination, [string]$ExpectedHash) {
    $null = Assert-HotdPath $Destination
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination))
    $temporary = Get-HotdChild $State ('replace-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-HotdDurable $Source $temporary
        if ((Get-HotdHash $temporary) -cne $ExpectedHash) { throw 'Update file changed while being copied.' }
        if ([IO.File]::Exists($Destination)) { [IO.File]::Replace($temporary, $Destination, [NullString]::Value) }
        else { [IO.File]::Move($temporary, $Destination) }
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}

function Restore-HotdLocked([string]$Root, [string]$State) {
    if (![IO.File]::Exists((Get-HotdChild $State 'transaction.json'))) { return }
    $journal = Get-HotdJournal $Root $State
    $backupRoot = Get-HotdChild $State ('backup-' + $journal.id)
    # Preflight every target first. Never overwrite files modified outside this
    # transaction, and keep all backups if a path/hash cannot be accounted for.
    foreach ($entry in @($journal.files)) {
        $destination = Get-HotdChild $Root $entry.path
        if ([IO.Directory]::Exists($destination)) { throw 'A recovery target is now a directory.' }
        if ([IO.File]::Exists($destination)) {
            $current = Get-HotdHash $destination
            if ($current -cne $entry.beforeHash -and $current -cne $entry.afterHash) { throw "Recovery target was modified: $($entry.path). Backups have been preserved." }
        }
        if ($journal.phase -ceq 'committed') {
            if (($entry.remove -and [IO.File]::Exists($destination)) -or (!$entry.remove -and (![IO.File]::Exists($destination) -or (Get-HotdHash $destination) -cne $entry.afterHash))) {
                throw 'Committed update files changed; recovery backups have been preserved.'
            }
        }
    }
    if ($journal.phase -ceq 'installing') {
        foreach ($entry in @($journal.files)) {
            $destination = Get-HotdChild $Root $entry.path
            if ($entry.existed) {
                if (![IO.File]::Exists($destination) -or (Get-HotdHash $destination) -cne $entry.beforeHash) {
                    Set-HotdFile $State (Get-HotdChild $backupRoot ($entry.index.ToString() + '.bak')) $destination $entry.beforeHash
                }
            } elseif ([IO.File]::Exists($destination)) { [IO.File]::Delete($destination) }
        }
    }
    Remove-HotdTransaction $State $journal
}

function Restore-Update([string]$Root) {
    $ErrorActionPreference = 'Stop'
    $rootPath = Assert-HotdPath $Root
    $state = Get-HotdState $rootPath
    $lock = Open-HotdLock $state
    try { Restore-HotdLocked $rootPath $state } finally { $lock.Dispose() }
}

function Clear-UpdateStage([string]$Root) {
    $ErrorActionPreference = 'Stop'
    $state = Get-HotdState $Root
    $lock = Open-HotdLock $state
    try {
        if (Test-UpdatePending $Root) { throw 'Cannot clear staging while recovery is pending.' }
        $stage = Get-HotdChild $state 'stage'
        if ([IO.File]::Exists($stage)) { throw 'The staging path is not a directory.' }
        if (![IO.Directory]::Exists($stage)) { return }
        $files = [Collections.Generic.List[string]]::new()
        $directories = [Collections.Generic.List[string]]::new()
        $queue = [Collections.Generic.Queue[string]]::new()
        $queue.Enqueue($stage)
        # Validate the complete tree before deleting anything. Do not let a
        # recursive removal follow a junction outside this exact staging path.
        while ($queue.Count -gt 0) {
            $directory = $queue.Dequeue()
            $null = Assert-HotdPath $directory
            $directories.Add($directory)
            foreach ($path in [IO.Directory]::EnumerateFileSystemEntries($directory)) {
                $checked = Assert-HotdPath $path
                if (!$checked.StartsWith($stage + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Staging path escaped its root.' }
                if ([IO.Directory]::Exists($checked)) { $queue.Enqueue($checked) }
                else { $files.Add($checked) }
            }
        }
        foreach ($file in $files) { $null = Assert-HotdPath $file; [IO.File]::Delete($file) }
        foreach ($directory in ($directories | Sort-Object Length -Descending)) { $null = Assert-HotdPath $directory; [IO.Directory]::Delete($directory) }
        $script:HotdVerifiedPayloads.Remove((Get-HotdChild $stage 'hotd2re'))
    } finally { $lock.Dispose() }
}

function Install-Update([string]$Root, [string]$Payload, [scriptblock]$FaultInjector = $null, [scriptblock]$BeforeCommit = $null) {
    $ErrorActionPreference = 'Stop'
    $rootPath = Assert-HotdPath $Root
    $payloadPath = Assert-HotdPath $Payload
    $state = Get-HotdState $rootPath
    if ($payloadPath -ine (Get-HotdChild $state 'stage/hotd2re') -or !$script:HotdVerifiedPayloads.ContainsKey($payloadPath)) { throw 'Install requires a verified staged package.' }
    $receipt = $script:HotdVerifiedPayloads[$payloadPath]
    if ((Get-HotdHash (Get-HotdChild $payloadPath 'PACKAGE-MANIFEST.json')) -cne $receipt.manifestHash) { throw 'Staged manifest changed after verification.' }
    $records = Get-HotdPayload $payloadPath $receipt.architecture $receipt.tag
    $lock = Open-HotdLock $state
    $id = $null
    $journalSaved = $false
    try {
        if (Test-UpdatePending $rootPath) { throw 'An earlier update must be recovered before installing.' }
        $names = @($records.Keys | Where-Object { Test-HotdManaged $_ })
        $remove = @{}
        $oldManifest = Get-HotdChild $rootPath 'PACKAGE-MANIFEST.json'
        if ([IO.File]::Exists($oldManifest)) {
            $old = Get-HotdJson $oldManifest
            if ($old.format -ne 1 -or @($old.files).Count -gt 512) { throw 'Existing package manifest is invalid.' }
            foreach ($entry in @($old.files)) {
                if ($entry.path -isnot [string]) { throw 'Invalid old package record.' }
                $name = Assert-HotdRelative $entry.path
                if (!(Test-HotdManaged $name) -or $records.ContainsKey($name) -or $name -ieq 'hotd2.exe') { continue }
                $path = Get-HotdChild $rootPath $name
                if ([IO.File]::Exists($path)) {
                    if ($entry.sha256 -cnotmatch '^[0-9a-fA-F]{64}$' -or (Get-HotdHash $path) -ine $entry.sha256) { throw "Obsolete program file was modified: $name" }
                    if (!$remove.ContainsKey($name)) { $remove[$name] = $true; $names += $name }
                }
            }
        }
        $names = @($names | Where-Object { $_ -ine 'hotd2.exe' } | Sort-Object) + @('hotd2.exe')
        $id = [guid]::NewGuid().ToString('N')
        $backupRoot = Get-HotdChild $state ('backup-' + $id)
        $null = [IO.Directory]::CreateDirectory($backupRoot)
        $entries = @()
        foreach ($name in $names) {
            $destination = Get-HotdChild $rootPath $name
            if ([IO.Directory]::Exists($destination)) { throw "Program file is a directory: $name" }
            $existed = [IO.File]::Exists($destination)
            $beforeHash = $null
            if ($existed) {
                $beforeHash = Get-HotdHash $destination
                $backup = Get-HotdChild $backupRoot ($entries.Count.ToString() + '.bak')
                Copy-HotdDurable $destination $backup
                if ((Get-HotdHash $backup) -cne $beforeHash) { throw 'Installed file changed while backing it up.' }
            }
            $afterHash = if ($remove.ContainsKey($name)) { $null } else { $records[$name].sha256.ToLowerInvariant() }
            $entries += [pscustomobject]@{ path = $name; index = $entries.Count; existed = $existed; remove = $remove.ContainsKey($name); beforeHash = $beforeHash; afterHash = $afterHash }
        }
        $journal = [pscustomobject]@{ format = 1; root = $rootPath; id = $id; phase = 'installing'; files = $entries }
        Save-HotdJournal $state $journal
        $journalSaved = $true
        try {
            foreach ($entry in $entries) {
                $destination = Get-HotdChild $rootPath $entry.path
                if ($entry.existed -and (Get-HotdHash $destination) -cne $entry.beforeHash) { throw 'Installed file changed during the update.' }
                if (!$entry.existed -and [IO.File]::Exists($destination)) { throw 'A new file appeared during the update.' }
                if ($entry.remove) { [IO.File]::Delete($destination) }
                else { Set-HotdFile $state (Get-HotdChild $payloadPath $entry.path) $destination $entry.afterHash }
                if ($FaultInjector) { $null = & $FaultInjector $entry.index $entry.path }
            }
            foreach ($entry in $entries) {
                $destination = Get-HotdChild $rootPath $entry.path
                if (($entry.remove -and [IO.File]::Exists($destination)) -or (!$entry.remove -and (Get-HotdHash $destination) -cne $entry.afterHash)) { throw 'Installed update verification failed.' }
            }
            if ($BeforeCommit) { $null = & $BeforeCommit }
            $journal.phase = 'committed'
            Save-HotdJournal $state $journal
        } catch {
            $failure = $_
            try { Restore-HotdLocked $rootPath $state }
            catch { throw "Update failed and recovery needs another attempt. Keep .hotd2-update; it contains your original files. $($_.Exception.Message)" }
            throw "Update failed; the previous version was restored. $($failure.Exception.Message)"
        }
        Remove-HotdTransaction $state $journal
    } catch {
        if (!$journalSaved -and $id -and !(Test-UpdatePending $rootPath)) {
            try { Remove-HotdBackups $state $id } catch { }
        }
        throw
    } finally { $lock.Dispose() }
}
