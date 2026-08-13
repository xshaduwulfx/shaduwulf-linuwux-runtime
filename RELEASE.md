# Shaduwulf's LinUwUx Runtime

Standalone LinUwUx runtime rework distributed as a preloadable Linux shared
library.

> [!IMPORTANT]
> This project is experimental and under active development.

## Assets

Download:

- `liblinuwux_runtime.so`
- `linuwux`
- `SHA256SUMS`

GitHub also provides source archives for the tagged release.

Verify the downloaded runtime and launcher with:

```sh
sha256sum -c SHA256SUMS
```

## Install

Create the local installation directories:

```sh
mkdir -p ~/.local/lib ~/.local/bin
```

Install the runtime:

```sh
install -m 0755 liblinuwux_runtime.so ~/.local/lib/liblinuwux_runtime.so
```

Install the launcher:

```sh
install -m 0755 linuwux ~/.local/bin/linuwux
```

If `~/.local/bin` is not already in your `PATH`, the launcher can still be
used through its full path:

```text
~/.local/bin/linuwux
```

## Usage

The `linuwux` launcher loads the runtime through `LD_PRELOAD` and preserves
an existing preload chain.

### Steam

Use Proton-CachyOS or Proton-GE.

Steam launch option:

```text
~/.local/bin/linuwux %command%
```

Valve's official Proton is currently outside the primary supported target.

### Faugus Launcher

Shaduwulf's LinUwUx Runtime is primarily developed and tested with
Proton-CachyOS in Faugus Launcher.

Configure the game so the Linux-side launch command is wrapped with:

```text
~/.local/bin/linuwux
```

Alternatively, the runtime can be injected manually through:

```text
LD_PRELOAD=/home/YOUR_USERNAME/.local/lib/liblinuwux_runtime.so
```

The exact integration can depend on the launcher and UMU configuration.

### Heroic Games Launcher

Use Proton-GE or another compatible community Proton build.

The runtime can be supplied through the game's environment:

```text
LD_PRELOAD=/home/YOUR_USERNAME/.local/lib/liblinuwux_runtime.so
```

Launcher-specific behavior may vary while the runtime remains experimental.

### Lutris

Add the following environment variable to the game configuration:

```text
LD_PRELOAD=/home/YOUR_USERNAME/.local/lib/liblinuwux_runtime.so
```

Use a compatible community Wine/Proton runner.

### Command line

Any existing Linux-side launch command can be wrapped with:

```sh
~/.local/bin/linuwux COMMAND [ARG...]
```

The runtime can also be injected manually:

```sh
LD_PRELOAD="$HOME/.local/lib/liblinuwux_runtime.so${LD_PRELOAD:+:$LD_PRELOAD}" \
COMMAND [ARG...]
```

## Debugging

Enable runtime logging with:

```text
LINUWUX_DEBUG=1
```

For example:

```sh
LINUWUX_DEBUG=1 ~/.local/bin/linuwux COMMAND [ARG...]
```

## Proton compatibility

Primary targets:

- Proton-CachyOS
- Proton-GE

Valve's official Proton is currently outside the supported target and should
not be assumed to work.

Compatibility with other Wine or Proton builds may vary.

## Credits

The original LinUwUx behavior and protocol were introduced by **LinUwUx**
through `LinUwUx.patch`.

Shaduwulf's LinUwUx Runtime adapts, restructures and extends that work into a
standalone preloadable runtime.

Special thanks to **LinUwUx** for the original work and for giving this
standalone rework the green light.

## License

GNU Lesser General Public License version 2.1 or later.

See `LICENSE` and `NOTICE.md`.
