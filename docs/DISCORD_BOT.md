# Discord Bot on SteveOS

SteveOS Server Mode is designed to host a normal Node.js Discord bot. The native desktop does not run Node.js itself. The GUI launches Server Mode, and Server Mode supplies the Linux userspace, Node.js, npm, networking, FFmpeg and the audio stack.

## Compatibility

The Server Mode image includes Node.js and npm. Alpine Linux 3.24 currently provides Node.js 24.x, which is compatible with current discord.js releases that require Node.js 24.17 or newer.

The image also includes FFmpeg, Opus, PipeWire, ALSA and the Linux audio devices used by voice-capable Discord bots. Native build tooling is included so npm packages with native components can compile when necessary.

## Using a bot project

Put the bot project in:

```text
\\SteveOS\\Server\\bot\\
```

The directory should contain the bot's normal `package.json` and entry point. A `package-lock.json` is recommended.

For a Node bot, the default command is:

```text
node index.js
```

The server configuration can override this with `DISCORD_START`.

## GUI workflow

The intended normal-user workflow is:

1. Open **Server Manager**.
2. Install Server Mode if it is not installed.
3. Open the Discord service settings.
4. Select the bot project or provide its project source.
5. Enter the bot configuration without putting the token into the SteveOS source repository.
6. Enable **Start Discord Bot automatically**.
7. Start Server Mode.
8. Server Mode installs the bot's npm dependencies and starts it.
9. If the bot exits, the service supervisor restarts it.

The native desktop already has a Server Manager and persistent service-start settings. The remaining GUI work is to expose the bot source, start command and secret/token fields directly in that window instead of requiring the user to edit `server.conf` manually.

## Token security

Do not commit a Discord bot token to the SteveOS repository. Store it in the installed Server Mode configuration or a protected runtime secret store.

If a token has ever been exposed publicly, rotate it in the Discord Developer Portal before using the bot on SteveOS.

## Voice support

Bots using `@discordjs/voice` can run in Server Mode. Voice support depends on the bot's own npm dependencies and the host exposing usable audio/network devices. FFmpeg and Opus support are included in Server Mode.

## Windows compatibility

The Discord bot does not need Wine. It runs natively as a Linux Node.js process inside Server Mode. Wine is reserved for Windows `.EXE` applications.

## Current limitation

The native framebuffer GUI is still being expanded. Server Manager can install and boot Server Mode and toggle service startup intent, but the complete Discord configuration form is not yet wired into the native desktop. Until that GUI work lands, the same settings can be supplied through `\\SteveOS\\Server\\server.conf`.
