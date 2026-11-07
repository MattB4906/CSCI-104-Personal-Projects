# WakeDeck

WakeDeck wakes a sleeping computer with a Wake-on-LAN "magic packet", waits until the service you need on it is actually ready, and then opens your client, for example Moonlight for a PC that runs Sunshine. It is written in C++17 with Qt 6 for macOS.

**Status: Milestone 3 of 6, saved profiles.** The wake logic, the service checks, the Wake & Connect session and saved profiles exist and are tested, all usable from the command line. The client launcher and the graphical interface (Qt Widgets) follow in the next milestones; see [Current Scope](#current-scope).

## Purpose

Waking a PC remotely usually means a script that fires a magic packet and prints "sent", leaving you to guess when the machine is really usable. WakeDeck keeps two facts apart that are easy to confuse:

- **Wake request sent**: the packets left this computer. That is all a sender can ever know.
- **Service ready**: the program you want to use, such as Sunshine or Remote Desktop, answered.

The finished app will let anyone add their own computers in a graphical interface: how to wake each one, which address and service to check, and which installed client to open. Personal network settings never live in this repository: they are saved in a profiles file in your own Library folder, or passed on the command line.

## Features

### Available now (Milestone 3)

- **Profiles.** A saved computer has a name, how to wake it, what to check, how long to wait, which client to open, and notes. Profiles are stored as JSON in `~/Library/Application Support/WakeDeck/profiles.json`, with a schema version so future versions can convert the file. `profile add`, `list`, `show`, `remove`, `import` and `export` manage them; `connect --profile <name>` runs one. A file that cannot be read is moved aside and kept, never deleted, and a file written by a newer WakeDeck is refused and left untouched.
- **Example profile.** `examples/example-profile.json` is a fictional, complete profile to import and edit. Its values come from ranges reserved for documentation.
- `wake`: builds the 102-byte magic packet from a MAC address written as `00:00:5E:00:53:2A`, `00-00-5E-00-53-2A` or `00005E00532A` and sends it over UDP to an address or host name, including broadcast addresses, to a list of ports in order, a configurable number of times per port. Defaults: port 9 then port 7, five packets each, 100 ms apart. `--dry-run` shows the plan and the bytes and touches nothing.
- `check`: asks once whether a service is ready. The Sunshine check fetches `/serverinfo` over HTTP and reads Sunshine's state; the TCP check tries a connection to a port (Remote Desktop, SSH, anything). Every answer is classified: ready, not ready, refused, timed out, unreachable, name not resolved.
- `connect`: the Wake & Connect run. It checks the service; if the service is not ready and a wake is configured, it sends the wake request, then keeps checking every few seconds until the service answers or the time limit runs out. Each step is a time-stamped line, "Wake request sent" and "ready" are separate events, and a timeout ends with a plain-language **What happened** summary and likely causes.
- Nothing blocks: checks, name lookups and the pauses between packets all run on the event loop, which is what the graphical app will need.
- Reports honestly: "sent" means the packets left this computer, and every failed send or check is listed.
- Tests cover the packet logic, the real socket code over the loopback interface only, the probes against local servers (including a fake Sunshine), the waker, every path of the session with fakes, the explanation text, the profile format with its validation and recovery paths, and the command-line behaviour end to end on loopback.

### Planned

- Client launch: Moonlight found automatically, or any application you choose; the settings are already part of a profile (Milestone 4).
- A Qt Widgets interface: select a computer, click **Wake & Connect** (Milestone 5).
- A signed and notarized disk image (Milestone 6).

## How It Works

A magic packet is a UDP datagram of 102 bytes: six `0xFF` bytes followed by the target's MAC address repeated sixteen times. A sleeping PC has no working IP address. Its network card watches the wire for that pattern and powers the machine on, so the packet must arrive on the PC's own network segment, which normally means a broadcast on that LAN.

- **Same network.** Your Mac and the PC share a Wi-Fi or Ethernet network: send to the network's broadcast address, for example `192.168.1.255`, and the card sees it. Most Wi-Fi adapters cannot wake a powered-off machine; Ethernet usually can.
- **From elsewhere.** The internet and VPNs route packets to running hosts and do not carry LAN broadcasts, and a sleeping PC is not on your VPN at all. Something always on inside the home network has to emit the broadcast for you: a router with a wake feature or a port forward to the PC's reserved address, or a small always-on device (a Raspberry Pi, a NAS) that relays the packet. WakeDeck sends to whatever destination you configure and does not assume how it reaches the PC.

Once the PC is up, a VPN such as Tailscale gives it a stable address from anywhere. That address is what the readiness check uses, and what Moonlight will use.

### Wake & Connect, step by step

1. **Check.** Ask the service once. For Sunshine that is an HTTP `GET /serverinfo` on port 47989, the same request Moonlight uses to show a PC as online; the reply is XML with the host name and a state such as `SUNSHINE_SERVER_FREE`, and a reply that is not Sunshine's counts as *not ready*. For any other service it is a plain TCP connection to its port.
2. **Wake.** If the service did not answer and a wake is configured, send the magic packets. "Wake request sent" is reported as its own event.
3. **Wait.** Check again every few seconds (default 3 s) until the service answers or the limit (default 2 minutes) runs out. Each check is classified:
   - *refused*: something at that address refused the connection, usually the computer itself, awake but with the service not listening yet;
   - *timed out* or *unreachable*: nothing answered, so the computer is still off, or not reachable from here;
   - *name not resolved*: the host name could not be turned into an address, often a VPN that is not connected;
   - *not ready*: something answered on the port, but not with the expected reply.
4. **Launch.** Open the client. This step arrives in Milestone 4; today the session reports that nothing was launched.

When the wait runs out, the summary says exactly what was observed, for example "40 checks, 40 timed out", when the wake request was sent, and what that pattern usually means. If the host looks like a Tailscale address it says so. The text never claims the computer is on or off; it says what answered and what did not.

A typical run, with placeholder values:

```
22:41:05  Checking Sunshine at gaming-pc.example.net:47989
22:41:07  Not ready: timed out (no answer within 2000 ms)
22:41:07  Wake request sent: 10 packets to 192.168.1.255 (ports 9 x5, 7 x5)
22:41:07  Waiting for Sunshine at gaming-pc.example.net:47989 (up to 2:00)
22:41:12  Check 2: timed out (waiting 0:07 of 2:00)
22:41:17  Check 3: timed out (waiting 0:12 of 2:00)
22:41:48  Sunshine at gaming-pc.example.net:47989 ready: hostname LIVING-ROOM-PC, state free (after 0:43)
22:41:48  No client configured in this milestone; nothing launched.

Result: ready after wake
Service ready 0:43 after the wake request.
```

### Saved profiles

A profile is the whole configuration of one computer. The file is plain JSON, one object per profile inside a `profiles` list, and looks like the example below (shortened). Every field has a default except the name and the check host, and every value is validated when the file is read, so a mistyped MAC address, port or field name is reported by name instead of producing a half-configured computer. The client settings follow one set of rules whether they come from a file or from `profile add`: a custom client needs a path, streaming needs the name of the app, and those options apply to the client they belong to.

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

A few rules protect the file. It is written atomically, so a crash while saving leaves the previous version intact. A file that is not valid JSON, or has no profiles list, is renamed to `profiles.json.corrupt-<date>` (never overwriting an earlier one) and WakeDeck starts with an empty list, telling you where the old file went. A file whose `schemaVersion` is newer than this WakeDeck understands is refused and not modified, and so is an exported single profile mistaken for a store. Single unreadable entries are skipped with a warning and one copy of the original file is kept beside it. `profile export` refuses to write over the store itself.

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
| `src/engine/Profile.h/.cpp` | `Profile` and `ClientConfig`, the saved-computer value types; `toJson` and `profileFromJson` with full validation; name helpers. |
| `src/engine/ProfileStore.h/.cpp` | Reads and writes the profiles file atomically, with the schema version, recovery of unreadable files and single-profile import and export. |
| `src/cli/main.cpp` | Command dispatch and the synchronous `wake` command. |
| `src/cli/Flags.h/.cpp` | The small option parser shared by every command: values, switches, positional words, help anywhere. |
| `src/cli/SessionFlags.h/.cpp` | Turns flags into `SessionConfig` and `ClientConfig`, so `connect` and `profile add` accept the same options. |
| `src/cli/ConnectCommand.h/.cpp` | The `check` and `connect` commands on Qt's event loop, including `connect --profile`. |
| `src/cli/ProfileCommand.h/.cpp` | The `profile` command: list, show, add, remove, import, export. |
| `examples/example-profile.json` | A complete fictional profile to import and edit. |
| `tests/check.h`, `tests/test_core.cpp`, `tests/test_loopback.cpp` | Framework-free unit tests of the core and the loopback socket test (Milestone 1). |
| `tests/test_engine.cpp` | Qt Test suite for the engine: probes against local servers, the waker over loopback, the session with fakes, the explanation text. |
| `tests/test_profiles.cpp` | Qt Test suite for profiles: JSON round trip, defaults, 20 invalid-input cases, the store's save, load and recovery paths, import and export, the shipped example. |
| `tests/cli_test.sh` | Runs the built tool through usage errors, `--dry-run`, the `profile` command on a temporary store, and `check`/`connect` on loopback ports. |
| `CMakeLists.txt`, `Makefile` | CMake defines the libraries, the tool and the tests; the Makefile is the entry point and drives CMake. |

### Object Relationships

The core has no Qt and no event loop; it is plain C++17 that any program could link. The engine depends on the core and on Qt Core and Qt Network. `Session` never names a concrete probe, waker or launcher: it receives factories that create them, so the tests hand it scripted fakes while the tool hands it the real ones. `Profile` is a value type that converts to the `SessionConfig` the session runs; `ProfileStore` is the only code that reads or writes the profiles file. Every asynchronous object emits one `finished` signal and is deleted with `deleteLater`, never from inside the signal handler. The command-line tool depends on the engine only.

## How to Compile & Run

Requirements: macOS 13 or later with the Xcode Command Line Tools, CMake 3.21 or later, Qt 6.7 or later (Core, Network, Test) and optionally Ninja. With Homebrew:

```bash
brew install cmake ninja qt
```

Then, like the other projects in this repository, `make` does the rest. The Makefile finds Homebrew's tools even when they are not on your PATH yet.

```bash
cd WakeDeck
make                # configures CMake the first time, then builds build/wakedeck-cli and the tests
make test           # runs every test through ctest
make run ARGS="wake --mac 00:00:5E:00:53:2A --to 192.168.1.255 --dry-run"
```

Qt installed elsewhere: `make QT_PREFIX=/path/to/Qt/6.x/macos`. Another build directory: `make BUILD_DIR=/path/outside/the/repo`.

Commands:

```
wakedeck-cli wake    --mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100] [--dry-run]
wakedeck-cli check   --host <host> [--port 47989] [--kind sunshine|tcp] [--timeout-ms 2000]
wakedeck-cli connect --check-host <host> [--check-port 47989] [--check sunshine|tcp] [--probe-timeout-ms 2000]
                     [--poll-s 3] [--timeout-s 120] [--mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100]]
wakedeck-cli connect --profile <name> [--poll-s 3] [--timeout-s 120]
wakedeck-cli profile list | show <name> | remove <name> | import <file.json> [--replace] | export <name> <file.json>
wakedeck-cli profile add --name <name> --check-host <host> [check and wake options as for connect]
                         [--client none|moonlight|custom] [--client-path <path>] [--launch open|stream] [--app <name>]
                         [--notes <text>] [--replace]
wakedeck-cli <command> --help
```

Every `profile` form and `connect --profile` accept `--store <file>` to use another profiles file; the default is `~/Library/Application Support/WakeDeck/profiles.json`. Profile names are matched without regard to case.

A first session, with placeholder values (replace them with your own):

```bash
./build/wakedeck-cli profile import examples/example-profile.json
./build/wakedeck-cli profile add --name "Gaming PC" --check-host gaming-pc.example.net \
    --mac 00:00:5E:00:53:2A --to 192.168.1.255 --client moonlight --launch stream --app Desktop
./build/wakedeck-cli profile list
./build/wakedeck-cli connect --profile "Gaming PC"
```

Exit codes. `wake`: 0 all packets sent, 1 usage error, 2 destination not resolved, 3 socket error or some packets not sent. `check`: 0 ready, 1 usage error, 2 not ready. `connect`: 0 ready, 1 usage error, 2 timed out or profile not found, 3 wake request failed, 4 client not launched. `profile`: 0 done, 1 usage error, 2 not found, already exists, or a file problem.

## Verification

`make test` runs five tests through ctest:

- `test_core` (66 checks): MAC parsing, packet layout including the full hex dump decoded back, port-list parsing, plan order, `sendWake` through a recording sink including a simulated failing port, numeric address resolution. No sockets, no name lookups.
- `test_loopback` (25 checks): a UDP listener on `127.0.0.1` receives three packets from the real `UdpSender` and checks every byte; then descriptor creation is blocked for a moment to check that a sender which cannot open its socket says so.
- `test_engine` (28 test functions, Qt Test): Sunshine reply parsing, including the lenient case without a `status_code`; the TCP and Sunshine probes against servers on `127.0.0.1`, including a fake Sunshine that answers with real XML, a wrong service, an HTTP error and a silent server that makes the Sunshine check time out; the waker over loopback, byte for byte, plus its refusal of an empty plan; the session with scripted fakes through every outcome (ready at once, wake then ready, timeout with explanation, wake failure, launch failure and success, no wake configured, cancel, and a "keep waiting" extension); the explanation text, durations and the Tailscale heuristic.
- `test_profiles` (Qt Test): the JSON round trip of a full profile, defaults for a minimal one, 31 invalid inputs each rejected with a reason that names the field (bad values, inconsistent client settings, mistyped field names), the shipped example, and the store in a temporary folder: save and load with folders created and no temporary files left, a missing file, a corrupt file moved aside twice within the same second, an invalid schema version, a single-profile file refused and left untouched, a file without a profiles list, a newer schema refused and left untouched, invalid entries skipped with exactly one copy kept, import and export, and the default path under Application Support.
- `tests/cli_test.sh` (84 checks): usage errors, help and `--dry-run` output of every command; the `profile` command against a temporary store (add, duplicate names, `--replace`, list, show, export including the refusal to export over the store, import of the example, a single-profile file offered as a store, remove, recovery of a corrupt store twice in a row); and, when `python3` is available to open ports, end-to-end runs of `check`, `connect` and `connect --profile` against closed and open loopback ports, with a wake request addressed to `127.0.0.1`.

Not covered automatically, because they need a silent or absent host: the plain TCP check's own *timed out* path, the *unreachable* and *name not resolved* classifications of the real probes, and a real wake reaching a real PC. The session's handling of those results is covered through the fakes.

## Current Scope

Milestone 3 of 6. Planned sequence: command-line wake prototype, readiness check and session, saved profiles (this), macOS app lookup and launch, Qt Widgets interface, packaging and release.

Not here yet, and worth knowing:

- No client launch and no graphical interface yet; the client settings in a profile are stored but not acted on.
- IPv4 only. SecureOn passwords are not supported.
- macOS only. The core is plain C++17 and portable, the engine uses only portable Qt modules, but only macOS is built and tested.
- Binaries built against Homebrew's Qt run on the macOS version they were built on, or newer. The release build in Milestone 6 will use Qt's official binaries so the disk image runs on macOS 13 and later.

## Concepts Demonstrated

- Byte-level data with the C++17 standard library: `std::array<std::uint8_t, N>`, `std::optional` for parse results, `std::string_view` parsing, `std::from_chars`.
- Resource management with RAII: a socket descriptor owned by one object, released in its destructor, non-copyable.
- Interfaces separating policy from mechanism: `DatagramSink`, `Probe`, `Waker`, `Launcher`, with factories so tests substitute fakes.
- Event-driven programming without threads: Qt signals and slots, timers, asynchronous sockets and name lookups; a state machine (`Session`) that advances only when something finishes.
- A serialization boundary: value types in memory, JSON on disk, validation at the edge with messages that name the field, a schema version for the future, atomic writes and recovery that never deletes the user's data.
- BSD sockets and HTTP by hand on the test side: `getaddrinfo`, `socket`, `setsockopt(SO_BROADCAST)`, `sendto`, `bind`, `recvfrom`, a fake HTTP server built on `QTcpServer`.
- Build systems: a Makefile with pattern rules in Milestone 1, then CMake targets, static libraries and Qt's automatic moc, with the Makefile kept as the entry point.

## License

MIT, see [LICENSE](./LICENSE). WakeDeck uses the Qt framework under the LGPL v3, linked dynamically; the finished app will carry Qt's notices in its About box.
