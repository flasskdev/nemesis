$code = @"
using System;
using System.IO;

public class PrintBytes {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        
        int start = 0x7D5B90;
        for (int i = start; i < start + 64; i++) {
            Console.Write("{0:X2} ", b[i]);
        }
        Console.WriteLine();
    }
}
"@
Add-Type -TypeDefinition $code
[PrintBytes]::Run()
