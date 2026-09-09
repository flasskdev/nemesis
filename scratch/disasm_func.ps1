$code = @"
using System;
using System.IO;

public class Disasm {
    public static void Run() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        
        // Find function prologue before 0x7D8383
        int xref = 0x7D8383;
        int funcStart = xref;
        while (funcStart > xref - 0x1000) {
            // Check for common 64-bit prologues: 48 89 5C 24, 40 55, 48 83 EC, 55 48 8D EC, 48 81 EC
            if ((b[funcStart] == 0x48 && b[funcStart+1] == 0x89 && b[funcStart+2] == 0x5C && b[funcStart+3] == 0x24) ||
                (b[funcStart] == 0x40 && b[funcStart+1] == 0x55 && b[funcStart+2] == 0x53) ||
                (b[funcStart] == 0x40 && b[funcStart+1] == 0x53 && b[funcStart+2] == 0x48 && b[funcStart+3] == 0x83) ||
                (b[funcStart] == 0x48 && b[funcStart+1] == 0x8B && b[funcStart+2] == 0xC4) ||
                (b[funcStart] == 0x40 && b[funcStart+1] == 0x57 && b[funcStart+2] == 0x48 && b[funcStart+3] == 0x83)) {
                // Check if preceded by CC or C3
                if (funcStart > 0 && (b[funcStart-1] == 0xCC || b[funcStart-1] == 0xC3)) {
                    break;
                }
            }
            funcStart--;
        }

        Console.WriteLine("Function start at 0x{0:X} (offset from start: +0x{1:X})", funcStart, xref - funcStart);
        
        // Print bytes from funcStart up to xref + 0x100
        for (int i = funcStart; i < xref + 0x100; i++) {
            Console.Write("{0:X2} ", b[i]);
            if ((i - funcStart + 1) % 32 == 0) Console.WriteLine();
        }
        Console.WriteLine();
    }
}
"@
Add-Type -TypeDefinition $code
[Disasm]::Run()
