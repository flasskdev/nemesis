$code = @"
using System;
using System.IO;

public class TestSig {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        
        // Pattern: 48 89 5C 24 08 57 48 83 EC 30 48 8B D9
        byte[] pat = new byte[] { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9 };
        int count = 0;
        for (int i = 0; i < b.Length - pat.Length; i++) {
            bool m = true;
            for (int j = 0; j < pat.Length; j++) {
                if (b[i+j] != pat[j]) { m = false; break; }
            }
            if (m) {
                count++;
                Console.WriteLine("Match at 0x{0:X}", i);
            }
        }
        Console.WriteLine("Total matches: {0}", count);
    }
}
"@
Add-Type -TypeDefinition $code
[TestSig]::Run()
