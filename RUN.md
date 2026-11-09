# 1 vs 100 PC

- Play 1 vs 100 on Windows, Linux, or macOS, online together with Xbox 360 players.
- Bring your own game files. Nothing playable ships here.
- Website: [1vs100.fans](https://1vs100.fans)
- Help and chat: [Discord](https://discord.gg/tV3ajhwFTf)

## Start playing

1. Copy your untouched Xbox 360 packages into the `content` folder: the game package, plus the show and episode packages. They are used as they are, never unpacked.
2. Extract the archive into a writable folder: `onevs100-linux-*.tar.xz` on Linux, `onevs100-windows-*.zip` or the installer on Windows, `onevs100-mac-*.zip` on macOS. On Windows and Linux, first run creates `userdata/` next to the executables, so a read-only location fails. On macOS the app can go anywhere, including `/Applications`. Requires macOS 15 or newer.
3. On macOS move `1vs100.app` out of Downloads (for example to `/Applications`), then clear quarantine once: `xattr -dr com.apple.quarantine 1vs100.app`. On Linux the tar preserves exec bits, so no `chmod` is needed.
4. Open launcher (launcher.exe on Windows, launcher inside 1vs100.app on macOS).
5. On the Player tab, enter your gamertag and choose your avatar file.
6. Press Launch.

The game connects to the 1vs100.fans server by default. You can change the server, turn on windowed mode, and enable keyboard controls in the launcher's Settings tab. The Files tab shows whether your content/ and avatar files check out; Launch stays disabled until they do.

On macOS, `onevs100.toml`, `userdata/` and the `content` folder live in `~/Library/Application Support/1vs100` instead of next to the executables. The launcher's Settings tab shows the full path.

## Your avatar

Make your avatar at [360sona.com](https://360sona.com). Open **My Sonas**, then **Import / Export**, and click **Save the description (.bin)**. Pick that `.bin` file in the launcher.

The game also needs the avatar pack table from Xbox 360 System Update 17559 ([download](https://archive.org/details/xbox-360-system-update-17559-cd-usb)). Put `AvatarAssetPack.toc`, or the `FFFE07DF00000002` package that holds it, in the `content` folder with the game packages.

## Without the launcher

You can start `onevs100` directly. It reads its settings from `onevs100.toml`. Your gamertag and avatar live in `userdata/profiles/` next to it, or in the data folder on macOS.

`onevs100.toml` uses flat keys, one per line (`gameserver_host = "..."`). The launcher rewrites the file when you press Launch, so hand-kept comments and blank lines are not preserved.

## If something goes wrong

- A missing-files startup failure shows an error dialog naming what it saw in `content` (missing, empty, or files but no game package), then the game exits. `--game_data_root does not exist: .../assets` means the game found no game files. Copy the untouched game package (plus the show and episode packages) directly into the `content` folder named in the dialog, with the files inside and no subfolders. On Windows and Linux you can instead unpack `default.xex` and its media into an `assets` folder next to the executable.
- If pressing Launch seems to do nothing, run `onevs100` (`onevs100.exe`) in a terminal next to the launcher and read the log: a missing package or DLL shows up there.
- A crash shows a dialog naming the crash or log file before the game exits. Attach that file on Discord, or use the launcher's Send logs. A missing avatar pack warns once and the game continues without avatars.
- To report a problem, set **Log level** to `debug` under Logs in the launcher's Settings tab, and tick **Write the game's Lua output to the log**. Then play until the problem happens and send the log. The Settings tab shows the log folder. Log times are UTC, the same as the server's, so the two can be read side by side.
- If you upgraded from an older build, rename `content_install` to `content` in `onevs100.toml` (and on the command line). The old name is ignored.
- Something else not working? Ask on [Discord](https://discord.gg/tV3ajhwFTf).
