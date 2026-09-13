import struct

client_path = r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll"

with open(client_path, "rb") as f:
    data = f.read()

for field in [b"m_nUserID", b"m_steamID", b"m_hPawn", b"m_hPlayerPawn", b"m_sSanitizedPlayerName", b"m_iPawnHealth", b"m_iPawnArmor"]:
    pos = 0
    found = []
    while True:
        idx = data.find(field + b"\x00", pos)
        if idx == -1:
            break
        found.append(hex(idx))
        pos = idx + len(field) + 1
    print(f"Field '{field.decode()}': {found}")
