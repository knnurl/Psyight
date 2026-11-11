# Listens to a MIDI input (winmm) for a few seconds and summarises what arrived: per channel and
# control (or note), the count and the value range. Receives only.
#   powershell -ExecutionPolicy Bypass -File tools\studio\midilisten.ps1 "<device name part>" [seconds]
# (docs/STUDIO_CHECKLIST.md)
param([string]$Name = "DJM-A9", [double]$Seconds = 20)
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

public static class MidiListen {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct MIDIINCAPS { public ushort wMid, wPid; public uint vDriverVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szPname; public uint dwSupport; }
    delegate void MidiInProc(IntPtr h, uint msg, IntPtr inst, IntPtr p1, IntPtr p2);
    [DllImport("winmm.dll")] static extern uint midiInGetNumDevs();
    [DllImport("winmm.dll", CharSet = CharSet.Unicode)] static extern uint midiInGetDevCaps(UIntPtr id, out MIDIINCAPS caps, uint size);
    [DllImport("winmm.dll")] static extern uint midiInOpen(out IntPtr h, uint id, MidiInProc cb, IntPtr inst, uint flags);
    [DllImport("winmm.dll")] static extern uint midiInStart(IntPtr h);
    [DllImport("winmm.dll")] static extern uint midiInStop(IntPtr h);
    [DllImport("winmm.dll")] static extern uint midiInClose(IntPtr h);
    const uint MIM_DATA = 0x3C3;
    static readonly object Gate = new object();
    static readonly Dictionary<string, int[]> Seen = new Dictionary<string, int[]>(); // count, min, max, last
    static readonly List<string> Order = new List<string>();
    static int total;
    static MidiInProc keep;

    static void OnMidi(IntPtr h, uint msg, IntPtr inst, IntPtr p1, IntPtr p2) {
        if (msg != MIM_DATA) return;
        uint d = (uint)p1.ToInt64();
        int status = (int)(d & 0xFF), a = (int)((d >> 8) & 0x7F), b = (int)((d >> 16) & 0x7F);
        int kind = status & 0xF0, ch = (status & 0x0F) + 1;
        string key;
        if (kind == 0xB0) key = "ch" + ch + " CC " + a;
        else if (kind == 0x90 || kind == 0x80) key = "ch" + ch + " note " + a;
        else if (kind == 0xE0) { key = "ch" + ch + " pitch bend"; b = (b << 7) | a; }
        else key = "status 0x" + status.ToString("X2");
        lock (Gate) {
            ++total;
            int[] s;
            if (!Seen.TryGetValue(key, out s)) { s = new[] { 0, int.MaxValue, int.MinValue, 0 }; Seen[key] = s; Order.Add(key); }
            s[0]++; s[1] = Math.Min(s[1], b); s[2] = Math.Max(s[2], b); s[3] = b;
        }
    }

    public static string Run(string part, double seconds) {
        uint n = midiInGetNumDevs();
        var sb = new StringBuilder();
        int found = -1;
        for (uint i = 0; i < n; ++i) {
            MIDIINCAPS caps; midiInGetDevCaps((UIntPtr)i, out caps, (uint)Marshal.SizeOf(typeof(MIDIINCAPS)));
            sb.AppendLine("MIDI input " + i + ": " + caps.szPname);
            if (found < 0 && caps.szPname.Contains(part)) found = (int)i;
        }
        if (found < 0) return sb + "no MIDI input matching " + part;
        keep = OnMidi;
        IntPtr h;
        uint r = midiInOpen(out h, (uint)found, keep, IntPtr.Zero, 0x30000 /*CALLBACK_FUNCTION*/);
        if (r != 0) return sb + "midiInOpen failed: " + r + " (another program may hold the port)";
        midiInStart(h);
        System.Threading.Thread.Sleep((int)(seconds * 1000));
        midiInStop(h); midiInClose(h);
        lock (Gate) {
            sb.AppendLine("listened " + seconds + " s on input " + found + ": " + total + " messages");
            foreach (var k in Order) {
                var s = Seen[k];
                sb.AppendLine(string.Format("  {0}: {1} messages, values {2} to {3}, last {4}", k, s[0], s[1], s[2], s[3]));
            }
        }
        return sb.ToString();
    }
}
'@
[MidiListen]::Run($Name, $Seconds)
