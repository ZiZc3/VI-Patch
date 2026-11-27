# GTA VI patches

Game patches for **Grand Theft Auto VI** on a jailbroken **PS5**, made for [PS Patch System](https://github.com/illusionyy/ps-patch-system).

| | |
|---|---|
| Game | Grand Theft Auto VI (`PPSA38500`) |
| Version | **01.002.001** only |
| Format | ps-patch-system XML (`PPSA38500.xml`) |

## Patches

| Patch | What it does |
|---|---|
| 🖤 **No Vignette** | Removes the dark vignette when you drive fast or aim. Story and mission effects stay. |
| ⏭️ **Skip intro logos** | Skips the Rockstar intro logo movie at startup. Every other movie plays normally. |
| 🥽 **First person you can walk in (L2+R3)** | **L2+R3** switches on first person on foot that you can walk and play in. Press **L2+R3** again to go back to third person. |

## How to install

1. Put the patch entries below in `/data/user/patches/xml_prospero/PPSA38500.xml` on your PS5. Add them to the file if you already have one.
2. Turn the patches on in PS Patch System.
3. Start the game.

## Patch XML

```xml
<Metadata Title="Grand Theft Auto VI" Name="No Vignette" Note="Removes the black vignette when driving fast and when aiming." Author="ZiZc3" PatchVer="1.0" AppVer="01.002.001" AppElf="eboot.bin">
  <PatchList>
    <Line Type="bytes" Address="0x00e724cd" Value="e93101000090" />
    <Line Type="bytes" Address="0x00e722af" Value="e9a400000090" />
  </PatchList>
</Metadata>
<Metadata Title="Grand Theft Auto VI" Name="Skip intro logos" Note="Skips the Rockstar intro logo movie." Author="ZiZc3" PatchVer="2.0" AppVer="01.002.001" AppElf="eboot.bin">
  <PatchList>
    <Line Type="bytes" Address="0x04877f43" Value="c7470801000000eb0f" />
    <Line Type="bytes" Address="0x04877f50" Value="817f04a45f0e057502ebe8e940e8ffff" />
  </PatchList>
</Metadata>
<Metadata Title="Grand Theft Auto VI" Name="First person you can walk in (L2+R3)" Note="L2+R3 switches walkable first person on and off." Author="ZiZc3" PatchVer="1.0" AppVer="01.002.001" AppElf="eboot.bin">
  <PatchList>
    <Line Type="bytes" Address="0x04fbdb9e" Value="83a39800000000838ba8000000ff6690" />
    <Line Type="bytes" Address="0x04fbdc0b" Value="c783a80000000200000090" />
  </PatchList>
</Metadata>
```

## How they work

- **No Vignette:** fast driving and aiming use their own camera exposure profiles (`SPOTMETER_fast` and `SPOTMETER_aiming`), and those add a black vignette. The patch skips those two profiles, so the normal camera exposure is used.
- **Skip intro logos:** the logo is a Bink movie (4K, 450 frames). When the game opens that one movie, the patch sets its frame count to 1, so it ends at once and the game moves on.
- **First person:** the game has two first-person modes. L2+R3 normally turns on a view-only mode. The patch makes L2+R3 use the forced first-person mode that the game's scripts use, which lets you move.

## Credits

- [PS Patch System](https://github.com/illusionyy/ps-patch-system) by illusionyy.
- [Leonida](https://git.disroot.org/gmanka/leonida), whose documentation of the game helped (the Bink movies and first-person notes).
