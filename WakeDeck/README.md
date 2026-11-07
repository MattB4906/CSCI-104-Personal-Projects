# WakeDeck

WakeDeck wakes a sleeping computer with a Wake-on-LAN "magic packet", waits until the service you need on it is actually ready, and then opens your client, for example Moonlight for a PC that runs Sunshine. It is written in C++17 with Qt 6 for macOS.

**Status: Milestone 2 of 6, readiness check and Wake & Connect on the command line.** The wake logic, the service checks and the Wake & Connect session exist and are tested; saved profiles, the client launcher and the graphical interface (Qt Widgets) follow in later milestones; see [Current Scope](#current-scope).

## Purpose

Waking a PC remotely usually means a script that fires a magic packet and prints "sent", leaving you to guess when the machine is really usable. WakeDeck keeps two facts apart that are easy to confuse:

- **Wake request sent**: the packets left this computer. That is all a sender can ever know.
- **Service ready**: the program you want to use, such as Sunshine or Remote Desktop, answered.

The finished app will let anyone add their own computers in a graphical interface: how to wake each one, which address and service to check, and which installed client to open. Personal network settings never live in this repository. Today they are passed on the command line; later they are stored in the app's own settings.

## Features

### Available now (Milestone 2)

- `wake`: builds the 102-byte magic packet from a MAC address written as `00:00:5E:00:53:2A`, `00-00-5E-00-53-2A` or `00005E00532A` and sends it over UDP to an address or host name, including broadcast addresses, to a list of ports in order, a configurable number of times per port. Defaults: port 9 then port 7, five packets each, 100 ms apart. `--dry-run` shows the plan and the bytes and touches nothing.
- `check`: asks once whether a service is ready. The Sunshine check fetches `/serverinfo` over HTTP and reads Sunshine's state; the TCP check tries a connection to a port (Remote Desktop, SSH, anything). Every answer is classified: ready, not ready, refused, timed out, unreachable, name not resolved.
- `connect`: the Wake & Connect run. It checks the service; if the service is not ready and a wake is configured, it sends the wake request, then keeps checking every few seconds until the service answers or the time limit runs out. Each step is a time-stamped line, "Wake request sent" and "ready" are separate events, and a timeout ends with a plain-language **What happened** summary and likely causes.
- Nothing blocks: checks, name lookups and the pauses between packets all run on the event loop, which is what the graphical app will need.
- Reports honestly: "sent" means the packets left this computer, and every failed send or check is listed.
- Tests cover the packet logic, the real socket code over the loopback interface only, the probes against local servers (including a fake Sunshine), the waker, every path of the session with fakes, the explanation text and the command-line behaviour.

### Planned

- Saved profiles: name, wake settings, check, client, in a JSON file in the app's own folder (Milestone 3).
- Client launch: Moonlight found automatically, or any application you choose (Milestone 4).
- A Qt Widgets interface: select a computer, click **Wake & Connect** (Milestone 5).
- A signed and notarized disk image (Milestone 6).

## How It Works

A magic packet is a UDP datagram of 102 bytes: six `0xFF` bytes followed by the target's MAC address repeated sixteen times. A sleeping PC has no working IP address. Its network card watches the wire for that pattern and powers the machine on, so the packet must arrive on the PC's own network segment, which normally means a broadcast on that LAN.

- **Same network.** Your Mac and the PC share a Wi-Fi or Ethernet network: send to the network's broadcast address, for example `192.168.1.255`, and the card sees it. Most Wi-Fi adapters cannot wake a powered-off machine; Ethernet usually can.
- **From elsewhere.** The internet and VPNs route packets to running hosts and do not carry LAN broadcasts, and a sleeping PC is not on your VPN at all. Something always on inside the home network has to emit the broadcast for you: a router with a wake feature or a port forward to the PC's reserved address, or a small always-on device (a Raspberry Pi, a NAS) that relays the packet. WakeDeck sends to whatever destination you configure and does not assume how it reaches the PC.

Once the PC is up, a VPN such as Tailscale gives it a stable address from anywhere. That address is what the readiness check uses, and what Moonlight will use.

### Wake & Connect, step by step

1. **Check.** Ask the service once. For Sunshine that is an HTTP `GET /serverinfo` on port 47989, the same request Moonlight uses to show a PC as online; the reply is XML with the host name and a state such as `SUNSHINE_SERVER_FREE`. For any other service it is a plain TCP connection to its port.
2. **Wake.** If the service did not answer and a wake is configured, send the magic packets. "Wake request sent" is reported as its own event.
3. **Wait.** Check again every few seconds (default 3 s) until the service answers or the limit (default 2 minutes) runs out. Each check is classified:
   - *refused*: the computer is up and refused the connection, so the service is not listening there yet;
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
| `src/cli/main.cpp` | Command dispatch and the synchronous `wake` command. |
| `src/cli/ConnectCommand.h/.cpp` | The `check` and `connect` commands on Qt's event loop. |
| `tests/check.h`, `tests/test_core.cpp`, `tests/test_loopback.cpp` | Framework-free unit tests of the core and the loopback socket test (Milestone 1). |
| `tests/test_engine.cpp` | Qt Test suite for the engine: probes against local servers, the waker over loopback, the session with fakes, the explanation text. |
| `tests/cli_test.sh` | Runs the built tool through its usage errors and `--dry-run` paths and checks exit codes and output. |
| `CMakeLists.txt`, `Makefile` | CMake defines the libraries, the tool and the tests; the Makefile is the entry point and drives CMake. |

### Object Relationships

The core has no Qt and no event loop; it is plain C++17 that any program could link. The engine depends on the core and on Qt Core and Qt Network. `Session` never names a concrete probe, waker or launcher: it receives factories that create them, so the tests hand it scripted fakes while the tool hands it the real ones. Every asynchronous object emits one `finished` signal and is deleted with `deleteLater`, never from inside the signal handler. The command-line tool depends on the engine only.

## How to Compile & Run

Requirements: macOS 13 or later with the Xcode Command Line Tools, CMake 3.21 or later, Qt 6 (Core, Network, Test) and optionally Ninja. With Homebrew:

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
wakedeck-cli <command> --help
```

Examples with placeholder values (replace them with your own):

```bash
./build/wakedeck-cli wake --mac 00:00:5E:00:53:2A --to 192.168.1.255
./build/wakedeck-cli check --host gaming-pc.example.net
./build/wakedeck-cli connect --check-host gaming-pc.example.net --mac 00:00:5E:00:53:2A --to 192.168.1.255
```

Exit codes. `wake`: 0 all packets sent, 1 usage error, 2 destination not resolved, 3 socket error or some packets not sent. `check`: 0 ready, 1 usage error, 2 not ready. `connect`: 0 ready, 1 usage error, 2 timed out, 3 wake request failed, 4 client not launched.

Tip: keep your own values out of this repository. A shell alias or a small script in your home folder can call `wakedeck-cli` with your MAC address, destination and host.

## Verification

`make test` runs four tests through ctest:

- `test_core` (66 checks): MAC parsing, packet layout including the full hex dump decoded back, port-list parsing, plan order, `sendWake` through a recording sink including a simulated failing port, numeric address resolution. No sockets, no name lookups.
- `test_loopback` (25 checks): a UDP listener on `127.0.0.1` receives three packets from the real `UdpSender` and checks every byte; then descriptor creation is blocked for a moment to check that a sender which cannot open its socket says so.
- `test_engine` (24 test functions, Qt Test): Sunshine reply parsing; the TCP and Sunshine probes against servers on `127.0.0.1`, including a fake Sunshine that answers with real XML, a wrong service and an HTTP error; the waker over loopback, byte for byte; the session with scripted fakes through every outcome (ready at once, wake then ready, timeout with explanation, wake failure, no wake configured, cancel); the explanation text, durations and the Tailscale heuristic.
- `tests/cli_test.sh` (38 checks): usage errors, help and `--dry-run` output of all three commands.

Not covered automatically, because they need a silent or absent host: the *timed out*, *unreachable* and *name not resolved* classifications of the real probes, and a real wake reaching a real PC. The session's handling of those results is covered through the fakes.

## Current Scope

Milestone 2 of 6. Planned sequence: command-line wake prototype, readiness check and session (this), saved profiles, macOS app lookup and launch, Qt Widgets interface, packaging and release.

Not here yet, and worth knowing:

- No saved profiles, no client launch, no graphical interface yet; the tool is how the engine is exercised today.
- IPv4 only. SecureOn passwords are not supported.
- macOS only. The core is plain C++17 and portable, the engine uses only portable Qt modules, but only macOS is built and tested.
- Binaries built against Homebrew's Qt run on the macOS version they were built on, or newer. The release build in Milestone 6 will use Qt's official binaries so the disk image runs on macOS 13 and later.

## Concepts Demonstrated

- Byte-level data with the C++17 standard library: `std::array<std::uint8_t, N>`, `std::optional` for parse results, `std::string_view` parsing, `std::from_chars`.
- Resource management with RAII: a socket descriptor owned by one object, released in its destructor, non-copyable.
- Interfaces separating policy from mechanism: `DatagramSink`, `Probe`, `Waker`, `Launcher`, with factories so tests substitute fakes.
- Event-driven programming without threads: Qt signals and slots, timers, asynchronous sockets and name lookups; a state machine (`Session`) that advances only when something finishes.
- BSD sockets and HTTP by hand on the test side: `getaddrinfo`, `socket`, `setsockopt(SO_BROADCAST)`, `sendto`, `bind`, `recvfrom`, a fake HTTP server built on `QTcpServer`.
- Build systems: a Makefile with pattern rules in Milestone 1, then CMake targets, static libraries and Qt's automatic moc, with the Makefile kept as the entry point.

## License

MIT, see [LICENSE](./LICENSE). WakeDeck uses the Qt framework under the LGPL v3, linked dynamically; the finished app will carry Qt's notices in its About box.
