# 1 vs 100 PC

- Play 1 vs 100 on Windows or Linux, online together with Xbox 360 players.
- Bring your own game files. Nothing playable ships here.
- Website: [1vs100.fans](https://1vs100.fans)
- Help and chat: [Discord](https://discord.gg/tV3ajhwFTf)

## Start playing

1. Copy your untouched Xbox 360 packages into the `content` folder: the game package, plus the show and episode packages. They are used as they are, never unpacked.
2. On Linux, make the binaries executable once: `chmod +x launcher onevs100`.
3. Open `launcher` (`launcher.exe` on Windows).
4. On the Player tab, enter your gamertag and choose your avatar file.
5. Press Launch.

The game connects to the 1vs100.fans server by default. You can change the server, turn on windowed mode, and enable keyboard controls in the launcher's Settings tab.

## Your avatar

Make your avatar at [360sona.com](https://360sona.com). Open **My Sonas**, then **Import / Export**, and click **Save the description (.bin)**. Pick that `.bin` file in the launcher.

To show avatars at all, the game needs the avatar asset files from Xbox 360 System Update 17559 ([download](https://archive.org/details/xbox-360-system-update-17559-cd-usb)). Add this line to `onevs100.toml`, with the path to the `FFFE07DF00000002` package or to the folder holding `0000000000000000/FFFE07DF/...`:

```
native_avatar_content = "/path/to/avatar/content"
```

## Without the launcher

You can start `onevs100` directly. It reads its settings from `onevs100.toml`. Your gamertag and avatar live in `userdata/profiles/` next to it.

`onevs100.toml` uses flat keys, one per line (`gameserver_host = "..."`). The launcher rewrites the file when you press Launch, so hand-kept comments and blank lines are not preserved.

## If something goes wrong

- If pressing Launch seems to do nothing, run `onevs100` (`onevs100.exe`) in a terminal next to the launcher and read the log: a missing package or DLL shows up there.
- If you upgraded from an older build, rename `content_install` to `content` in `onevs100.toml` (and on the command line). The old name is ignored.
- Something else not working? Ask on [Discord](https://discord.gg/tV3ajhwFTf).
