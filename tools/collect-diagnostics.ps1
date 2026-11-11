param([string]$Root = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$destination = Join-Path ([IO.Path]::GetFullPath($Root)) 'private/diagnostics'
[IO.Directory]::CreateDirectory($destination) | Out-Null
try {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class UuDisplayRead {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct Device {
        public int Size;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string Name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Description;
        public uint Flags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Id;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string Key;
    }
    [StructLayout(LayoutKind.Explicit, Size=220, CharSet=CharSet.Unicode)]
    public struct Mode {
        [FieldOffset(68)] public ushort Size;
        [FieldOffset(76)] public int X;
        [FieldOffset(80)] public int Y;
        [FieldOffset(172)] public uint Width;
        [FieldOffset(176)] public uint Height;
        [FieldOffset(184)] public uint Frequency;
    }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern bool EnumDisplayDevices(string name, uint i, ref Device device, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    public static extern bool EnumDisplaySettings(string name, int number, ref Mode mode);
}
'@
    $displays = @(for ($i = 0; ; $i++) {
        $device = New-Object UuDisplayRead+Device
        $device.Size = [Runtime.InteropServices.Marshal]::SizeOf($device)
        if (![UuDisplayRead]::EnumDisplayDevices($null, $i, [ref]$device, 0)) { break }
        $current = New-Object UuDisplayRead+Mode
        $current.Size = 220
        $saved = New-Object UuDisplayRead+Mode
        $saved.Size = 220
        $hasCurrent = [UuDisplayRead]::EnumDisplaySettings($device.Name, -1, [ref]$current)
        $hasSaved = [UuDisplayRead]::EnumDisplaySettings($device.Name, -2, [ref]$saved)
        [pscustomobject]@{ Name=$device.Name; Description=$device.Description; Flags=$device.Flags;
            Active=$hasCurrent; Width=$current.Width; Height=$current.Height; X=$current.X; Y=$current.Y;
            Saved=$hasSaved; SavedWidth=$saved.Width; SavedHeight=$saved.Height }
    })
    $hosts = @(Get-Process GameViewerServer -ErrorAction SilentlyContinue | ForEach-Object {
        [pscustomobject]@{ Id=$_.Id; Started=$_.StartTime; Path=$_.Path;
            PatchModules=@($_.Modules | Where-Object { $_.FileName -like '*UU-Patch*' } | Select-Object -ExpandProperty FileName) }
    })
    @{ Collected=(Get-Date -Format o); Displays=$displays; Hosts=$hosts } |
        ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destination 'state.json') -Encoding utf8
    $dumpFolder = Join-Path $env:SystemRoot 'System32/config/systemprofile/AppData/Local/CrashDumps'
    Get-ChildItem -LiteralPath $dumpFolder -Filter 'GameViewerServer.exe.*.dmp' -ErrorAction SilentlyContinue |
        Where-Object LastWriteTime -GT (Get-Date).AddHours(-4) | ForEach-Object {
            $target = Join-Path $destination $_.Name
            if (!(Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath $_.FullName -Destination $target }
        }
    'Read-only diagnostic collection completed.' | Set-Content -LiteralPath (Join-Path $destination 'result.txt')
} catch {
    $_.Exception.Message | Set-Content -LiteralPath (Join-Path $destination 'result.txt')
    exit 1
}
