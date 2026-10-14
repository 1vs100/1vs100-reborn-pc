1 vs 100 mods folder
====================

Anything here is applied to the game's UI packages each time the launcher starts
a show. The originals are never changed; "Play without mods" on the launcher's
Debug tab plays them as shipped.

See what's there
  Launcher > Debug > "Export game images & text" writes everything to _export:
  INDEX.html shows every image with its size; *.xus.txt files list every piece
  of on-screen text (key, tab, text).

Swap an image (logo, glyph, background)
  Save a PNG of the same size under the same path:
    mods\launch\files\assets\intro_logo.png
    mods\launch\files\assets\compress\Jumbotron_Logo.png
  Some images are in both packages (the jumbotron logo is); put the PNG under
  mods\launch\files and mods\episode\files to change both.
  "Xbox texture" entries (assets\compress\...) are experimental: a plain PNG is
  expected to load in their place (the original episode package already does
  this for SprintLogo.png), but check the screen.

Change text
  Put a .json file in mods\launch\text (or mods\episode\text):
    { "tables": {
        "*\\onevs100.xus": { "SUB_LH_PRIZE_01": "Your text here" }
    } }
  "*" means every language; use e.g. "en-us\\onevs100.xus" for one. A key ending
  in * changes every key that starts with it. Write a line break as \r\n.

The game's own prize-free text and points glyph are in work\native-parity\mods
and are applied first; your mods are applied after them, so yours win.
If a mod breaks a package, the launcher puts that package back to the original
and writes "Mods: ... left original" in its output.
