import struct

client_path = r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"

with open(client_path, "rb") as f:
    data = f.read()

e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
opt_hdr_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
image_base = struct.unpack_from("<Q", data, e_lfanew + 24 + 24)[0]
num_sections = struct.unpack_from("<H", data, e_lfanew + 6)[0]
sections_offset = e_lfanew + 24 + opt_hdr_size

sections = []
for i in range(num_sections):
    sec_data = data[sections_offset + i*40 : sections_offset + (i+1)*40]
    name = sec_data[:8].rstrip(b'\x00').decode('latin1', 'ignore')
    vsize, va, rsize, roff = struct.unpack_from("<IIII", sec_data, 8)
    sections.append((name, va, vsize, roff, rsize))

def offset_to_rva(off):
    for name, va, vsize, roff, rsize in sections:
        if roff <= off < roff + rsize:
            return off - roff + va
    return None

# Find "attacker" and "userid"
attacker_off = 0x19F3558
userid_off = 0x19B3F4C

attacker_rva = offset_to_rva(attacker_off)
userid_rva = offset_to_rva(userid_off)

print(f"attacker RVA: 0x{attacker_rva:X}, userid RVA: 0x{userid_rva:X}")

for sname, sva, svsize, sroff, srsize in sections:
    if sname == ".text":
        sec_bytes = data[sroff:sroff+srsize]
        for i in range(len(sec_bytes) - 7):
            if sec_bytes[i] in (0x48, 0x4C) and sec_bytes[i+1] == 0x8D:
                rel = struct.unpack_from("<i", sec_bytes, i+3)[0]
                curr_rva = sva + i
                target_rva = curr_rva + 7 + rel
                if target_rva in (attacker_rva, userid_rva):
                    tag = "attacker" if target_rva == attacker_rva else "userid"
                    print(f"Ref to {tag} at RVA 0x{curr_rva:X}")
