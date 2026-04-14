# mino-nrepl

An [nREPL](https://nrepl.org) server for [mino](https://github.com/leifericf/mino), enabling interactive development from any nREPL-compatible editor.

## Build

```
git clone --recursive https://github.com/leifericf/mino-nrepl.git
cd mino-nrepl
make
```

Requires a C99 compiler. No other dependencies.

## Usage

```
./mino-nrepl                  # random port (written to .nrepl-port)
./mino-nrepl --port 7888      # fixed port
./mino-nrepl --bind 0.0.0.0   # listen on all interfaces
```

The server writes a `.nrepl-port` file to the current directory on startup and removes it on shutdown. Most editors auto-detect this file.

## Editor Setup

### Conjure (Neovim)

Open a file in the project directory. Conjure auto-connects via `.nrepl-port`.

### vim-fireplace (Vim)

Open a file in the project directory. Fireplace auto-reads `.nrepl-port`. Eval with `cpp`.

### CIDER (Emacs)

`M-x cider-connect-clj`, enter `localhost` and the port number. Basic eval works; advanced CIDER features that depend on cider-nrepl middleware are not available.

### Calva (VS Code)

Command Palette > "Calva: Connect to a Running REPL Server" > "Generic" > enter host and port.

## Supported Operations

| Op | Description |
|---|---|
| `clone` | Create a new session |
| `close` | Close a session |
| `describe` | List server capabilities |
| `eval` | Evaluate code in a session |
| `completions` | Symbol completion by prefix |
| `load-file` | Evaluate file contents |
| `ls-sessions` | List active sessions |

## Tests

```
make test
```

## License

MIT
