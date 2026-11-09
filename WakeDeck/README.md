# WakeDeck

WakeDeck wakes a sleeping computer with a Wake-on-LAN "magic packet", waits until the service you need on it is actually ready, and then opens your client, for example Moonlight for a PC that runs Sunshine. It is written in C++17 with Qt 6 for macOS.

**Status: Milestone 5 of 6, the graphical interface.** WakeDeck is now an app: a list of your computers, one **Wake & Connect** button, and a timeline that keeps "wake request sent" and "service ready" apart. The command-line tool remains for scripts and tests. The signed disk image follows in the last milestone; see [Current Scope](#current-scope).

## Purpose

Waking a PC remotely usually means a script that fires a magic packet and prints "sent", leaving you to guess when the machine is really usable. WakeDeck keeps two facts apart that are easy to confuse:

- **Wake request sent**: the packets left this computer. That is all a sender can ever know.
- **Service ready**: the program you want to use, such as Sunshine or Remote Desktop, answered.

Anyone can add their own computers in the app: how to wake each one, which address and service to check, and which installed client to open. Personal network settings never live in this repository: they are saved in a profiles file in your own Library folder.

## Features

### Available now (Milestone 5)

- **The app.** Computers on the left; on the right the selected one, a big **Wake & Connect** button, the two facts of the current run ("Wake request: not sent / sent at 22:41:07", "Service: not checked / checking… / ready at 22:41:48 / not ready: timed out"), and the time-stamped timeline. **Check now**, **Wake only** and **Open client** do one step each. **Cancel** stops a run; **Keep waiting 1:00** appears while the app is waiting for the service. A timeout ends with the **What happened** box: what was observed, when the wake request went out, and likely causes, with **Try again** and **Copy details**.
- **Adding a computer.** The editor has three groups, Wake, Check and Connect, each with a Test button that uses the real engine: send the wake request now, check the service now, or find the client without opening it. The MAC address, ports and every other value are validated with the same rules as the profiles file, so the dialog cannot save a half-configured computer. Browse… picks an application; the Wake group can be switched off for always-on computers.
- **When the client is missing.** The run still shows that the service is ready, then explains where it looked and offers **Locate client…**, which saves the chosen application into the profile.
- **File menu.** New, edit and delete computers; import and export a single profile as JSON; **Load Example** adds the fictional example profile bundled into the app. **Help ▸ How Waking Works** explains local and remote waking in a few paragraphs.
- **Everything underneath** is the engine of the earlier milestones: the wake sender, the Sunshine and TCP checks, the session state machine, the saved profiles and the client launcher, all exercised from the command line too (`wake`, `check`, `connect`, `launch`, `profile`).
- Nothing blocks the window: checks, name lookups, the pauses between packets and the client launch all run on the event loop.
- Tests cover the packet logic, the real socket code over the loopback interface only, the probes against local servers (including a fake Sunshine), the waker, every path of the session with fakes, the explanation text, the profile format with its validation and recovery paths, the client lookup and launch with harmless programs only, the command-line behaviour end to end on loopback, and the window and the editor driven on Qt's offscreen platform with fake probes, wakers and launchers.

### Planned

- A signed and notarized disk image, built against Qt's official binaries so it runs on macOS 13 and later (Milestone 6).

## How It Works

A magic packet is a UDP datagram of 102 bytes: six `0xFF` bytes followed by the target's MAC address repeated sixteen times. A sleeping PC has no working IP address. Its network card watches the wire for that pattern and powers the machine on, so the packet must arrive on the PC's own network segment, which normally means a broadcast on that LAN.

- **Same network.** Your Mac and the PC share a Wi-Fi or Ethernet network: send to the network's broadcast address, for example `192.168.1.255`, and the card sees it. Most Wi-Fi adapters cannot wake a powered-off machine; Ethernet usually can.
- **From elsewhere.** The internet and VPNs route packets to running hosts and do not carry LAN broadcasts, and a sleeping PC is not on your VPN at all. Something always on inside the home network has to emit the broadcast for you: a router with a wake feature or a port forward to the PC's reserved address, or a small always-on device (a Raspberry Pi, a NAS) that relays the packet. WakeDeck sends to whatever destination you configure and does not assume how it reaches the PC.

Once the PC is up, a VPN such as Tailscale gives it a stable address from anywhere. That address is what the readiness check uses, and what Moonlight connects to.

### In the app

First time: open WakeDeck and click **Add…**. Give the computer a name. In **Wake**, enter its MAC address and the address the wake request should be sent to, usually the broadcast address of its network; the ports and counts default to 9 and 7, five packets each. In **Check**, choose Sunshine or a TCP port and enter the computer's Tailscale name or address. In **Connect**, choose Moonlight and either "Open Moonlight" or "Start streaming an app" with the app's name as Moonlight shows it, for example Desktop. The three Test buttons tell you at once whether the packets left, whether the service answers right now, and whether the client was found. Click OK. The profile is saved in `~/Library/Application Support/WakeDeck/profiles.json`.

Every day: select the computer and click **Wake & Connect**, or double-click it. If the service is already ready, the client opens within a second and the timeline says no wake was needed. If the computer is asleep, the timeline shows the check, the wake request with its time, the checks that follow, the moment the service answered, and the launch. If nothing answers in the configured time, the **What happened** box says exactly what was observed and what that usually means.

### Wake & Connect, step by step

1. **Check.** Ask the service once. For Sunshine that is an HTTP `GET /serverinfo` on port 47989, the same request Moonlight uses to show a PC as online; the reply is XML with the host name and a state such as `SUNSHINE_SERVER_FREE`, and a reply that is not Sunshine's counts as *not ready*. For any other service it is a plain TCP connection to its port.
2. **Wake.** If the service did not answer and a wake is configured, send the magic packets. "Wake request sent" is reported as its own event.
3. **Wait.** Check again every few seconds (default 3 s) until the service answers or the limit (default 2 minutes) runs out. Each check is classified:
   - *refused*: something at that address refused the connection, usually the computer itself, awake but with the service not listening yet;
   - *timed out* or *unreachable*: nothing answered, so the computer is still off, or not reachable from here;
   - *name not resolved*: the host name could not be turned into an address, often a VPN that is not connected;
   - *not ready*: something answered on the port, but not with the expected reply.
4. **Launch.** Open the client configured for this computer. A computer without a client stops at "ready".

When the wait runs out, the summary says exactly what was observed, for example "40 checks, 40 timed out", when the wake request was sent, and what that pattern usually means. If the host looks like a Tailscale address it says so. The text never claims the computer is on or off; it says what answered and what did not.

A typical run as the timeline shows it, with placeholder values:

```
22:41:05  Checking Sunshine at gaming-pc.example.net:47989
22:41:07  Not ready: timed out (no answer within 2000 ms)
22:41:07  Wake request sent: 10 packets to 192.168.1.255 (ports 9 x5, 7 x5)
22:41:07  Waiting for Sunshine at gaming-pc.example.net:47989 (up to 2:00)
22:41:12  Check 2: timed out (waiting 0:07 of 2:00)
22:41:17  Check 3: timed out (waiting 0:12 of 2:00)
22:41:48  Sunshine at gaming-pc.example.net:47989 ready: hostname LIVING-ROOM-PC, state free (after 0:43)
22:41:48  Launching Moonlight, stream "Desktop" on gaming-pc.example.net
22:41:48  Launched Moonlight, stream "Desktop" on gaming-pc.example.net (process 48213)
```

### Opening the client

Moonlight has two launch modes. **Open** asks the system to open the application bundle, or to bring it to the front if it is already running, exactly as a double click in Finder would. **Stream** runs Moonlight's own executable with `stream <host> <app>`, Moonlight's command-line form, so the stream starts without any clicking; the executable is run directly because arguments handed to an already-running application are not delivered to it. A **custom** client is an absolute path to an executable or a .app bundle plus optional arguments; a bundle without arguments is opened, anything with arguments is run, and `{host}` in an argument becomes the computer's address. The host used is the one the service check used, so the client connects to the same address that just answered. Moonlight is found through Launch Services by its bundle identifier, then in /Applications and ~/Applications, or at the path saved in the profile.

### Saved profiles

A profile is the whole configuration of one computer. The file is plain JSON, one object per profile inside a `profiles` list, and looks like the example below (shortened). Every field has a default except the name and the check host, and every value is validated when the file is read, so a mistyped MAC address, port or field name is reported by name instead of producing a half-configured computer. The client settings follow one set of rules whether they come from the app, a file or the command line: a custom client needs an absolute path, streaming needs the name of the app, and those options apply to the client they belong to.

```json
{
    "name": "Living Room PC (example)",
    "wake":  { "mac": "00:00:5E:00:53:2A", "destination": "192.168.1.255", "ports": [9, 7], "repeat": 5, "intervalMs": 100 },
    "check": { "kind": "sunshine", "host": "living-room-pc.example.net", "port": 47989, "timeoutMs": 2000 },
    "pollIntervalMs": 3000,
    "overallTimeoutMs": 120000,
    "client": { "preset": "moonlight", "launchMode": "stream", "appName": "Desktop" }
}
```

A few rules protect the file. It is written atomically, so a crash while saving leaves the previous version intact. A file that is not valid JSON, or has no profiles list, is renamed to `profiles.json.corrupt-<date>` (never overwriting an earlier one) and WakeDeck starts with an empty list, telling you where the old file went. A file whose `schemaVersion` is newer than this WakeDeck understands is refused and not modified, and so is an exported single profile mistaken for a store. Single unreadable entries are skipped with a warning and one copy of the original file is kept beside it.

## Class & File Structure

| File | Role |
|---|---|
| `src/core/MacAddress.h/.cpp` | Parses and formats a 48-bit hardware address. `parse` returns `std::optional`, so invalid text never produces a half-built object. |
| `src/core/MagicPacket.h/.cpp` | Builds the 102-byte frame from a `MacAddress`, plus a hex dump for diagnostics. |
| `src/core/WakePlan.h/.cpp` | `WakeTarget` (destination, ports, repeat, interval), `expandWakePlan`, which turns it into the ordered list of sends, and `parsePortList`. |
| `src/core/UdpSender.h/.cpp` | `resolveIPv4`, the `DatagramSink` interface, and `UdpSender`, the BSD-socket implementation. The only core file that talks to the operating system. |
| `src/core/WakeSender.h/.cpp` | `sendWake`: runs a plan through any `DatagramSink` and returns a `WakeReport` listing every attempt. Used by the synchronous `wake` command. |
| `src/engine/ProbeResult.h/.cpp` | `ProbeStatus` and `ProbeResult`: what one check observed, in words. |
| `src/engine/ServiceProbe.h/.cpp` | `Probe`, the asynchronous check interface; `TcpProbe` and `SunshineProbe`; `interpretServerInfo`, which reads Sunshine's XML; `makeProbe`. |
| `src/engine/QtWaker.h/.cpp` | `Waker` interface and `QtWaker`: resolves the destination asynchronously and sends one packet per timer tick, reusing the core's packet and plan. |
| `src/engine/Explain.h/.cpp` | `Observations` and `explainTimeout`, the "What happened" text; `formatDuration`; `looksLikeTailscale`. |
| `src/engine/Session.h/.cpp` | `Session`, the Wake & Connect state machine (Checking, Waking, Waiting, Launching, Done, Failed), its timeline events and outcomes; the `Launcher` interface with `NoLauncher`. |
| `src/engine/Profile.h/.cpp` | `Profile` and `ClientConfig`, the saved-computer value types; `toJson` and `profileFromJson` with full validation; `clientConfigError`, the one set of client rules; name helpers. |
| `src/engine/ProfileStore.h/.cpp` | Reads and writes the profiles file atomically, with the schema version, recovery of unreadable files and single-profile import and export. |
| `src/engine/AppLocator.h`, `src/mac/AppLocator.mm` | The platform interface and its macOS implementation in Objective-C++: Launch Services, `NSBundle`, `NSWorkspace`. The only file that speaks to AppKit. |
| `src/engine/ClientLauncher.h/.cpp` | `resolveLaunch`, which turns a `ClientConfig` and a host into the exact program and arguments or an explanation of what was searched; `ClientLauncher`, the `Launcher` that runs it. |
| `src/app/main.cpp` | Creates the application and the window. |
| `src/app/MainWindow.h/.cpp` | The window: the list of computers, the details pane, the run controls, the two facts, the timeline and the "What happened" box; menus for import, export, the example and help. Takes the factories for a run as a parameter, so tests drive it with fakes. |
| `src/app/ProfileEditorDialog.h/.cpp` | The add/edit dialog with the Wake, Check and Connect groups, their Test buttons and the same validation as the file format. |
| `src/cli/main.cpp` | Command dispatch and the synchronous `wake` command. |
| `src/cli/Flags.h/.cpp` | The small option parser shared by every command: values, switches, positional words, help anywhere. |
| `src/cli/SessionFlags.h/.cpp` | Turns flags into `SessionConfig` and `ClientConfig`, so `connect`, `launch` and `profile add` accept the same options. |
| `src/cli/ConnectCommand.h/.cpp`, `src/cli/LaunchCommand.h/.cpp`, `src/cli/ProfileCommand.h/.cpp` | The `check`, `connect`, `launch` and `profile` commands. |
| `examples/example-profile.json` | A complete fictional profile; bundled into the app for **Load Example**, and importable from the command line. |
| `tests/check.h`, `tests/test_core.cpp`, `tests/test_loopback.cpp` | Framework-free unit tests of the core and the loopback socket test (Milestone 1). |
| `tests/test_engine.cpp`, `tests/test_profiles.cpp`, `tests/test_launcher.cpp` | Qt Test suites for the engine, the profiles and the launcher. |
| `tests/test_gui.cpp` | Qt Test suite for the window and the editor on the offscreen platform, with fake probes, wakers and launchers and a temporary profiles folder. |
| `tests/cli_test.sh` | Runs the built tool through usage errors, `--dry-run`, the `profile` command on a temporary store, `launch` with harmless clients, and `check`/`connect` on loopback ports. |
| `CMakeLists.txt`, `Makefile` | CMake defines the libraries, the app bundle, the tool and the tests; the Makefile is the entry point and drives CMake. |

### Object Relationships

The core has no Qt and no event loop; it is plain C++17 that any program could link. The engine depends on the core and on Qt Core and Qt Network, plus one Objective-C++ file for the macOS application services. `Session` never names a concrete probe, waker or launcher: it receives factories that create them, so the tests hand it scripted fakes while the app and the tool hand it the real ones. `Profile` is a value type that converts to the `SessionConfig` the session runs and the `ClientConfig` the launcher resolves; `ProfileStore` is the only code that reads or writes the profiles file. The window is a thin layer: it starts sessions, probes, wakers and launchers and displays what they report, and it receives the factories for a run from outside, which is how the GUI tests run it without a network. Every asynchronous object emits one `finished` signal and is deleted with `deleteLater`, never from inside the signal handler.

## How to Compile & Run

Requirements: macOS 13 or later with the Xcode Command Line Tools, CMake 3.21 or later, Qt 6.7 or later (Core, Network, Widgets, Test) and optionally Ninja. With Homebrew:

```bash
brew install cmake ninja qt
```

Then, like the other projects in this repository, `make` does the rest. The Makefile finds Homebrew's tools even when they are not on your PATH yet.

```bash
cd WakeDeck
make                # configures CMake the first time, then builds build/WakeDeck.app, build/wakedeck-cli and the tests
make run-app        # opens the app
make test           # runs every test through ctest
make run ARGS="wake --mac 00:00:5E:00:53:2A --to 192.168.1.255 --dry-run"
```

Qt installed elsewhere: `make QT_PREFIX=/path/to/Qt/6.x/macos`. Another build directory: `make BUILD_DIR=/path/outside/the/repo`. The app built this way is unsigned and links the Qt frameworks from your Qt installation; the signed, self-contained disk image is Milestone 6.

Commands of the tool:

```
wakedeck-cli wake    --mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100] [--dry-run]
wakedeck-cli check   --host <host> [--port 47989] [--kind sunshine|tcp] [--timeout-ms 2000]
wakedeck-cli connect --check-host <host> [--check-port 47989] [--check sunshine|tcp] [--probe-timeout-ms 2000]
                     [--poll-s 3] [--timeout-s 120] [--mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100]]
                     [--client none|moonlight|custom] [--client-path <path>] [--launch open|stream] [--app <name>] [--client-args "<arguments>"]
wakedeck-cli connect --profile <name> [--poll-s 3] [--timeout-s 120]
wakedeck-cli launch  --profile <name> [--dry-run]
wakedeck-cli launch  --host <host> --client moonlight|custom [--client-path <path>] [--launch open|stream] [--app <name>]
                     [--client-args "<arguments>"] [--dry-run]
wakedeck-cli profile list | show <name> | remove <name> | import <file.json> [--replace] | export <name> <file.json>
wakedeck-cli profile add --name <name> --check-host <host> [check, wake and client options as for connect] [--notes <text>] [--replace]
wakedeck-cli <command> --help
```

Every `profile` form, `connect --profile` and `launch --profile` accept `--store <file>` to use another profiles file; the default is the same file the app uses, `~/Library/Application Support/WakeDeck/profiles.json`, so computers added in the app are available to the tool and vice versa. Profile names are matched without regard to case.

Exit codes. `wake`: 0 all packets sent, 1 usage error, 2 destination not resolved, 3 socket error or some packets not sent. `check`: 0 ready, 1 usage error, 2 not ready. `connect`: 0 ready, and the client launched if one is configured, 1 usage error, 2 timed out or profile not found, 3 wake request failed, 4 client not found or not launched. `launch`: 0 launched or would be, 1 usage error, 2 client not found or not started. `profile`: 0 done, 1 usage error, 2 not found, already exists, or a file problem.

## Verification

`make test` runs seven tests through ctest:

- `test_core` (66 checks): MAC parsing, packet layout including the full hex dump decoded back, port-list parsing, plan order, `sendWake` through a recording sink including a simulated failing port, numeric address resolution. No sockets, no name lookups.
- `test_loopback` (25 checks): a UDP listener on `127.0.0.1` receives three packets from the real `UdpSender` and checks every byte; then descriptor creation is blocked for a moment to check that a sender which cannot open its socket says so.
- `test_engine` (28 test functions, Qt Test): Sunshine reply parsing; the TCP and Sunshine probes against servers on `127.0.0.1`, including a fake Sunshine, a wrong service, an HTTP error and a silent server; the waker over loopback; the session with scripted fakes through every outcome; the explanation text, durations and the Tailscale heuristic.
- `test_profiles` (Qt Test): the JSON round trip, defaults, 32 invalid inputs each rejected by field name, the shipped example, and the store's save, load and recovery paths in a temporary folder.
- `test_launcher` (20 test functions, Qt Test): Launch Services lookups, a fake `.app` bundle in a temporary folder, `resolveLaunch` for every client kind including relative paths, launches of `/usr/bin/true` and of the fake bundle's script, the fake bundle opened through the system, a completion skipped for a deleted caller, and the session's launch step. No window ever appears.
- `test_gui` (16 test functions, Qt Test, offscreen platform): the window with an empty store, with saved computers, adding, removing and reloading, **Load Example** from the bundled resource; **Wake & Connect** clicked for real on a laid-out window with fake probes, wakers and launchers through ready-at-once, wake-then-ready, timeout with the **What happened** box, a missing client with **Locate client…** offered while the ready fact stays, cancel, **Check now** and **Wake only**; and the editor: a full profile round trip, the validation messages step by step, and the Moonlight streaming rule. Nothing is drawn on any screen and no real profiles file is touched.
- `tests/cli_test.sh` (110 checks): usage errors, help and `--dry-run` output of every command; the `profile` command against a temporary store; `launch` with `/usr/bin/true` and `/bin/echo`; and, when `python3` is available to open ports, end-to-end runs of `check`, `connect`, `connect --profile` and `connect` with a custom client against closed and open loopback ports, with a wake request addressed to `127.0.0.1`.

Not covered automatically: opening a real graphical application such as Moonlight, the look of the window on a real screen, the plain TCP check's own *timed out* path, the *unreachable* and *name not resolved* classifications of the real probes, and a real wake reaching a real PC. The author has run the full flow against a real PC from the command line: wake over Wake-on-LAN, Sunshine answering over Tailscale 26 seconds later.

## Current Scope

Milestone 5 of 6. Planned sequence: command-line wake prototype, readiness check and session, saved profiles, macOS app lookup and launch, Qt Widgets interface (this), packaging and release.

Not here yet, and worth knowing:

- The app has no icon and is not signed; it runs from the build folder. Milestone 6 produces the disk image.
- Stream mode runs Moonlight's executable directly, so the arguments always arrive even if Moonlight is already open; a second Moonlight window may then appear until the first is closed.
- IPv4 only. SecureOn passwords are not supported.
- macOS only, by design: the client launcher uses Launch Services and NSWorkspace.
- Binaries built against Homebrew's Qt run on the macOS version they were built on, or newer. The release build in Milestone 6 will use Qt's official binaries so the disk image runs on macOS 13 and later.

## Concepts Demonstrated

- Byte-level data with the C++17 standard library: `std::array<std::uint8_t, N>`, `std::optional` for parse results, `std::string_view` parsing, `std::from_chars`.
- Resource management with RAII: a socket descriptor owned by one object, released in its destructor, non-copyable.
- Interfaces separating policy from mechanism: `DatagramSink`, `Probe`, `Waker`, `Launcher`, with factories so tests substitute fakes, in the engine and in the window alike.
- Event-driven programming without threads: Qt signals and slots, timers, asynchronous sockets and name lookups; a state machine (`Session`) that advances only when something finishes; a window that never blocks.
- A serialization boundary: value types in memory, JSON on disk, validation at the edge with messages that name the field, a schema version for the future, atomic writes and recovery that never deletes the user's data.
- A platform layer: a plain C++ interface implemented once in Objective-C++, so AppKit, blocks and automatic reference counting stay in one file, and a system completion handler that hops back onto the Qt event loop before touching any object.
- A graphical interface kept thin: Qt Widgets, layouts and dialogs that only display what the engine reports, testable on the offscreen platform without a screen.
- BSD sockets and HTTP by hand on the test side: `getaddrinfo`, `socket`, `setsockopt(SO_BROADCAST)`, `sendto`, `bind`, `recvfrom`, a fake HTTP server built on `QTcpServer`.
- Build systems: a Makefile with pattern rules in Milestone 1, then CMake targets, static libraries, Qt's automatic moc, a second language in one target, a macOS application bundle and embedded resources, with the Makefile kept as the entry point.

## License

MIT, see [LICENSE](./LICENSE). WakeDeck uses the Qt framework under the LGPL v3, linked dynamically; the app's About box carries Qt's notice.
