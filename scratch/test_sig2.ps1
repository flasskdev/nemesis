$code = @"
using System;
using System.IO;

public class TestSig2 {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        
        // 48 89 5C 24 08 57 48 83 EC 30 48 8B D9 E8 ?? ?? ?? ?? BA 01 00 00 00 48 8B CB
        byte[] pat = new byte[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9 };
        int count = 0;
        for (int i = 0; i < b.Length - 25; i++) {
            bool m = true;
            for (int j = 0; j < pat.Length; j++) {
                if (b[i+j] != pat[j]) { m = false; break; }
            }
            if (m && b[i+13] == 0xE8 && b[i+18] == 0xBA && b[i+19] == 0x01 && b[i+20] == 0x00 && b[i+21] == 0x00 && b[i+22] == 0x00 && b[i+23] == 0x48 && b[i+24] == 0x8B && b[i+25] == 0xCB) {
                count++;
                Console.WriteLine("Unique Match at 0x{0:X}", i);
            }
        }
        Console.WriteLine("Total matches: {0}", count);
    }
}
"@
Add-Type -TypeDefinition $code
[TestSig2]::Run()
