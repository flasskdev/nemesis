$code = @"
using System;
using System.IO;

public class Disasm {
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

        Action<int, int, string> DumpHex = (rva, len, name) => {
            Console.WriteLine("=== " + name + " (RVA 0x" + rva.ToString("X") + ") ===");
            int fo = RvaToOffset(rva);
            for (int i = 0; i < len; i += 16) {
                int chunk = Math.Min(16, len - i);
                Console.Write("0x" + (rva + i).ToString("X8") + ": ");
                for (int j = 0; j < chunk; j++) {
                    Console.Write(b[fo + i + j].ToString("X2") + " ");
                }
                Console.WriteLine();
            }
        };

        DumpHex(0x917C60, 0x60, "Crash Fn 0x917C60");
        DumpHex(0x8026C0, 0x50, "Frame 0 0x8026C0");
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[Disasm]::Main()
