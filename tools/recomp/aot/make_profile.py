#!/usr/bin/env python3
"""Generate the PSXRecomp AOT overlay profile (schema "psxrecomp AOT methods v1") for
Bust A Groove 2 from the DBI_ inventory of BUST2.BIN.

Overlay loaders in SLUS_011.59 copy a DBI_ payload (12-byte header skipped) to a fixed address:
  FUN_8002A6C0 (modules 0..4, top-level entries 2..6)  -> 0x8008F6C0  (FUN_8001F7E0 installer)
  FUN_8003235C (P1 character, container 87+2k / 159+2k) -> 0x801CC510
  FUN_80032508 (P2 character, container 88+2k / 160+2k) -> 0x801D3B64
  FUN_800326F8 (stage, containers 231..245, entry 0)     -> 0x801DB1B8
Load class per block was confirmed by the absolute pointers each payload contains
(tools/recomp/aot/dbi_inventory.json, field "base").

Usage: python make_profile.py <disc.bin> <dbi_inventory.json> <out overlays.json>
"""
import hashlib, json, sys

disc_bin, inv_path, out_path = sys.argv[1:4]
inv = json.load(open(inv_path))

h = hashlib.sha256()
with open(disc_bin, 'rb') as f:
    for chunk in iter(lambda: f.read(1 << 22), b''):
        h.update(chunk)
disc_sha = h.hexdigest()

classes = {
    'main': ('0x8008F6C0', 'Main modules: FUN_8002A6C0 loads top-level DF2_ entries 2..6 and FUN_8001F7E0 copies the DBI_ payload to 0x8008F6C0'),
    'p1': ('0x801CC510', 'Player 1 character overlay: FUN_8003235C copies the DBI_ payload to 0x801CC510'),
    'p2': ('0x801D3B64', 'Player 2 character overlay: FUN_80032508 copies the DBI_ payload to 0x801D3B64'),
    'stage': ('0x801DB1B8', 'Stage overlay: FUN_800326F8 copies entry 0 of the stage container to 0x801DB1B8'),
}
# Data regions inside overlays that the recompiler would otherwise decode as code (audit fails with
# UNSUPPORTED_INSTRUCTION). Keyed by DF2_ entry path; ranges are 4-byte aligned guest addresses.
EXCLUDED = {
    '3': [
        ('0x80095580', '0x80097CC0', '81706059e0fa2f019c74e2275e6d22aacdc34c830ebd96f93cffb7ddc1ff811d', 'tables/data between the code and the tail function (0xFFFF/0 runs; audit flagged 0x800956B0..0x80095924 and 0x800960A4)'),
        ('0x80097EC0', '0x8009A0C0', 'cc2244f4c90220ae943679b69069851b26956a8b5c46e34d6822ee0daac41cb8', 'data region after the small code block at 0x80097CC0'),
    ],
}

images = []
for cls, (addr, why) in classes.items():
    seen = set()   # identical payloads at the same destination (e.g. containers 159..194 duplicate 87..122)
    for d in sorted((d for d in inv if d['base'] == cls), key=lambda d: d['abs_off']):
        if d['sha256'] in seen:
            continue
        seen.add(d['sha256'])
        item = {
            'name': f"{cls}-{d['path'].replace('/', '_')}",
            'method': 'fixed_address_extents',
            'allow_missing': True,
            'reason': why,
            'extents': [{
                'file': 'BUST2.BIN',
                'file_offset': 0,
                'base': 0,
                'address': f"0x{d['payload_off']:X}",
                'size': f"0x{d['payload_size']:X}",
                'sha256': d['sha256'],
                'load_addr': addr,
                'note': f"DF2_ entry {d['path']} (DBI_ header at 0x{d['abs_off']:X}, bss 0x{d['bss']:X})",
            }],
        }
        if d['path'] in EXCLUDED:
            item['excluded_ranges'] = [{'start': a, 'end': b, 'sha256': h, 'reason': r} for a, b, h, r in EXCLUDED[d['path']]]
        images.append(item)

profile = {
    'schema': 'psxrecomp AOT methods v1',
    'game_id': 'SLUS-01159',
    'disc_hashes': {'sha256': disc_sha},
    'main_ram_bytes': '0x200000',
    'images': images,
    'checks': [
        # The three fixed destinations are built with lui/addiu right before the installer call.
        # FUN_8003235C: lui a1,0x801d ; addiu a1,a1,-0x3af0 (= 0x801CC510)  — verified below by `words`.
    ],
    'expected_records': sum(len(i['extents']) for i in images),
    'strict_bounds': True,
    'static_output': 'generated/overlays_static.c',
}
json.dump(profile, open(out_path, 'w'), indent=1)
print(f'{len(images)} unique images of {len(inv)} blocks; disc sha256 {disc_sha}; -> {out_path}')
