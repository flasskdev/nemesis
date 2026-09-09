$code = @"
using System;
using System.IO;

public class FindCallers2 {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        int target = 0x7D5B90;
        
        for (int i = 0; i < b.Length - 5; i++) {
            if (b[i] == 0xE8 || b[i] == 0xE9) {
                int rel = BitConverter.ToInt32(b, i + 1);
                if (i + 5 + rel == target) {
                    Console.WriteLine("CALL to 0x7D5B90 at 0x{0:X}", i);
                }
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code
[FindCallers2]::Run()
