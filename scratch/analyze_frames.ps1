$code = @"
using System;
using System.IO;
using System.Text;

public class Analyze {
    public static void Main() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);

        int e_lfanew = BitConverter.ToInt32(b, 0x3C);
        ushort numSections = BitConverter.ToUInt16(b, e_lfanew + 6);
        ushort optHdrSize = BitConverter.ToUInt16(b, e_lfanew + 20);
        int secHdrStart = e_lfanew + 24 + optHdrSize;

        Func<int, int> RvaToOffset = (rva) => {
            for (int i = 0; i < numSections; i++) {
                int secOffset = secHdrStart + i * 40;
                uint virtSize = BitConverter.ToUInt32(b, secOffset + 8);
                uint virtAddr = BitConverter.ToUInt32(b, secOffset + 12);
                uint rawSize  = BitConverter.ToUInt32(b, secOffset + 16);
                uint rawAddr  = BitConverter.ToUInt32(b, secOffset + 20);
                if (rva >= virtAddr && rva < virtAddr + virtSize) {
                    return (int)(rawAddr + (rva - virtAddr));
                }
            }
            return -1;
        };

        Func<int, int> OffsetToRva = (off) => {
            for (int i = 0; i < numSections; i++) {
                int secOffset = secHdrStart + i * 40;
                uint rawSize  = BitConverter.ToUInt32(b, secOffset + 16);
                uint rawAddr  = BitConverter.ToUInt32(b, secOffset + 20);
                uint virtAddr = BitConverter.ToUInt32(b, secOffset + 12);
                if (off >= rawAddr && off < rawAddr + rawSize) {
                    return (int)(virtAddr + (off - rawAddr));
                }
            }
            return -1;
        };

        Action<int, string> ScanStrings = (rva, name) => {
            Console.WriteLine("\n=== Strings for " + name + " (0x" + rva.ToString("X") + ") ===");
            int fo = RvaToOffset(rva);
            if (fo < 0) return;
            for (int p = fo - 0x200; p < fo + 0x400 && p < b.Length - 7; p++) {
                if (b[p] == 0x48 && b[p+1] == 0x8D && (b[p+2] == 0x15 || b[p+2] == 0x0D)) {
                    int disp = BitConverter.ToInt32(b, p + 3);
                    int curRva = OffsetToRva(p + 7);
                    int targetRva = curRva + disp;
                    int targetFo = RvaToOffset(targetRva);
                    if (targetFo >= 0 && targetFo < b.Length - 10) {
                        string s = "";
                        for (int c = targetFo; c < targetFo + 100 && c < b.Length; c++) {
                            if (b[c] == 0) break;
                            if (b[c] >= 32 && b[c] <= 126) s += (char)b[c];
                            else break;
                        }
                        if (s.Length >= 4) {
                            Console.WriteLine("  [0x" + curRva.ToString("X") + " -> 0x" + targetRva.ToString("X") + "]: " + s);
                        }
                    }
                }
            }
        };

        ScanStrings(0x390750, "Frame 4 (0x390750)");
        ScanStrings(0xAA26C0, "Frame 3 (0xAA26C0)");
        ScanStrings(0xA79937, "Frame 2 (0xA79937)");
        ScanStrings(0x9BF8DD, "Frame 1 (0x9BF8DD)");
        ScanStrings(0x8026C0, "Frame 0 (0x8026C0)");
        ScanStrings(0x917C60, "Crash fn (0x917C60)");
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[Analyze]::Main()
