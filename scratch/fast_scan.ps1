$code = @"
using System;
using System.IO;
using System.Text;

public class Scanner {
    public static void Scan() {
        string path = @"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll";
        byte[] b = File.ReadAllBytes(path);
        byte[] target = Encoding.ASCII.GetBytes("weapons/models/shared/stattrak/stattrak_module.vmdl");
        int strIdx = -1;
        for (int i = 0; i < b.Length - target.Length; i++) {
            bool m = true;
            for (int j = 0; j < target.Length; j++) {
                if (b[i+j] != target[j]) { m = false; break; }
            }
            if (m) { strIdx = i; break; }
        }
        Console.WriteLine("String found at 0x{0:X}", strIdx);
        if (strIdx < 0) return;

        // Find LEA references (48 8D ...)
        for (int i = 0; i < b.Length - 7; i++) {
            if (b[i] == 0x48 && b[i+1] == 0x8D) {
                int disp = BitConverter.ToInt32(b, i + 3);
                if (i + 7 + disp == strIdx) {
                    Console.WriteLine("Found xref at 0x{0:X}", i);
                    // print 100 bytes around
                    int start = Math.Max(0, i - 40);
                    int end = Math.Min(b.Length, i + 60);
                    for (int k = start; k < end; k++) {
                        Console.Write("{0:X2} ", b[k]);
                    }
                    Console.WriteLine();
                }
            }
        }
    }
}
"@
Add-Type -TypeDefinition $code
[Scanner]::Scan()
