param([string]$RequestPath)

# This script is embedded in the client and runs in a private updater session.
# Dot-sourcing only defines helpers, so the protocol and Windows launch can be tested offline.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:UpdateForm = $null
$script:UpdateStatus = $null
$script:UpdateProgress = $null

function ConvertTo-UpdateArgument([AllowEmptyString()][string]$Value) {
    # CommandLineToArgvW / C runtime rules, including quotes and trailing backslashes.
    $result = New-Object System.Text.StringBuilder
    [void]$result.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            [void]$result.Append('\', (2 * $slashes + 1))
        } else {
            [void]$result.Append('\', $slashes)
        }
        [void]$result.Append($character)
        $slashes = 0
    }
    [void]$result.Append('\', (2 * $slashes))
    [void]$result.Append('"')
    return $result.ToString()
}

function Get-UpdateCommandLine($Request) {
    $parts = @((ConvertTo-UpdateArgument $Request.executable))
    foreach ($argument in $Request.arguments) { $parts += ConvertTo-UpdateArgument $argument }
    $line = $parts -join ' '
    if ($line.Length -gt 32766) { throw 'The game launch options are too long.' }
    return $line
}

function Assert-UpdateDownloadUri([Uri]$Uri) {
    if (-not $Uri.IsAbsoluteUri -or $Uri.Scheme -cne 'https' -or $Uri.Port -ne 443 -or
        $Uri.UserInfo -ne '' -or $Uri.Fragment -ne '') {
        throw 'The download address could not be verified.'
    }
    if ($Uri.DnsSafeHost -ceq 'github.com') {
        if ($Uri.AbsolutePath -cnotmatch '\A/GitFist/hotd2-re/releases/download/v[0-9]{4}\.[0-9]{2}\.[0-9]{2}-[a-z]{1,16}/(?:hotd2re-win-(?:x64|x86)\.(?:zip|files\.json)(?:\.sha256)?|hotd2re-file-[0-9a-f]{64}\.gz)\z' -or $Uri.Query -ne '') {
            throw 'The download address could not be verified.'
        }
    } elseif ($Uri.DnsSafeHost -cnotin @('release-assets.githubusercontent.com', 'objects.githubusercontent.com', 'github-releases.githubusercontent.com')) {
        throw 'The download address could not be verified.'
    }
}

function Get-UpdateChecksum([string]$Text, [string]$Asset) {
    $pattern = '\A([0-9a-fA-F]{64})(?:[ \t]+\*?' + [regex]::Escape($Asset) + ')?[\r\n]*\z'
    $match = [regex]::Match($Text, $pattern)
    if (-not $match.Success) { throw 'The update verification file is invalid.' }
    return $match.Groups[1].Value.ToLowerInvariant()
}

function Read-UpdateRequest([string]$Path, [string]$SessionDirectory) {
    if (-not [IO.Path]::IsPathRooted($Path) -or (Get-Item -LiteralPath $Path).Length -gt 65536) {
        throw 'The update request is invalid.'
    }
    $pathFull = [IO.Path]::GetFullPath($Path)
    $sessionFull = [IO.Path]::GetFullPath($SessionDirectory).TrimEnd('\')
    if (-not $pathFull.Equals((Join-Path $sessionFull 'request.json'), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The update request is in the wrong folder.'
    }
    $request = [IO.File]::ReadAllText($pathFull) | ConvertFrom-Json
    foreach ($field in @('root', 'tag', 'architecture', 'parentPid', 'parentStartTime', 'executable', 'arguments', 'workingDirectory', 'recover')) {
        if (-not $request.PSObject.Properties[$field]) { throw 'The update request is incomplete.' }
    }
    foreach ($field in @('root', 'executable', 'workingDirectory')) {
        if ($request.$field -isnot [string] -or $request.$field -notmatch '\A(?:[A-Za-z]:[\\/]|\\\\[^\\/]+[\\/][^\\/]+[\\/])' -or $request.$field.Contains([char]0)) {
            throw 'The game folder could not be verified.'
        }
        $request.$field = [IO.Path]::GetFullPath($request.$field).TrimEnd('\')
    }
    $control = Join-Path $request.root '.hotd2-update'
    if (-not ([IO.Path]::GetDirectoryName($sessionFull)).Equals($control, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($sessionFull) -cnotmatch '\Asession-[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}\z') {
        throw 'The updater folder could not be verified.'
    }
    foreach ($pathToCheck in @($request.root, $control, $sessionFull, $pathFull)) {
        if ((Get-Item -LiteralPath $pathToCheck -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'The updater cannot use a linked folder or file.'
        }
    }
    if (-not $request.executable.Equals((Join-Path $request.root 'hotd2.exe'), [StringComparison]::OrdinalIgnoreCase) -or
        -not [IO.Directory]::Exists($request.workingDirectory)) {
        throw 'The game location could not be verified.'
    }
    # The executable can be absent during interrupted replacement; recovery must still work.
    if ($request.recover -isnot [bool] -or $request.architecture -cnotin @('x64', 'x86') -or
        $request.tag -isnot [string] -or (-not $request.recover -and $request.tag -cnotmatch '\Av[0-9]{4}\.[0-9]{2}\.[0-9]{2}-[a-z]{1,16}\z') -or
        [string]$request.parentPid -notmatch '\A[1-9][0-9]{0,9}\z' -or [long]$request.parentPid -gt [int]::MaxValue -or
        [string]$request.parentStartTime -notmatch '\A[1-9][0-9]{0,18}\z' -or $request.arguments -isnot [Array]) {
        throw 'The update request is invalid.'
    }
    $date = [datetime]::MinValue
    if (-not $request.recover -and -not [datetime]::TryParseExact($request.tag.Substring(1, 10), 'yyyy.MM.dd', [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::None, [ref]$date)) {
        throw 'The update version is invalid.'
    }
    $null = [long]::Parse([string]$request.parentStartTime, [Globalization.CultureInfo]::InvariantCulture)
    foreach ($argument in $request.arguments) {
        if ($argument -isnot [string] -or $argument.Contains([char]0)) { throw 'A game launch option is invalid.' }
    }
    $null = Get-UpdateCommandLine $request
    return $request
}

function Get-UpdateMutexName([string]$Root) {
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $hash.ComputeHash([Text.Encoding]::UTF8.GetBytes([IO.Path]::GetFullPath($Root).TrimEnd('\').ToUpperInvariant()))
        return 'Local\Hotd2Update-' + ([BitConverter]::ToString($bytes)).Replace('-', '')
    } finally { $hash.Dispose() }
}

function Initialize-UpdateWindow {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    if (-not ('Hotd2Update.ProgressWindow' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace Hotd2Update {
    public static class ProgressWindow {
        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")]
        private static extern bool IsWindowVisible(IntPtr window);
        public static void Show(IntPtr window) {
            // Form.Visible can be true even when STARTUPINFO hid the native
            // window. A second explicit show repairs that inherited state.
            ShowWindow(window, 5);
            if (!IsWindowVisible(window))
                throw new InvalidOperationException("The update progress window could not be shown. You can keep playing and try again.");
        }
    }
}
'@
    }
    [Windows.Forms.Application]::EnableVisualStyles()
    $script:UpdateForm = New-Object Windows.Forms.Form
    $script:UpdateForm.Text = 'The House of the Dead 2 - Update'
    $script:UpdateForm.ClientSize = New-Object Drawing.Size(430, 115)
    $script:UpdateForm.StartPosition = 'CenterScreen'
    $script:UpdateForm.FormBorderStyle = 'FixedDialog'
    $script:UpdateForm.ControlBox = $false
    $script:UpdateForm.Add_FormClosing({ param($sender, $eventArgs) $eventArgs.Cancel = $true })
    $script:UpdateStatus = New-Object Windows.Forms.Label
    $script:UpdateStatus.SetBounds(20, 20, 390, 35)
    $script:UpdateStatus.Text = 'Preparing the update...'
    $script:UpdateProgress = New-Object Windows.Forms.ProgressBar
    $script:UpdateProgress.SetBounds(20, 65, 390, 22)
    $script:UpdateProgress.Style = 'Marquee'
    $script:UpdateForm.Controls.Add($script:UpdateStatus)
    $script:UpdateForm.Controls.Add($script:UpdateProgress)
    $script:UpdateForm.Show()
    [Hotd2Update.ProgressWindow]::Show($script:UpdateForm.Handle)
    [Windows.Forms.Application]::DoEvents()
}

function Set-UpdateStatus([string]$Text, [int]$Percent = -1) {
    if ($null -eq $script:UpdateForm) { return }
    $script:UpdateStatus.Text = $Text
    if ($Percent -lt 0) { $script:UpdateProgress.Style = 'Marquee' } else {
        $script:UpdateProgress.Style = 'Continuous'
        $script:UpdateProgress.Value = [Math]::Max(0, [Math]::Min(100, $Percent))
    }
    [Windows.Forms.Application]::DoEvents()
}

function Wait-UpdateTask($Task, [datetime]$Deadline) {
    $operationDeadline = [datetime]::UtcNow.AddSeconds(30)
    while (-not $Task.IsCompleted) {
        if ([datetime]::UtcNow -gt $Deadline -or [datetime]::UtcNow -gt $operationDeadline) {
            throw 'The download timed out. Please check your connection and try again.'
        }
        if ($null -ne $script:UpdateForm) { [Windows.Forms.Application]::DoEvents() }
        Start-Sleep -Milliseconds 30
    }
    return $Task.GetAwaiter().GetResult()
}

function Save-UpdateDownload([Uri]$Uri, [string]$Destination, [long]$MaximumBytes, [string]$Description) {
    $deadline = [datetime]::UtcNow.AddMinutes(10)
    $response = $null
    $request = $null
    $stream = $null
    $file = $null
    try {
        for ($redirect = 0; $redirect -le 5; $redirect++) {
            Assert-UpdateDownloadUri $Uri
            $request = [Net.HttpWebRequest]::Create($Uri)
            $request.AllowAutoRedirect = $false
            $request.UserAgent = 'hotd2-re-updater'
            $request.Timeout = 30000
            $request.ReadWriteTimeout = 30000
            $request.Proxy = [Net.WebRequest]::DefaultWebProxy
            $response = Wait-UpdateTask ($request.GetResponseAsync()) $deadline
            $code = [int]$response.StatusCode
            if ($code -in @(301, 302, 303, 307, 308)) {
                if ($redirect -eq 5 -or [string]::IsNullOrWhiteSpace($response.Headers['Location'])) { throw 'The download address could not be verified.' }
                $Uri = New-Object Uri($Uri, $response.Headers['Location'])
                $response.Dispose()
                $response = $null
                continue
            }
            if ($code -ne 200) { throw 'The update could not be downloaded. Please try again later.' }
            break
        }
        $length = [long]$response.ContentLength
        if ($length -gt $MaximumBytes) { throw 'The update download is larger than expected.' }
        $stream = $response.GetResponseStream()
        $file = New-Object IO.FileStream($Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $buffer = New-Object byte[] 65536
        $received = [long]0
        do {
            $count = Wait-UpdateTask ($stream.ReadAsync($buffer, 0, $buffer.Length)) $deadline
            if ($count -eq 0) { break }
            $received += $count
            if ($received -gt $MaximumBytes) { throw 'The update download is larger than expected.' }
            $file.Write($buffer, 0, $count)
            $percent = if ($length -gt 0) { [int](100.0 * $received / $length) } else { -1 }
            Set-UpdateStatus $Description $percent
        } while ($true)
        if ($received -eq 0 -or ($length -ge 0 -and $received -ne $length)) { throw 'The update download was interrupted. Please try again.' }
        $file.Flush($true)
    } finally {
        if ($null -ne $file) { $file.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
        if ($null -ne $response) { $response.Dispose() }
        if ($null -ne $request) { $request.Abort() }
    }
}

function Wait-UpdateParent($Request) {
    Set-UpdateStatus 'Waiting for the game to close...'
    $process = $null
    try { $process = [Diagnostics.Process]::GetProcessById([int]$Request.parentPid) } catch [ArgumentException] { return }
    try {
        if ($process.Id -eq $PID) { throw 'The updater cannot wait for itself.' }
        if ($process.HasExited) { return }
        # A recycled process ID does not identify the game that requested this update.
        if ($process.StartTime.ToUniversalTime().ToFileTimeUtc() -ne [long]$Request.parentStartTime) { return }
        $deadline = [datetime]::UtcNow.AddSeconds(120)
        while (-not $process.WaitForExit(50)) {
            if ($null -ne $script:UpdateForm) { [Windows.Forms.Application]::DoEvents() }
            if ([datetime]::UtcNow -gt $deadline) { throw 'The game did not close. Please close it and try again.' }
        }
    } finally { if ($null -ne $process) { $process.Dispose() } }
}

function Initialize-UpdateLaunch {
    if ('Hotd2Update.Launch' -as [type]) { return }
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
namespace Hotd2Update {
    public sealed class Launch : IDisposable {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct StartupInfo {
            public int cb; public string reserved, desktop, title;
            public int x, y, xSize, ySize, xChars, yChars, fill, flags;
            public short show, reservedSize; public IntPtr reservedData, input, output, error;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct StartupInfoEx { public StartupInfo startup; public IntPtr attributes; }
        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInfo { public IntPtr process, thread; public int processId, threadId; }
        [StructLayout(LayoutKind.Sequential)]
        private struct BasicJobLimits {
            public long processTime, jobTime; public uint flags;
            public UIntPtr minimumWorkingSet, maximumWorkingSet; public uint activeProcesses;
            public UIntPtr affinity; public uint priorityClass, schedulingClass;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct IoCounters { public ulong readOps, writeOps, otherOps, readBytes, writeBytes, otherBytes; }
        [StructLayout(LayoutKind.Sequential)]
        private struct JobLimits {
            public BasicJobLimits basic; public IoCounters io;
            public UIntPtr processMemory, jobMemory, peakProcessMemory, peakJobMemory;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateProcessW(string application, StringBuilder command, IntPtr processAttributes,
            IntPtr threadAttributes, bool inherit, int flags, IntPtr environment, string directory,
            ref StartupInfoEx startup, out ProcessInfo process);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref JobLimits limits, uint length);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool InitializeProcThreadAttributeList(IntPtr attributes, int count, int flags, ref IntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool UpdateProcThreadAttribute(IntPtr attributes, int flags, IntPtr attribute,
            IntPtr value, IntPtr size, IntPtr previous, IntPtr returned);
        [DllImport("kernel32.dll")] private static extern void DeleteProcThreadAttributeList(IntPtr attributes);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll")] private static extern uint WaitForSingleObject(IntPtr handle, uint timeout);
        [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
        private ProcessInfo info;
        private IntPtr job;
        public int ProcessId { get { return info.processId; } }
        public static Launch Suspended(string executable, string arguments, string directory) {
            var launch = new Launch();
            var startup = new StartupInfoEx(); startup.startup.cb = Marshal.SizeOf(typeof(StartupInfoEx));
            IntPtr jobValue = IntPtr.Zero;
            bool initialized = false;
            try {
                launch.job = CreateJobObjectW(IntPtr.Zero, null);
                if (launch.job == IntPtr.Zero) throw new Win32Exception();
                var limits = new JobLimits(); limits.basic.flags = 0x2000; // KILL_ON_JOB_CLOSE
                if (!SetInformationJobObject(launch.job, 9, ref limits, (uint)Marshal.SizeOf(typeof(JobLimits)))) throw new Win32Exception();
                IntPtr size = IntPtr.Zero;
                InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
                if (size == IntPtr.Zero) throw new Win32Exception();
                startup.attributes = Marshal.AllocHGlobal(size);
                if (!InitializeProcThreadAttributeList(startup.attributes, 1, 0, ref size)) throw new Win32Exception();
                initialized = true;
                jobValue = Marshal.AllocHGlobal(IntPtr.Size);
                Marshal.WriteIntPtr(jobValue, launch.job);
                // Atomic job assignment prevents an orphaned suspended game if the updater
                // is killed in the instant between process creation and transaction commit.
                if (!UpdateProcThreadAttribute(startup.attributes, 0, new IntPtr(0x2000d), jobValue,
                    new IntPtr(IntPtr.Size), IntPtr.Zero, IntPtr.Zero)) throw new Win32Exception();
                if (!CreateProcessW(executable, new StringBuilder(arguments), IntPtr.Zero, IntPtr.Zero, false,
                    0x80004, IntPtr.Zero, directory, ref startup, out launch.info)) throw new Win32Exception();
                return launch;
            } catch { launch.Dispose(); throw; }
            finally {
                if (initialized) DeleteProcThreadAttributeList(startup.attributes);
                if (startup.attributes != IntPtr.Zero) Marshal.FreeHGlobal(startup.attributes);
                if (jobValue != IntPtr.Zero) Marshal.FreeHGlobal(jobValue);
            }
        }
        public void Resume() {
            if (ResumeThread(info.thread) == 0xffffffff) throw new Win32Exception();
            // Until this succeeds, updater death still closes the job and terminates the
            // newly committed game; the next ordinary launch can safely start it again.
            var limits = new JobLimits();
            if (!SetInformationJobObject(job, 9, ref limits, (uint)Marshal.SizeOf(typeof(JobLimits)))) throw new Win32Exception();
        }
        public void Terminate() {
            if (info.process != IntPtr.Zero) {
                if (WaitForSingleObject(info.process, 0) == 0) return;
                if (!TerminateProcess(info.process, 1)) throw new Win32Exception();
                WaitForSingleObject(info.process, 5000);
            }
        }
        public void Dispose() {
            if (info.thread != IntPtr.Zero) { CloseHandle(info.thread); info.thread = IntPtr.Zero; }
            if (info.process != IntPtr.Zero) { CloseHandle(info.process); info.process = IntPtr.Zero; }
            if (job != IntPtr.Zero) { CloseHandle(job); job = IntPtr.Zero; }
        }
    }
}
'@
}

function Start-UpdateGame($Request) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $Request.executable
    $start.Arguments = (@($Request.arguments | ForEach-Object { ConvertTo-UpdateArgument $_ }) -join ' ')
    $start.WorkingDirectory = $Request.workingDirectory
    $start.UseShellExecute = $false
    $process = [Diagnostics.Process]::Start($start)
    if ($null -eq $process) { throw 'Windows could not start the game.' }
    $process.Dispose()
}

function Show-UpdateMessage([string]$Message) {
    if ($null -ne $script:UpdateForm) {
        [Hotd2Update.ProgressWindow]::Show($script:UpdateForm.Handle)
        [void][Windows.Forms.MessageBox]::Show($script:UpdateForm, $Message, 'The House of the Dead 2 - Update', 'OK', 'Warning')
    } else {
        [void][Windows.Forms.MessageBox]::Show($Message, 'The House of the Dead 2 - Update', 'OK', 'Warning')
    }
}

function Get-UpdatePayload($Request, [string]$SessionDirectory) {
    $release = 'https://github.com/GitFist/hotd2-re/releases/download/' + $Request.tag + '/'
    $stem = 'hotd2re-win-' + $Request.architecture
    $stage = Join-Path $Request.root '.hotd2-update/stage'
    Clear-UpdateStage -Root $Request.root
    try {
        # New releases describe independently compressed files. Verify the index,
        # reuse matching installed bytes, and validate the same complete payload
        # that the ZIP path supplies before any installed file can be changed.
        $indexAsset = $stem + '.files.json'
        $indexChecksum = Join-Path $SessionDirectory 'files.sha256'
        $indexPath = Join-Path $SessionDirectory 'files.json'
        Set-UpdateStatus 'Checking which files changed...'
        Save-UpdateDownload ([Uri]($release + $indexAsset + '.sha256')) $indexChecksum 512 'Checking the update...'
        $hash = Get-UpdateChecksum ([IO.File]::ReadAllText($indexChecksum)) $indexAsset
        Save-UpdateDownload ([Uri]($release + $indexAsset)) $indexPath 2097152 'Checking which files changed...'
        $download = {
            param($Asset, $Destination, $MaximumBytes, $Description)
            Save-UpdateDownload ([Uri]($release + $Asset)) $Destination $MaximumBytes $Description
        }.GetNewClosure()
        return Expand-VerifiedFileUpdate -Root $Request.root -IndexPath $indexPath -ExpectedSha256 $hash -Stage $stage `
            -Architecture $Request.architecture -Tag $Request.tag -DownloadFile $download
    } catch {
        # Old releases have no file index. Missing/corrupt blobs or unusable local
        # files also fall back to the independently verified full client ZIP.
        # Cleanup must succeed before reusing the staging directory.
        Clear-UpdateStage -Root $Request.root
        Set-UpdateStatus 'Using the full client download...'
    }
    $asset = $stem + '.zip'
    $checksumPath = Join-Path $SessionDirectory 'download.sha256'
    $archive = Join-Path $SessionDirectory 'download.zip'
    Save-UpdateDownload ([Uri]($release + $asset + '.sha256')) $checksumPath 512 'Checking the full update...'
    $hash = Get-UpdateChecksum ([IO.File]::ReadAllText($checksumPath)) $asset
    Save-UpdateDownload ([Uri]($release + $asset)) $archive 536870912 'Downloading the full client update...'
    Set-UpdateStatus 'Verifying the update...'
    return Expand-VerifiedUpdate -Archive $archive -ExpectedSha256 $hash -Stage $stage `
        -Architecture $Request.architecture -Tag $Request.tag
}

function Invoke-UpdateRunner([string]$Path, [string]$SessionDirectory = $PSScriptRoot) {
    $request = $null
    $mutex = $null
    $ownsMutex = $false
    $ready = $false
    $parentExited = $false
    $installed = $false
    $recovered = $false
    $preserveState = $false
    $launchState = @{ child = $null }
    try {
        $request = Read-UpdateRequest $Path $SessionDirectory
        $mutex = New-Object Threading.Mutex($false, (Get-UpdateMutexName $request.root))
        try { $ownsMutex = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
        if (-not $ownsMutex) { throw 'Another update is already running for this game.' }
        . (Join-Path $SessionDirectory 'update_transaction.ps1')
        Initialize-UpdateLaunch
        Initialize-UpdateWindow
        [IO.File]::WriteAllText((Join-Path $SessionDirectory 'ready'), 'ready')
        $ready = $true
        Wait-UpdateParent $request
        $parentExited = $true
        if (Test-UpdatePending -Root $request.root) {
            Set-UpdateStatus 'Restoring your previous version...'
            Restore-Update -Root $request.root
        }
        if ($request.recover) {
            Set-UpdateStatus 'Starting the game...'
            Start-UpdateGame $request
            $recovered = $true
            return 0
        }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $payload = Get-UpdatePayload $request $SessionDirectory
        Set-UpdateStatus 'Installing the update. Your saves and settings will be kept.'
        $beforeCommit = {
            $launchState.child = [Hotd2Update.Launch]::Suspended($request.executable, (Get-UpdateCommandLine $request), $request.workingDirectory)
        }.GetNewClosure()
        Install-Update -Root $request.root -Payload $payload -BeforeCommit $beforeCommit
        $installed = $true
        Set-UpdateStatus 'Update complete. Starting the game...' 100
        $launchState.child.Resume()
        $launchState.child.Dispose()
        $launchState.child = $null
        return 0
    } catch {
        $failureReason = $_.Exception.GetBaseException().Message
        if ($null -ne $launchState.child) {
            try { $launchState.child.Terminate() } catch { $preserveState = $true }
            $launchState.child.Dispose()
            $launchState.child = $null
        }
        if (-not $ready) {
            # The parent is still running and displays this error; do not open a second dialog.
            try { [IO.File]::WriteAllText((Join-Path $SessionDirectory 'error.txt'), $_.Exception.Message) } catch {}
            return 1
        }
        $rollbackFailed = $false
        if ($parentExited) {
            try {
                if (Test-UpdatePending -Root $request.root) {
                    Set-UpdateStatus 'Restoring your previous version...'
                    Restore-Update -Root $request.root
                }
            } catch { $rollbackFailed = $true; $preserveState = $true }
        }
        if ($rollbackFailed) {
            $message = 'The update could not finish, and some files could not be restored. Your backup has been kept. Close any other copies of the game, then launch the game again to retry recovery.'
        } elseif ($installed) {
            $message = 'The update was installed, but Windows could not restart the game. Please launch the game again. Your saves and settings have been kept.'
        } elseif (-not $parentExited) {
            $message = 'The game did not close, so the update was cancelled. Close any other copies of the game and try again.'
        } elseif ($request.recover -and -not $recovered) {
            $message = 'Your previous version has been restored, but Windows could not restart it. Please launch the game again.'
        } else {
            $message = 'The update could not be completed. Your previous version, saves, and settings have been kept. Check your connection and available disk space, then try again. The game will reopen.'
        }
        Set-UpdateStatus 'The update needs your attention.'
        Show-UpdateMessage ($message + "`n`nReason: " + $failureReason)
        if ($parentExited -and -not $rollbackFailed -and -not $installed -and -not $request.recover) {
            try { Start-UpdateGame $request } catch {
                Show-UpdateMessage 'Windows could not restart the game. Please launch it again.'
            }
        }
        return 1
    } finally {
        if ($null -ne $script:UpdateForm) { $script:UpdateForm.Dispose(); $script:UpdateForm = $null }
        if ($ownsMutex -and $ready -and -not $preserveState) {
            try { Clear-UpdateStage -Root $request.root } catch { $preserveState = $true }
        }
        if ($ownsMutex) { $mutex.ReleaseMutex() }
        if ($null -ne $mutex) { $mutex.Dispose() }
        # Only remove this validated session; interrupted transaction backups live outside it.
        if ($ready) {
            try {
                $resolved = [IO.Path]::GetFullPath($SessionDirectory)
                $control = Join-Path $request.root '.hotd2-update'
                if (([IO.Path]::GetDirectoryName($resolved)).Equals($control, [StringComparison]::OrdinalIgnoreCase) -and
                    -not ((Get-Item -LiteralPath $resolved -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                    # This folder only holds direct files. Never recurse into an unexpected
                    # subfolder or link, even if another process created one during the update.
                    $files = @(Get-ChildItem -LiteralPath $resolved -Force)
                    foreach ($file in $files) {
                        if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected updater session entry.' }
                    }
                    foreach ($file in $files) { Remove-Item -LiteralPath $file.FullName -Force }
                    [IO.Directory]::Delete($resolved, $false)
                }
            } catch {}
        }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-UpdateRunner $RequestPath)
}
