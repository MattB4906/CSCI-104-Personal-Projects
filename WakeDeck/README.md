# WakeDeck

WakeDeck wakes a sleeping computer with a Wake-on-LAN "magic packet", waits until the service you need on it is actually ready, and then opens your client, for example Moonlight for a PC that runs Sunshine. It is written in C++17 for macOS.

**Status: Milestone 1 of 6, command-line prototype.** This folder holds the wake logic and a small command-line tool. The readiness check, the client launcher, saved profiles and the graphical interface (Qt Widgets) follow in later milestones; see [Current Scope](#current-scope).

## Purpose

Waking a PC remotely usually means a script that fires a magic packet and prints "sent", leaving you to guess when the machine is really usable. WakeDeck keeps two facts apart that are easy to confuse:

- **Wake request sent**: the packets left this computer. That is all a sender can ever know.
- **Service ready**: the program you want to use, such as Sunshine or Remote Desktop, answered.

The finished app will let anyone add their own computers in a graphical interface: how to wake each one, which address and service to check, and which installed client to open. Personal network settings never live in this repository. Today they are passed on the command line; later they are stored in the app's own settings.

## Features

### Available now (Milestone 1)

- Builds the 102-byte magic packet from a MAC address written as `00:00:5E:00:53:2A`, `00-00-5E-00-53-2A` or `00005E00532A`. Anything else is rejected before a packet exists.
- Sends it over UDP to an IPv4 address or host name, including broadcast addresses, to a list of ports in order, a configurable number of times per port, with a pause between packets. Defaults: port 9 then port 7, five packets each, 100 ms apart.
- `--dry-run` prints the plan and the packet bytes without sending anything and without resolving the destination, so it never touches the network.
- Reports honestly. "Sent" means the packets left this computer, nothing more, and every failed send is listed.
- Tests cover the parsing, the packet layout, the send order and the real socket code, the last one over the loopback interface only.

### Planned

- Readiness check: a TCP port, or Sunshine's `/serverinfo` answer, polled with a timeout and a plain-language explanation of what was observed when it fails.
- Client launch: Moonlight found automatically, or any application you choose.
- Saved profiles and a Qt Widgets interface: select a computer, click **Wake & Connect**.
- A signed and notarized disk image.

## How It Works

A magic packet is a UDP datagram of 102 bytes: six `0xFF` bytes followed by the target's MAC address repeated sixteen times. A sleeping PC has no working IP address. Its network card watches the wire for that pattern and powers the machine on, so the packet must arrive on the PC's own network segment, which normally means a broadcast on that LAN.

- **Same network.** Your Mac and the PC share a Wi-Fi or Ethernet network: send to the network's broadcast address, for example `192.168.1.255`, and the card sees it. Most Wi-Fi adapters cannot wake a powered-off machine; Ethernet usually can.
- **From elsewhere.** The internet and VPNs route packets to running hosts and do not carry LAN broadcasts, and a sleeping PC is not on your VPN at all. Something always on inside the home network has to emit the broadcast for you: a router with a wake feature or a port forward to the PC's reserved address, or a small always-on device (a Raspberry Pi, a NAS) that relays the packet. WakeDeck sends to whatever destination you configure and does not assume how it reaches the PC.

Once the PC is up, a VPN such as Tailscale gives it a stable address from anywhere. That address is what the readiness check and Moonlight will use in later milestones.

`wakedeck-cli wake` performs the first half of **Wake & Connect**: it builds the packet, resolves the destination, sends the plan and reports what left the machine, with a timestamp.

## Class & File Structure

| File | Role |
|---|---|
| `src/core/MacAddress.h/.cpp` | Parses and formats a 48-bit hardware address. `parse` returns `std::optional`, so invalid text never produces a half-built object. |
| `src/core/MagicPacket.h/.cpp` | Builds the 102-byte frame from a `MacAddress`, plus a hex dump for diagnostics. |
| `src/core/WakePlan.h/.cpp` | `WakeTarget` (destination, ports, repeat, interval), `expandWakePlan`, which turns it into the ordered list of sends, and `parsePortList`. |
| `src/core/UdpSender.h/.cpp` | `resolveIPv4`, the `DatagramSink` interface, and `UdpSender`, the BSD-socket implementation. The only file that talks to the operating system. |
| `src/core/WakeSender.h/.cpp` | `sendWake`: runs a plan through any `DatagramSink` and returns a `WakeReport` listing every attempt. |
| `src/cli/main.cpp` | Argument parsing, exit codes and output of `wakedeck-cli`. |
| `tests/check.h` | A dozen lines of test helpers, no framework. |
| `tests/test_core.cpp` | Unit tests with a recording sink instead of sockets. |
| `tests/test_loopback.cpp` | Sends through the real `UdpSender` to a listener on 127.0.0.1 and checks the bytes that arrive; also forces the "socket cannot open" path. |
| `tests/cli_test.sh` | Runs the built tool with `--dry-run` and invalid inputs, checking exit codes and output. |
| `Makefile` | `make`, `make test`, `make run`, `make clean`. |

### Object Relationships

`main.cpp` depends only on the core. `sendWake` depends on the `DatagramSink` interface, not on `UdpSender`, so the tests substitute a recorder and the future Qt app can substitute its own transport. `UdpSender` owns its socket and closes it in the destructor (RAII), and refuses to be copied so the descriptor cannot be closed twice.

## How to Compile & Run

Requirements: macOS with the Xcode Command Line Tools (`c++` and `make`). No other dependencies at this milestone.

```bash
cd WakeDeck
make                # builds build/wakedeck-cli
make test           # builds and runs the tests
make run ARGS="wake --mac 00:00:5E:00:53:2A --to 192.168.1.255 --dry-run"
```

Or by hand, like the other projects in this repository:

```bash
c++ -std=c++17 -Wall -Wextra -pedantic -Isrc src/core/*.cpp src/cli/*.cpp -o wakedeck-cli
```

Usage:

```
wakedeck-cli wake --mac <address> --to <host> [--ports 9,7] [--repeat 5] [--interval-ms 100] [--dry-run]
wakedeck-cli --help | --version
```

Example with placeholder values (replace them with your own):

```bash
./build/wakedeck-cli wake --mac 00:00:5E:00:53:2A --to 192.168.1.255
```

```
Target MAC    00:00:5E:00:53:2A
Destination   192.168.1.255
Plan          10 packets: port 9 x5, port 7 x5, 100 ms apart

Wake request sent: 10 packets left this computer for 192.168.1.255 at 22:41:05.
This confirms the packets were sent, not that the computer is awake.
Check the service you need (for example Sunshine) before connecting.
```

Exit codes: `0` all packets sent, `1` usage error, `2` destination not resolved, `3` socket error or some packets not sent.

Tip: keep your own values out of this repository. A shell alias or a small script in your home folder can call `wakedeck-cli` with your MAC address and destination.

## Verification

`make test` builds and runs two executables and one script:

- `test_core`: MAC parsing (accepted and rejected forms), packet layout (sync bytes, sixteen repetitions, the full hex dump decoded back), port-list parsing, plan expansion order (port 9 five times, then port 7 five times), `sendWake` through a recording sink including a simulated failing port, and numeric address resolution. No name lookups, no sockets.
- `test_loopback`: binds a UDP listener on `127.0.0.1`, sends three packets through the real `UdpSender`, and checks that each one arrives as exactly 102 bytes with the expected content. It then blocks descriptor creation for a moment and checks that a sender which cannot open its socket reports the failure. No packet leaves the machine.
- `tests/cli_test.sh`: runs the built tool through its usage errors and `--dry-run` paths and checks exit codes and output text. Exit codes 2 and 3 need a real network and are not covered automatically.

All three print `N/N checks passed` and return a non-zero exit code on any failure, so `make test` fails loudly.

## Current Scope

Milestone 1 of 6. Planned sequence: command-line wake prototype (this), readiness check and session state machine, saved profiles, macOS app lookup and launch, Qt Widgets interface, packaging and release.

Not here yet, and worth knowing:

- The tool cannot tell you the PC woke up. Only the readiness check of Milestone 2 can.
- IPv4 only. SecureOn passwords are not supported.
- macOS only. The core is plain C++17 and portable, but only macOS is built and tested.

## Concepts Demonstrated

- Byte-level data with the C++17 standard library: `std::array<std::uint8_t, N>`, `std::optional` for parse results, `std::string_view` parsing, `std::from_chars`.
- Resource management with RAII: a socket descriptor owned by one object, released in its destructor, non-copyable.
- An interface (`DatagramSink`) separating the policy (`sendWake`) from the mechanism (`UdpSender`), which is what makes the logic testable without a network.
- BSD sockets: `getaddrinfo`, `socket`, `setsockopt(SO_BROADCAST)`, `sendto`, and on the test side `bind`, `getsockname`, `recvfrom` with a receive timeout.
- A Makefile with pattern rules, automatic header dependencies (`-MMD -MP`) and an out-of-tree build directory.

## License

MIT, see [LICENSE](./LICENSE). Later milestones add the Qt framework (LGPL v3), linked dynamically, with its notices.
