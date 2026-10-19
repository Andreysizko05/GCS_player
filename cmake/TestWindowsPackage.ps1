[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,

    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:OS -ne 'Windows_NT') {
    throw 'This package test must run on Windows.'
}

$archive = Get-Item -LiteralPath $PackagePath
$testParent = Join-Path $archive.DirectoryName 'package-tests'
# Exercise relocation, spaces, and Unicode without depending on script encoding.
$testRoot = Join-Path $testParent ("package test $([char]0x0416) " + [guid]::NewGuid().ToString('N'))
$unpackDirectory = Join-Path $testRoot 'unpacked'
$workingDirectory = Join-Path $testRoot 'unrelated working directory'
New-Item -ItemType Directory -Path $unpackDirectory, $workingDirectory -Force | Out-Null

function Invoke-PackageProcess {
    param(
        [string]$Name,
        [string]$FilePath,
        [string]$Arguments,
        [hashtable]$Environment
    )

    Write-Host "Testing $Name"
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = $Arguments
    $startInfo.WorkingDirectory = $workingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.WindowStyle = [System.Diagnostics.ProcessWindowStyle]::Hidden
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    foreach ($variableName in @($startInfo.EnvironmentVariables.Keys)) {
        if ($variableName -match '^(GST|GSTREAMER|QT_|GIO_)') {
            $startInfo.EnvironmentVariables.Remove($variableName)
        }
    }
    foreach ($entry in $Environment.GetEnumerator()) {
        $startInfo.EnvironmentVariables[$entry.Key] = [string]$entry.Value
    }

    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw "Unable to start $Name."
        }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "$Name exceeded the $TimeoutSeconds second timeout."
        }
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        [System.IO.File]::WriteAllText((Join-Path $testRoot "$Name.log"), $output)
        if ($process.ExitCode -ne 0) {
            throw "$Name exited with code $($process.ExitCode).`n$output"
        }
    } finally {
        $process.Dispose()
    }
}

