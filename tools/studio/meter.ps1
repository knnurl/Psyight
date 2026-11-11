# Reads a capture device for a few seconds (WASAPI shared mode) and prints peak and RMS per
# channel. Nothing is played or saved.
#   powershell -ExecutionPolicy Bypass -File tools\studio\meter.ps1 "<device name part>" [seconds]
# (docs/STUDIO_CHECKLIST.md)
param([string]$Name = "DJM-A9", [double]$Seconds = 3)
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom {}
[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator {
    int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
}
[Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceCollection {
    int GetCount(out int count);
    int Item(int index, out IMMDevice device);
}
[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice {
    int Activate(ref Guid iid, int ctx, IntPtr p, [MarshalAs(UnmanagedType.IUnknown)] out object o);
    int OpenPropertyStore(int access, out IPropertyStore store);
}
[Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IPropertyStore {
    int GetCount(out int c); int GetAt(int i, out PropertyKey k);
    int GetValue(ref PropertyKey k, out PropVariant v);
}
[StructLayout(LayoutKind.Sequential)] struct PropertyKey { public Guid fmtid; public int pid; }
[StructLayout(LayoutKind.Explicit)] struct PropVariant {
    [FieldOffset(0)] public short vt; [FieldOffset(8)] public IntPtr p;
}
[Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioClient {
    int Initialize(int shareMode, int flags, long bufferDuration, long periodicity, IntPtr format, IntPtr session);
    int GetBufferSize(out int frames);
    int GetStreamLatency(out long latency);
    int GetCurrentPadding(out int padding);
    int IsFormatSupported(int shareMode, IntPtr format, out IntPtr closest);
    int GetMixFormat(out IntPtr format);
    int GetDevicePeriod(out long def, out long min);
    int Start(); int Stop(); int Reset();
    int SetEventHandle(IntPtr h);
    int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object o);
}
[Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioCaptureClient {
    int GetBuffer(out IntPtr data, out int frames, out int flags, out long pos, out long qpc);
    int ReleaseBuffer(int frames);
    int GetNextPacketSize(out int frames);
}

public static class Meter {
    public static string Run(string part, double seconds) {
        var en = (IMMDeviceEnumerator)new MMDeviceEnumeratorCom();
        IMMDeviceCollection col; en.EnumAudioEndpoints(1 /*capture*/, 1 /*active*/, out col);
        int n; col.GetCount(out n);
        var nameKey = new PropertyKey { fmtid = new Guid("a45c254e-df1c-4efd-8020-67d146a850e0"), pid = 14 };
        IMMDevice dev = null; string devName = null;
        for (int i = 0; i < n; ++i) {
            IMMDevice d; col.Item(i, out d);
            IPropertyStore ps; d.OpenPropertyStore(0, out ps);
            PropVariant v; ps.GetValue(ref nameKey, out v);
            string s = Marshal.PtrToStringUni(v.p);
            if (s != null && s.Contains(part)) { dev = d; devName = s; break; }
        }
        if (dev == null) return "no capture device matching " + part;
        var iid = new Guid("1CB9AD4C-DBFA-4c32-B178-C2F568A703B2");
        object o; dev.Activate(ref iid, 23, IntPtr.Zero, out o);
        var client = (IAudioClient)o;
        IntPtr fmt; client.GetMixFormat(out fmt);
        int channels = Marshal.ReadInt16(fmt, 2);
        int rate = Marshal.ReadInt32(fmt, 4);
        int bits = Marshal.ReadInt16(fmt, 14);
        int hr = client.Initialize(0, 0, 2000000, 0, fmt, IntPtr.Zero);
        if (hr != 0) return "Initialize failed 0x" + hr.ToString("X");
        var ciid = new Guid("C8ADBD64-E71E-48a0-A4DE-185C395CD317");
        object co; client.GetService(ref ciid, out co);
        var cap = (IAudioCaptureClient)co;
        var peak = new double[channels]; var sum = new double[channels]; long frames = 0;
        long silentFrames = 0;
        client.Start();
        var until = DateTime.Now.AddSeconds(seconds);
        while (DateTime.Now < until) {
            System.Threading.Thread.Sleep(10);
            int packet; cap.GetNextPacketSize(out packet);
            while (packet > 0) {
                IntPtr data; int count, flags; long pos, qpc;
                cap.GetBuffer(out data, out count, out flags, out pos, out qpc);
                if ((flags & 2) != 0) { silentFrames += count; }
                else if (bits == 32) {
                    var buf = new float[count * channels];
                    Marshal.Copy(data, buf, 0, buf.Length);
                    for (int f = 0; f < count; ++f)
                        for (int c = 0; c < channels; ++c) {
                            double x = buf[f * channels + c];
                            peak[c] = Math.Max(peak[c], Math.Abs(x)); sum[c] += x * x;
                        }
                }
                frames += count;
                cap.ReleaseBuffer(count);
                cap.GetNextPacketSize(out packet);
            }
        }
        client.Stop();
        var r = string.Format("{0}: {1} ch, {2} Hz, {3}-bit mix format; {4:F1} s read, {5} frames flagged silent\n",
                              devName, channels, rate, bits, frames / (double)rate, silentFrames);
        for (int c = 0; c < channels; ++c) {
            double rms = frames > 0 ? Math.Sqrt(sum[c] / frames) : 0;
            r += string.Format("  channel {0}: peak {1:F1} dBFS, RMS {2:F1} dBFS\n", c + 1,
                               20 * Math.Log10(Math.Max(peak[c], 1e-9)), 20 * Math.Log10(Math.Max(rms, 1e-9)));
        }
        return r;
    }
}
'@
[Meter]::Run($Name, $Seconds)
