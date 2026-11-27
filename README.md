<h1 align="center">VI-Patch</h1>

<p align="center">
  <strong>Patches for Grand Theft Auto VI PS5.</strong><br>
</p>

<p align="center">
  <code>PPSA38500</code> &nbsp;&middot;&nbsp;
  <code>v01.002.001</code>
</p>

---

VI-Patch is a set of patches for Grand Theft Auto VI. Use them with [PS Patch System](https://github.com/illusionyy/ps-patch-system), or with the **VI-Patch Payload**.

---

## Patches

- **60 FPS**: removes the 30 fps cap. Needs 120 Hz forced on in PS Patch System settings.

- **1080p**: renders the game at 1080p instead of 1440p. Works on its own (30 FPS) or together with 60 FPS.

- **Weather fix**: on version 01.002.001 the weather gets stuck on sunny days (a game bug). This brings back rain, storms, hurricanes, fog and the four seasons.

- **Close game fix**: fixes the hang and the `CE-108255-1` error when you close the game. Made by [Eizovoid](https://github.com/Eizovoid/gta-vi-ps5-patches).

- **First person**: press **R3** + **D-pad Right** to switch between first and third person.

- **Skip intro logos**: the Rockstar logo movie is skipped, so the game goes straight to loading.

- **Mandy replaces Lucia**: Lucia becomes Mandy, with her own face, body and outfits. **WORKS ONLY IN NEW GAME**

- **No clothing**: hold **D-pad Down** and press **R3** to take the clothes off. Do it again to put them back on. Works on Jason and Lucia.

- **No Vignette**: no dark corners when you drive fast or aim.

- **No depth of field**: turns off the depth of field effect.

- **No lens flare**: removes lens flares from the sun and lights.

---

Use either the XML or the payload, not both.

## Run XML

You need a PS5 with **[PS Patch System](https://github.com/illusionyy/ps-patch-system)** running.

1. Copy `PPSA38500.xml` to `/data/user/patches/xml_prospero/`.
2. Turn on the patches you want in PS Patch System.
3. Start the game.

## Run Payload

1. Send `vi-patch.elf` with your payload loader **before** you start the game.
2. Start the game.
3. 60 FPS and Weather fix on by default.

The payload closes when it's done. Send it again every time you start the game.

> [!TIP]
> **Choose your patches** in `/data/vi-patch/vipatch.txt` (made on the first run). Set a patch to `1` for on or `0` for off, then send the payload again before you start the game.
>
> ```ini
> 60 FPS=1
> 1080p=0
> First person=0
> No Vignette=0
> No depth of field=0
> No lens flare=0
> Skip intro logos=0
> Weather fix=1
> No clothing=0
> Mandy replaces Lucia=0
> Close game fix=0
> ```

> [!IMPORTANT]
> The patches are made for game version **01.002.001** only.

---

## Credits

- **[PS Patch System](https://github.com/illusionyy/ps-patch-system)** by illusionyy: the patch loader
- **[Eizovoid](https://github.com/Eizovoid/gta-vi-ps5-patches)**: the Close game fix
