#!/usr/bin/env python3
"""List the C2C passenger requests of every course (Scripts/bin/Req_*.bin).

usage: request_catalogue.py <game folder> [-v]

The course ids are the order of the EXE's script-name table 64EF38 (read from
OR2006C2C.cache in the game folder). For each request type (the def's +0) and
sub-kind (+4) it prints one record to force with
OR2_DEBUG_REQUEST=<course>,<set>,<record>; -v lists every record.

Req_*.bin: u32 directory count, u32 relocation count, relocations
{u32 at, u32 target} (file offsets), then the directory {u32 hash, u32 count,
u32 offset}. The hash is 4F1260's (h = h * 0x83 + upper(c)). The sets
REQUEST_NORMAL / REQUEST_SPECIAL1..3 (64F080) hold 0x74-byte records:
u16 start cs, u16 end cs, ... +58 the def (a relocated pointer).
"""
import os, struct, sys, zlib, collections

SETS = ['REQUEST_NORMAL', 'REQUEST_SPECIAL1', 'REQUEST_SPECIAL2', 'REQUEST_SPECIAL3']

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    game = sys.argv[1]
    verbose = '-v' in sys.argv
    cache = open(os.path.join(game, 'OR2006C2C.cache'), 'rb').read()
    img = zlib.decompress(cache[64:])
    u32 = lambda a: struct.unpack_from('<I', img, a - 0x400000)[0]
    def cstr(a):
        o = a - 0x400000
        return img[o:img.index(b'\0', o)].decode('latin1')
    def h(name):
        v = 0
        for ch in name.upper():
            v = (v * 0x83 + ord(ch)) & 0xffffffff
        return v
    hashes = {h(n): i for i, n in enumerate(SETS)}
    best = collections.OrderedDict()
    for course in range(66):
        name = cstr(u32(0x64ef38 + course * 4))
        path = os.path.join(game, 'Scripts', 'bin', name + '.bin')
        if not os.path.exists(path):
            continue
        d = open(path, 'rb').read()
        count, nrel = struct.unpack_from('<II', d, 0)
        rel = dict(struct.unpack_from('<II', d, 8 + k * 8) for k in range(nrel))
        directory = 8 + nrel * 8
        for i in range(count):
            hh, n, off = struct.unpack_from('<III', d, directory + i * 12)
            if hh not in hashes:
                continue
            for r in range(n):
                at = off + r * 0x74
                start, end = struct.unpack_from('<HH', d, at)
                df = rel.get(at + 0x58)
                if df is None or df + 8 > len(d):
                    continue
                kind, sub = struct.unpack_from('<II', d, df)
                row = (course, name, hashes[hh], r, start, end)
                if verbose:
                    print('%x/%d course %d %s set %d record %d cs %d..%d' % ((kind, sub) + row))
                key = (kind, sub)
                if key not in best or start < best[key][4]:
                    best[key] = row
    for (kind, sub), (course, name, s, r, start, end) in sorted(best.items()):
        print('type %2x/%d: OR2_DEBUG_REQUEST=%d,%d,%d  (%s %s, cs %d..%d)' %
              (kind, sub, course, s, r, name, SETS[s], start, end))

if __name__ == '__main__':
    main()
