$code = @"
using System;
using System.IO;

public class DisasmCaller {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        
        int callSite = 0x7D5CB9;
        int start = callSite - 64;
        int end = callSite + 64;
        for (int i = start; i < end; i++) {
            if (i == callSite) Console.Write("[CALL] ");
            Console.Write("{0:X2} ", b[i]);
            if ((i - start + 1) % 32 == 0) Console.WriteLine();
        }
        Console.WriteLine();
    }
}
"@
Add-Type -TypeDefinition $code
[DisasmCaller]::Run()
