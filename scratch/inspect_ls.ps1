$code = @"
using System;
using System.IO;

public class InspectLevelShutdown {
    public static void Main() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);

        int e_lfanew = BitConverter.ToInt32(b, 0x3C);
        ushort numSections = BitConverter.ToUInt16(b, e_lfanew + 6);
        ushort optHdrSize = BitConverter.ToUInt16(b, e_lfanew + 20);
        int secHdrStart = e_lfanew + 24 + optHdrSize;
        long imageBase = BitConverter.ToInt64(b, e_lfanew + 0x30);

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

        int targetRva = 0xB535C0;
        int fo = RvaToOffset(targetRva);
        Console.WriteLine("Bytes of 0xB535C0 (+0xC0):");
        for (int i = 0; i < 48; i += 16) {
            Console.Write("0x" + (targetRva + i).ToString("X8") + ": ");
            for (int j = 0; j < 16; j++) {
                Console.Write(b[fo + i + j].ToString("X2") + " ");
            }
            Console.WriteLine();
        }

        // Check strings
        for (int p = fo; p < fo + 0x100; p++) {
            if (b[p] == 0x48 && b[p+1] == 0x8D && (b[p+2] == 0x15 || b[p+2] == 0x0D)) {
                int disp = BitConverter.ToInt32(b, p + 3);
                // RIP relative
                int curRva = targetRva + (p - fo) + 7;
                int strTargetRva = curRva + disp;
                int targetFo = RvaToOffset(strTargetRva);
                if (targetFo >= 0 && targetFo < b.Length - 10) {
                    string s = "";
                    for (int c = targetFo; c < targetFo + 100 && c < b.Length; c++) {
                        if (b[c] == 0) break;
                        if (b[c] >= 32 && b[c] <= 126) s += (char)b[c];
                    }
                    Console.WriteLine("  String at RVA 0x" + strTargetRva.ToString("X") + ": " + s);
                }
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[InspectLevelShutdown]::Main()
