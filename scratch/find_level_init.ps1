$code = @"
using System;
using System.IO;

public class FindLevelInit {
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

        for (int i = 0; i < b.Length - 15; i++) {
            if (b[i] == 0x48 && b[i+1] == 0x8D && b[i+2] == 0x05 &&
                b[i+7] == 0xC6 && b[i+8] == 0x41 && b[i+9] == 0x10 && b[i+10] == 0x00) {
                int rva = OffsetToRva(i);
                int disp = BitConverter.ToInt32(b, i + 3);
                int targetRva = rva + 7 + disp;
                Console.WriteLine("Matched pattern at RVA 0x" + rva.ToString("X"));
                Console.WriteLine("Target RVA (vtable): 0x" + targetRva.ToString("X"));
                int vtFo = RvaToOffset(targetRva);
                Console.WriteLine("VTable file offset: 0x" + vtFo.ToString("X"));
                
                for (int k = 20; k <= 30; k++) {
                    long vfVa = BitConverter.ToInt64(b, vtFo + k * 8);
                    int vfRva = (int)(vfVa - imageBase);
                    int vfFo = RvaToOffset(vfRva);
                    string bytes = (vfFo >= 0 && vfFo < b.Length - 16) ? BitConverter.ToString(b, vfFo, 16) : "";
                    Console.WriteLine("  [" + k + "] (0x" + (k*8).ToString("X") + ") = RVA 0x" + vfRva.ToString("X") + " : " + bytes);
                }
                break;
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[FindLevelInit]::Main()
