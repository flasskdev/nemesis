$code = @"
using System;
using System.IO;

public class Disasm9DCAB0 {
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

        int targetRva = 0x9DCAB0;
        int fo = RvaToOffset(targetRva);
        Console.WriteLine("Bytes at 0x9DCAB0:");
        for (int i = 0; i < 64; i += 16) {
            Console.Write("0x" + (targetRva + i).ToString("X8") + ": ");
            for (int j = 0; j < 16; j++) {
                Console.Write(b[fo + i + j].ToString("X2") + " ");
            }
            Console.WriteLine();
        }
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[Disasm9DCAB0]::Main()