# A missing imported DLL fails before main(), where the application's own
# SetErrorMode runs. Children inherit this mode so CI never waits for a dialog.
if (-not ('GcsPackageTest.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
namespace GcsPackageTest {
    public static class NativeMethods {
        [DllImport("kernel32.dll")]
        public static extern uint SetErrorMode(uint mode);
    }
}
'@
}
$previousErrorMode = [GcsPackageTest.NativeMethods]::SetErrorMode(0x8003)
$succeeded = $false
try {
    Write-Host "Extracting $($archive.FullName)"
    Expand-Archive -LiteralPath $archive.FullName -DestinationPath $unpackDirectory
    $executables = @(Get-ChildItem -LiteralPath $unpackDirectory -Filter GCS_player.exe -File -Recurse)
    if ($executables.Count -ne 1) {
        throw "Expected one packaged GCS_player.exe, found $($executables.Count)."
    }
    $application = $executables[0]
    $binDirectory = $application.DirectoryName
    $runtimeDirectory = Join-Path $binDirectory 'runtime'
    $toolsDirectory = Join-Path $binDirectory 'gstreamer-tools'
    if (@(Get-ChildItem -LiteralPath $binDirectory -Filter '*.dll' -File).Count -ne 0) {
        throw 'Runtime DLLs must be in subdirectories, not beside the launcher.'
    }
    if (-not (Test-Path -LiteralPath (Join-Path $runtimeDirectory 'GCS_player_app.dll') -PathType Leaf)) {
        throw 'The package is missing runtime/GCS_player_app.dll.'
    }
    # The helper tools carry the selected GStreamer SDK. Qt deployment must not
    # leave different GLib/GStreamer DLL versions in the application's runtime.
    $runtimeDlls = @(Get-ChildItem -LiteralPath $toolsDirectory -Filter '*.dll' -File)
    if ($runtimeDlls.Count -eq 0) {
        throw 'The package has no bundled GStreamer runtime DLLs.'
    }
    foreach ($dll in $runtimeDlls) {
        $deployedDll = Join-Path $runtimeDirectory $dll.Name
        if (-not (Test-Path -LiteralPath $deployedDll -PathType Leaf)) {
            throw "GStreamer runtime DLL is missing from runtime/: $($dll.Name)"
        }
        if ((Get-FileHash -LiteralPath $dll.FullName).Hash -ne (Get-FileHash -LiteralPath $deployedDll).Hash) {
            throw "GStreamer DLL in runtime/ differs from the selected SDK: $($dll.Name)"
        }
    }
    Write-Host "Verified $($runtimeDlls.Count) matching GStreamer DLLs in runtime/ and no DLLs beside the launcher."

    $systemPath = (Join-Path $env:SystemRoot 'System32') + ';' + $env:SystemRoot
    $environment = @{
        PATH = $systemPath
        QT_QPA_PLATFORM = 'windows'
        GST_REGISTRY = (Join-Path $testRoot 'clean-registry.bin')
    }
    Invoke-PackageProcess -Name 'clean-startup' -FilePath $application.FullName `
        -Arguments '--smoke-test --smoke-test-ms 250' -Environment $environment

    $scanners = @(Get-ChildItem -LiteralPath $toolsDirectory -Filter 'gst-plugin-scanner.exe' -File -Recurse)
    try {
        foreach ($scanner in $scanners) {
            Rename-Item -LiteralPath $scanner.FullName -NewName ($scanner.Name + '.disabled')
        }
        $environment.GST_REGISTRY = Join-Path $testRoot 'no-scanner-registry.bin'
        Invoke-PackageProcess -Name 'startup-without-scanner' -FilePath $application.FullName `
            -Arguments '--smoke-test --smoke-test-ms 250' -Environment $environment
    } finally {
        foreach ($scanner in $scanners) {
            $disabledScanner = $scanner.FullName + '.disabled'
            if (Test-Path -LiteralPath $disabledScanner) {
                Rename-Item -LiteralPath $disabledScanner -NewName $scanner.Name
            }
        }
    }

    $foreignDirectory = Join-Path $testRoot 'foreign GStreamer'
    New-Item -ItemType Directory -Path $foreignDirectory | Out-Null
    # Invalid external DLLs must never be selected ahead of the packaged runtime.
    foreach ($dll in $runtimeDlls) {
        [System.IO.File]::WriteAllText((Join-Path $foreignDirectory $dll.Name), 'foreign runtime')
    }
    $environment.PATH = "$foreignDirectory;$systemPath"
    $environment.GST_REGISTRY = Join-Path $testRoot 'foreign-registry.bin'
    foreach ($name in @('GST_PLUGIN_PATH', 'GST_PLUGIN_PATH_1_0', 'GST_PLUGIN_SYSTEM_PATH', 'GST_PLUGIN_SYSTEM_PATH_1_0')) {
        $environment[$name] = $foreignDirectory
    }
    $environment.GST_PLUGIN_SCANNER = Join-Path $foreignDirectory 'gst-plugin-scanner.exe'
    $environment.GST_PLUGIN_SCANNER_1_0 = $environment.GST_PLUGIN_SCANNER
    $environment.GSTREAMER_1_0_ROOT_MSVC_X86_64 = $foreignDirectory
    Invoke-PackageProcess -Name 'foreign-environment-startup' -FilePath $application.FullName `
        -Arguments '--smoke-test --smoke-test-ms 250' -Environment $environment

    # Standalone tools do not have the application's environment preparation.
    # Use only packaged plugins and a fresh private registry; scan in process.
    $pluginDirectory = Join-Path $binDirectory 'gstreamer-1.0'
    $environment = @{
        PATH = "$runtimeDirectory;$systemPath"
        GST_PLUGIN_PATH = $pluginDirectory
        GST_PLUGIN_PATH_1_0 = $pluginDirectory
        GST_PLUGIN_SYSTEM_PATH = $pluginDirectory
        GST_PLUGIN_SYSTEM_PATH_1_0 = $pluginDirectory
        GST_REGISTRY = (Join-Path $testRoot 'tools-registry.bin')
        GST_REGISTRY_FORK = 'no'
        GST_REGISTRY_REUSE_PLUGIN_SCANNER = 'no'
        GIO_EXTRA_MODULES = (Join-Path $binDirectory 'gio/modules')
    }
    $inspect = Join-Path $binDirectory 'gstreamer-tools/gst-inspect-1.0.exe'
    foreach ($element in @('appsink', 'rtspsrc', 'udpsrc', 'rtph264depay', 'rtph265depay', 'h264parse', 'h265parse', 'avdec_h264', 'avdec_h265', 'tsdemux')) {
        Invoke-PackageProcess -Name "plugin-$element" -FilePath $inspect `
            -Arguments $element -Environment $environment
    }
    $launch = Join-Path $binDirectory 'gstreamer-tools/gst-launch-1.0.exe'
    Invoke-PackageProcess -Name 'h264-pipeline' -FilePath $launch -Environment $environment `
        -Arguments '-q videotestsrc num-buffers=3 ! videoconvert ! x264enc tune=zerolatency ! h264parse ! avdec_h264 ! fakesink'

    $succeeded = $true
    Write-Host 'Windows package passed isolated startup, DLL consistency, plugin, and H.264 pipeline checks.'
} finally {
    [GcsPackageTest.NativeMethods]::SetErrorMode($previousErrorMode) | Out-Null
    if ($succeeded) {
        $resolvedParent = (Resolve-Path -LiteralPath $testParent).Path.TrimEnd('\') + '\'
        $resolvedRoot = (Resolve-Path -LiteralPath $testRoot).Path
        if (-not $resolvedRoot.StartsWith($resolvedParent, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove test directory outside $resolvedParent"
        }
        Remove-Item -LiteralPath $resolvedRoot -Recurse -Force
    } else {
        Write-Host "Package test files and logs retained at $testRoot"
    }
}
