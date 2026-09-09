$code = @"
using System;
using System.IO;

public class DisasmCallerFunc {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        
        int callSite = 0x7D5CB9;
        int funcStart = callSite;
        while (funcStart > callSite - 0x1000) {
            if ((b[funcStart-1] == 0xCC || b[funcStart-1] == 0xC3)) {
                if ((b[funcStart] == 0x48 && b[funcStart+1] == 0x89) ||
                    (b[funcStart] == 0x40 && b[funcStart+1] == 0x55) ||
                    (b[funcStart] == 0x40 && b[funcStart+1] == 0x53) ||
                    (b[funcStart] == 0x48 && b[funcStart+1] == 0x83 && b[funcStart+2] == 0xEC)) {
                    break;
                }
            }
            funcStart--;
        }
        Console.WriteLine("SetupAllModules func start: 0x{0:X} (signature bytes: {1:X2} {2:X2} {3:X2} {4:X2} {5:X2})",
            funcStart, b[funcStart], b[funcStart+1], b[funcStart+2], b[funcStart+3], b[funcStart+4]);
    }
}
"@
Add-Type -TypeDefinition $code
[DisasmCallerFunc]::Run()
