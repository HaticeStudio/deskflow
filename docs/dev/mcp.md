# MCP in Deskflow (fork design)

Branch `mcp` on HaticeStudio/deskflow, pinned to upstream `v1.27.0` (`081f6478`).
Goal: every machine that runs `deskflow-core` also exposes an MCP server so an AI
agent (Claude Code over ssh) can see the screen and drive input on that machine,
and on the server machine can also switch the shared keyboard/mouse between
computers. Human KVM behaviour of Deskflow is unchanged.

## Why inside deskflow-core

- `deskflow-core` already holds the one object that can inject input on this
  platform (`OSXComputer` on macOS, via `IPlatformScreen::fake*`) and already has
  the Accessibility / Input Monitoring permissions. Reusing it means one binary,
  one set of TCC grants.
- The server already knows all connected computers and has a `switchToScreen`
  action (`EventTypes::ServerSwitchToComputer` + `Server::SwitchToComputerInfo`).
  Posting that event is thread-safe and needs no private access.
- The GUI talks to the core over a `QLocalServer` named `kCoreIpcName` with
  newline-delimited `key=value` messages (`src/lib/deskflow/ipc/IpcServer.cpp`).
  The MCP endpoint follows the same shape: a second `QLocalServer`, JSON-RPC
  2.0 newline-delimited, owned by the Qt main thread like `CoreIpcServer`.

## Components

```
src/lib/mcp/                       new static lib "mcp" (Qt Core + Network)
  McpServer.{h,cpp}                QLocalServer "deskflow-mcp-<uid>", JSON-RPC framing,
                                   MCP handshake (initialize / ping / tools/list / tools/call)
  McpTools.{h,cpp}                 tool registry: name, JSON schema, handler
  McpBridge.{h,cpp}                hands tool calls to the core thread and waits for the result
src/lib/platform/OSXScreenshot.mm  CGDisplay / ScreenCaptureKit capture -> PNG bytes (macOS only)
src/apps/deskflow-mcp/             tiny console binary: stdio <-> local socket bridge, so
                                   Claude Code can run `ssh host deskflow-mcp`
```

`deskflow-core.cpp` creates `McpServer` right after `CoreIpcServer` and gives it
the `App*` and the `EventQueue*`.

## Threading

The core runs in `coreThread` with its own `EventQueue`; platform screen calls
must happen there (macOS Carbon loop). `McpBridge` wraps each tool call in an
`Event` of a new type `EventTypes::McpToolCall` carrying a `std::function` plus
a `QPromise`-like waiter, pushes it with `EventQueue::addEvent`, and the `App`
registers one handler that executes the function on the core thread. The MCP
socket handler blocks at most `timeout_ms` (default 5 s) per call.

## Tools (v1)

Local machine (server or client):

| tool            | args                                  | result                                    |
|-----------------|---------------------------------------|-------------------------------------------|
| `screen_info`   |                                       | name, mode, shape, cursor, connected list |
| `screenshot`    | `display?`, `scale?` (default 0.5)    | image/png (base64) + size                 |
| `mouse_move`    | `x`, `y`                              | ok                                        |
| `mouse_click`   | `x?`, `y?`, `button` (left/right/middle), `double?` | ok                          |
| `mouse_scroll`  | `dx`, `dy`                            | ok                                        |
| `key_type`      | `text`                                | ok (uses platform key state, unicode ok)  |
| `key_press`     | `keys` e.g. `cmd+shift+4`             | ok                                        |
| `clipboard_get` |                                       | text                                      |
| `clipboard_set` | `text`                                | ok                                        |

Server only:

| tool                 | args           | note                                        |
|----------------------|----------------|---------------------------------------------|
| `list_computers`     |                | from `Server::getClients`                   |
| `switch_to_computer` | `name`         | posts `ServerSwitchToComputer`                |
| `lock_to_computer`   | `locked: bool` | posts `ServerLockCursorToComputer`            |

Coordinates are in that machine's own screen space (same as `getShape`).

## Transport and security

- Local socket only, mode 0600 for the current user (not `WorldAccessOption`).
  Never a TCP port.
- Remote use goes through ssh: Claude Code MCP config
  `{"command": "ssh", "args": ["metal", "/Applications/Deskflow.app/Contents/MacOS/deskflow-mcp"]}`.
- `screenshot` needs the macOS Screen Recording permission for `deskflow-core`;
  the first call will trigger the system prompt once.
- Tool calls are logged at INFO with tool name and arg summary (no screenshot
  bytes) through the existing `LOG_*` so the GUI log shows agent activity.

## Settings

New keys in `Deskflow.conf`:

```
[mcp]
enabled=true          ; default false upstream-safe
socketName=           ; default deskflow-mcp-<uid>
timeoutMs=5000
```

## Licensing and upstream

- Deskflow is GPL-2.0. The fork stays GPL-2.0, every new file carries the SPDX
  header used by upstream. If the fork is published, source must be available.
- Upstream pin: `v1.27.0`. Rebase `mcp` onto new tags; the touch points are
  `deskflow-core.cpp`, `EventTypes.h`, `App.cpp`, and the new `src/lib/mcp` dir,
  so conflicts should stay small. See `UPSTREAM.md`.

## Milestones

1. Baseline build of unmodified v1.27.0 on this Mac with Homebrew Qt. Done when
   the built `Deskflow.app` runs server mode and metal connects.
2. `McpServer` with `initialize`, `ping`, `tools/list`, `screen_info`,
   `screenshot`; `deskflow-mcp` bridge; Claude Code sees the tools over ssh.
3. Input tools on the local machine via `IPlatformScreen::fake*`.
4. Server tools (`list_computers`, `switch_to_computer`, `lock_to_computer`).
5. Deploy to metal, Screen Recording grant, end-to-end: Claude takes a screenshot
   of metal, clicks a System Settings toggle, and switches the KVM back.
