# VI-Patch standalone loader

A PS5 payload that applies the VI-Patch patches to Grand Theft Auto VI without PS Patch System. It reads the same `PPSA38500.xml`, checks the game's original bytes before it writes anything, and works on the running game.

Tested on a PS5 Slim on firmware 13.60 (Y2JB, kstuff, Payload Manager), game version 01.002.001.

## How it works

1. You send `vi-patch-loader.elf`. It waits up to 10 minutes for GTA VI (`PPSA38500`). If the game is already open, it uses that.
2. For each patch that is on in the config, it reads every line's address from the game and compares it with the original bytes of 01.002.001 (`originals.txt`).
3. It writes a patch only if all its lines still hold the original bytes, then reads them back to check.
4. It shows a notification with the result, writes `/data/vi-patch/log.txt` and exits.

A patch is skipped, with the reason in the log, when:

- the game is another version, or
- another payload already changed those bytes. For example, HSgod's FPS unlocker writes two of the three 60 FPS lines, so 60 FPS is skipped after it.

Sending the loader again is safe: patches that are already in place are left as they are.

## Use

1. Send `vi-patch-loader.elf` to the console with your payload loader (elfldr port 9021, Payload Manager, ...).
2. Start GTA VI within 10 minutes. Send the loader before the game for **Skip intro logos**: it only helps if it lands before the logos play.
3. The first run writes `/data/vi-patch/config.ini`. Edit it over FTP to choose patches, then send the loader again:

```ini
60 FPS=0
No Vignette=1
No depth of field=1
Skip intro logos=1
Weather fix=1
No clothing=0
Mandy replaces Lucia=0
dump=0
```

60 FPS is off by default because it needs 120 Hz output, which PS Patch System forces and this loader does not. No clothing and Mandy are opt-in. Mandy works only in a new game.

## Build

Needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) v0.43, clang/lld 18 and python3.

```sh
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
```

`gen_patches.py` turns `../src/PPSA38500.xml` into `patches.h`. To build from another copy of the XML, such as a release asset:

```sh
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk XML=/path/to/PPSA38500.xml
```

## Adding or changing a patch

Every XML line needs its original bytes in `originals.txt`. Without them the build stops and names the missing lines. To get them:

1. Build with the new XML.
2. Set `dump=1` in the config.
3. Send the loader with the unpatched game running.

The log then lists the bytes at every address in `originals.txt` format, ready to paste. `zero` stands for a range that is all 00.

## Notes

- **Runtime patching.** PS Patch System patches the game as it loads; this loader writes into the running game. Inside each patch, lines that fill unused space (caves) are written first, so a hook never jumps into an empty cave. The other lines are written last to first.
- **Cave conflicts.** Weather fix 2.0 (#4), No clothing and Mandy put code in the free space at `0x9369400`–`0x9369fe7`. Other payloads use that space too. For example, a Turkish translation payload writes code at `0x9369300`–`0x936ad0c`.
  - If such a payload ran first, the loader sees the changed bytes and skips these patches.
  - If it runs after the loader, it overwrites them. Don't combine them.
- **Writes.** Code pages are not writable through the debug syscall on recent firmware, so `ptwalk.c` writes through the kernel's direct map (from ps5debug-NG). The debug syscall stays as a fallback.

## Credits

- [ZiZc3](https://github.com/ZiZc3): the patches.
- [HSgod/ps5-fpsunlocker-gta6](https://github.com/HSgod/ps5-fpsunlocker-gta6): game lookup and the ptwalk port this loader uses.
- [OpenSourcereR/ps5debug-NG](https://github.com/OpenSourcereR-dev/ps5debug-NG): the original `proc_ptwalk.c`.
- [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk): the toolchain.

## License

GPL-3.0-only (see `LICENSE`), because `ptwalk.c` is GPL-3.0-only. This covers the `standalone/` folder only.
