"""Real Windows PowerShell/WinForms startup; no downloads, installation or game launch."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
CHILD = r'''
param([string]$Runner, [string]$Request, [string]$Session, [string]$Output, [string]$Stop)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. $Runner
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class UpdateWindowProbe {
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
}
"@
function Wait-UpdateParent($Request) {
    # This is the production runner's first operation after publishing ready.
    # Keep its real progress form alive while the parent checks the HWND too.
    $state = [ordered]@{
        ready = [IO.File]::Exists((Join-Path $Session 'ready'))
        powershell = $PSVersionTable.PSVersion.ToString()
        formVisible = $script:UpdateForm.Visible
        nativeVisible = [UpdateWindowProbe]::IsWindowVisible($script:UpdateForm.Handle)
        window = $script:UpdateForm.Handle.ToInt64()
        console = [UpdateWindowProbe]::GetConsoleWindow().ToInt64()
    }
    Set-UpdateStatus 'Testing visible update progress...' 37
    $state.status = $script:UpdateStatus.Text
    $state.percent = $script:UpdateProgress.Value
    $state.visibleAfterStatus = [UpdateWindowProbe]::IsWindowVisible($script:UpdateForm.Handle)
    [IO.File]::WriteAllText(($Output + '.tmp'), ($state | ConvertTo-Json -Compress))
    [IO.File]::Move(($Output + '.tmp'), $Output)
    $deadline = [datetime]::UtcNow.AddSeconds(15)
    while (-not [IO.File]::Exists($Stop)) {
        if ([datetime]::UtcNow -gt $deadline) { throw 'Visibility test parent did not finish.' }
        [Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 20
    }
}
function Save-UpdateDownload { throw 'Window test must never download.' }
function Start-UpdateGame { }  # Recovery completes without starting any executable.
function Show-UpdateMessage([string]$Message) { [Console]::Error.WriteLine($Message) }
exit (Invoke-UpdateRunner $Request $Session)
'''


@unittest.skipUnless(os.name == 'nt', 'Requires an interactive Windows desktop')
class UpdateWindowTest(unittest.TestCase):
    def test_visible_before_ready_without_a_console(self):
        powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        user32 = ctypes.WinDLL('user32', use_last_error=True)
        user32.IsWindowVisible.argtypes = [ctypes.c_void_p]
        user32.IsWindowVisible.restype = ctypes.c_int
        user32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
        user32.GetWindowThreadProcessId.restype = ctypes.c_ulong
        for hidden_startup in (False, True):
            # The second case reproduces the former native launcher flags.
            # Form.Visible alone incorrectly reports True under SW_HIDE.
            with self.subTest(hidden_startup=hidden_startup), tempfile.TemporaryDirectory(prefix='hotd2-update-window-test-') as temporary:
                scratch = Path(temporary)
                root = scratch / 'game'
                session = root / '.hotd2-update/session-00000000-0000-0000-0000-000000000001'
                session.mkdir(parents=True)
                request = session / 'request.json'
                request.write_text(json.dumps(dict(root=str(root), tag='', architecture='x64',
                    parentPid=os.getpid(), parentStartTime='1', executable=str(root / 'hotd2.exe'),
                    arguments=[], workingDirectory=str(root), recover=True)), encoding='utf-8')
                # No transaction is needed for this recovery-only UI fixture.
                (session / 'update_transaction.ps1').write_text(
                    'function Test-UpdatePending { param($Root) return $false }\n'
                    'function Clear-UpdateStage { param($Root) }\n', encoding='utf-8')
                child = scratch / 'window.ps1'
                child.write_text(CHILD, encoding='utf-8')
                output, stop = scratch / 'observed.json', scratch / 'stop'
                startup = subprocess.STARTUPINFO()
                if hidden_startup:
                    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
                    startup.wShowWindow = subprocess.SW_HIDE
                process = subprocess.Popen([str(powershell), '-NoLogo', '-NoProfile', '-NonInteractive',
                    '-ExecutionPolicy', 'Bypass', '-STA', '-File', str(child),
                    '-Runner', str(ROOT / 'src/port/update/update_runner.ps1'),
                    '-Request', str(request), '-Session', str(session), '-Output', str(output), '-Stop', str(stop)],
                    startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW,
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                try:
                    deadline = time.monotonic() + 15
                    while not output.exists():
                        if process.poll() is not None:
                            stdout, stderr = process.communicate()
                            error = session / 'error.txt'
                            self.fail(f'Helper exited before readiness: {stdout} {stderr} '
                                      + (error.read_text() if error.exists() else ''))
                        if time.monotonic() > deadline:
                            self.fail('Helper did not reach its ready handshake within 15 seconds')
                        time.sleep(.025)
                    state = json.loads(output.read_text())
                    self.assertTrue(state['powershell'].startswith('5.1.'))
                    self.assertTrue(state['ready'], 'Runner must publish readiness before waiting for its parent')
                    self.assertTrue(state['formVisible'])
                    self.assertTrue(state['nativeVisible'], 'Native progress window is hidden despite readiness')
                    self.assertEqual(state['status'], 'Testing visible update progress...')
                    self.assertEqual(state['percent'], 37)
                    self.assertTrue(state['visibleAfterStatus'], 'Status updates must keep progress visible')
                    self.assertTrue(user32.IsWindowVisible(state['window']), 'Progress HWND must remain visible')
                    owner = ctypes.c_ulong()
                    self.assertNotEqual(user32.GetWindowThreadProcessId(state['window'], ctypes.byref(owner)), 0)
                    self.assertEqual(owner.value, process.pid, 'Check the helper window, not another application')
                    self.assertEqual(state['console'], 0, 'Progress must appear without a PowerShell console')
                    stop.write_text('stop')
                    stdout, stderr = process.communicate(timeout=10)
                    self.assertEqual(process.returncode, 0, f'{stdout} {stderr}')
                finally:
                    if process.poll() is None:
                        stop.write_text('stop')
                        try:
                            process.communicate(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.communicate()


if __name__ == '__main__':
    unittest.main()
