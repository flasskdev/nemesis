$code = @"
using System;
using System.IO;

public class FindCalls {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        int target = 0x7D80F0;
        
        for (int i = 0; i < b.Length - 5; i++) {
            if (b[i] == 0xE8) {
                int rel = BitConverter.ToInt32(b, i + 1);
                if (i + 5 + rel == target) {
                    Console.WriteLine("Direct CALL to 0x7D80F0 at 0x{0:X}", i);
                }
            } else if (b[i] == 0xE9) {
                int rel = BitConverter.ToInt32(b, i + 1);
                if (i + 5 + rel == target) {
                    Console.WriteLine("Direct JMP to 0x7D80F0 at 0x{0:X}", i);
                }
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code
[FindCalls]::Run()
