$code = @"
using System;
using System.IO;

public class FindSource2Client {
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

        byte[] pat = System.Text.Encoding.ASCII.GetBytes("Source2Client002\0");
        int strRva = -1;
        for (int i = 0; i <= b.Length - pat.Length; i++) {
            bool m = true;
            for (int j = 0; j < pat.Length; j++) {
                if (b[i + j] != pat[j]) { m = false; break; }
            }
            if (m) {
                strRva = OffsetToRva(i);
                Console.WriteLine("Source2Client002 string at RVA: 0x" + strRva.ToString("X"));
                break;
            }
        }

        if (strRva == -1) return;

        // Find pointer to strRva (in CreateInterface linked list or table)
        long strVa = imageBase + strRva;
        byte[] strVaBytes = BitConverter.GetBytes(strVa);
        for (int i = 0; i <= b.Length - 8; i += 8) {
            bool m = true;
            for (int j = 0; j < 8; j++) {
                if (b[i + j] != strVaBytes[j]) { m = false; break; }
            }
            if (m) {
                int refRva = OffsetToRva(i);
                Console.WriteLine("String referenced at RVA: 0x" + refRva.ToString("X"));
                // In CreateInterface reg: fnPtr is at i - 8
                long fnPtr = BitConverter.ToInt64(b, i - 8);
                int fnRva = (int)(fnPtr - imageBase);
                Console.WriteLine("CreateInterface fn at RVA: 0x" + fnRva.ToString("X"));
                int fnFo = RvaToOffset(fnRva);
                Console.WriteLine("CreateInterface bytes: " + BitConverter.ToString(b, fnFo, 16));
                
                // Usually `lea rax, [rip + ...]; ret` (48 8D 05 xx xx xx xx C3)
                if (b[fnFo] == 0x48 && b[fnFo+1] == 0x8D && b[fnFo+2] == 0x05) {
                    int disp = BitConverter.ToInt32(b, fnFo + 3);
                    int instRva = fnRva + 7 + disp;
                    Console.WriteLine("Instance at RVA: 0x" + instRva.ToString("X"));
                    int instFo = RvaToOffset(instRva);
                    long vtableVa = BitConverter.ToInt64(b, instFo);
                    int vtableRva = (int)(vtableVa - imageBase);
                    Console.WriteLine("VTable at RVA: 0x" + vtableRva.ToString("X"));
                    
                    int vtFo = RvaToOffset(vtableRva);
                    for (int k = 0; k < 40; k++) {
                        long vfVa = BitConverter.ToInt64(b, vtFo + k * 8);
                        int vfRva = (int)(vfVa - imageBase);
                        int vfFo = RvaToOffset(vfRva);
                        string byteStr = (vfFo >= 0 && vfFo < b.Length - 8) ? BitConverter.ToString(b, vfFo, 8) : "";
                        Console.WriteLine("  vfunc[" + k + "] = 0x" + vfRva.ToString("X") + " (" + byteStr + ")");
                    }
                }
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[FindSource2Client]::Main()
