$code = @"
using System;
using System.IO;

public class Disassembler {
    public static void Main() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);

        Console.WriteLine("=== Instructions around 0x802630 (RSP+0 caller) ===");
        PrintBytes(b, 0x802630, 0xC0);

        Console.WriteLine("\n=== Instructions around 0x917C50 (Crash RIP 0x917C60) ===");
        PrintBytes(b, 0x917C50, 0x60);
    }

    static void PrintBytes(byte[] b, int offset, int len) {
        for (int i = 0; i < len; i += 16) {
            int chunk = Math.Min(16, len - i);
            Console.Write("0x" + (offset + i).ToString("X8") + ": ");
            for (int j = 0; j < chunk; j++) {
                Console.Write(b[offset + i + j].ToString("X2") + " ");
            }
            Console.WriteLine();
        }
    }
}
"@
Add-Type -TypeDefinition $code -Language CSharp
[Disassembler]::Main()
