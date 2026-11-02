# Run guide

Get the game running: copy your files into place, set three settings, start it.

- You need your own Xbox 360 files. Nothing playable ships here.
- All paths below are next to the built executable, unless noted.

## Files

| Copy this | To here |
|---|---|
| `default.xex` and its media (the 1 vs 100 game package) | `assets/` |
| `vo.xwb` and `vo.xsb` from your show package (`dlcs/shows/<id>/media/audio/`) | `assets/Show/media/audio/` |
| Avatar content: the content folder with `0000000000000000/FFFE07DF/...`, the `AvatarAssetPack.toc` file itself, or the `FFFE07DF00000002` package from a dashboard system update | Anywhere, then set `ONEVS100_NATIVE_AVATAR_CONTENT` to it |
| Show and episode packages (gameserver mode needs them) | Anywhere, then list them in `content_install` |
| Your gamertag (Linux only, plain text, 15 characters max) | `profiles/local-name.txt` inside the player data folder |
| Your avatar: the `.bin` exported from 360sona, or from an old profile backup | `profiles/avatar.bin` inside the player data folder. Without it the game builds one from your profile id |

Make your avatar at [360sona.com](https://360sona.com), the Xbox 360 avatar editor rebuilt for the browser: **My Sonas → Import / Export → Save the description (.bin)**. The avatar asset files it and the game are built from come from Xbox 360 System Update 17559 ([CD + USB download](https://archive.org/details/xbox-360-system-update-17559-cd-usb)).

## Settings

Put this in `onevs100.toml` next to the executable, with your values:

```
[Online]
online_service = "gameserver"
gameserver_host = "1vs100.example.org"

[Content]
content_install = "/path/to/packages"
```

## Start

```
cd build/linux
ONEVS100_NATIVE_AVATAR_CONTENT=/path/to/avatar/content ./onevs100
```

On Windows, open `1vs100 Launcher.exe` and use the Play tab. Every `onevs100.toml` setting also works as a command line flag, for example `--gameserver_host=1vs100.example.org`.
