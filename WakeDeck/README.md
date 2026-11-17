# WakeDeck

WakeDeck wakes a sleeping computer with a Wake-on-LAN "magic packet", waits until the service you need on it is actually ready, and then opens your client, for example Moonlight for a PC that runs Sunshine. It is written in C++17 with Qt 6 for macOS.

**Status: version 1.3.1: the six milestones of 1.0.0, Quit When Client Closes from 1.1.0, the reliability and waiting features of 1.2.0, the daily-use, setup, and status features of 1.3.0, and macOS menu role & badge cleanup in 1.3.1.** WakeDeck is a signed, notarized Mac app: a list of your computers, one **Wake & Connect** button, and a timeline that keeps "wake request sent" and "service ready" apart. While it waits it sends the wake request again, can send it to every local network of the Mac, shows a progress bar in the window and on the Dock icon, says how long the computer usually takes, and notifies you when the run ends while you are in another application. Version 1.3.0 adds a right-click Dock menu for one-click connections, `wakedeck://` URL scheme integration for Apple Shortcuts and Stream Deck, computer duplication and reordering in the list, Tailscale peer picking and Moonlight preference import, and on-demand batch checking of all computers (`Cmd+Shift+K`). Version 1.3.1 fixes the macOS `Cmd+Q` application menu role assignment and cleans up sidebar badges on edit/rename/delete. When the client it opened closes, WakeDeck quits too, unless you turn that off. A command-line tool ships inside the app for scripts. See [Install](#install) to use it, [New in version 1.3.1](#new-in-version-131) for the latest changes and [Current Scope](#current-scope) for what it does not do.

## Install

Requirements: a Mac with Apple Silicon running macOS 13 Ventura or later.

1. Download the newest disk image, `WakeDeck-<version>.dmg`, from this repository's Releases page and open it.
2. Drag **WakeDeck** onto **Applications**.
3. Open WakeDeck from Applications. It is signed with a Developer ID and notarized by Apple, so it opens like any other downloaded app, with the usual one-time question about opening an app from the internet.
4. Click **Add…** and describe your computer, or choose **File ▸ Load Example** to see a complete, fictional one first. The first time WakeDeck checks a computer on your own network, macOS may ask whether it may find devices on your local network; allow it, or wake requests and checks on the local network cannot work.

The command-line tool is inside the app. To use it from Terminal:

```bash
/Applications/WakeDeck.app/Contents/MacOS/wakedeck-cli --help
```

It reads the same profiles as the app, so a computer added in the window is available as `wakedeck-cli connect --profile "<name>"`.

To remove WakeDeck, move it from Applications to the Bin. Your computers are kept in `~/Library/Application Support/WakeDeck/profiles.json`, and how long each one took to wake in `history.json` beside it; delete that folder too to remove them.

## Purpose

Waking a PC remotely usually means a script that fires a magic packet and prints "sent", leaving you to guess when the machine is really usable. WakeDeck keeps two facts apart that are easy to confuse:

- **Wake request sent**: the packets left this computer. That is all a sender can ever know.
- **Service ready**: the program you want to use, such as Sunshine or Remote Desktop, answered.

Anyone can add their own computers in the app: how to wake each one, which address and service to check, and which installed client to open. Personal network settings never live in this repository: they are saved in a profiles file in your own Library folder.

## Features

### In version 1.0.0

- **The app.** Computers on the left; on the right the selected one, a big **Wake & Connect** button, the two facts of the current run ("Wake request: not sent / sent at 22:41:07", "Service: not checked / checking… / ready at 22:41:48 / not ready: timed out"), and the time-stamped timeline. **Check now**, **Wake only** and **Open client** do one step each. **Cancel** stops a run; **Keep waiting 1:00** appears while the app is waiting for the service. A timeout ends with the **What happened** box: what was observed, when the wake request went out, and likely causes, with **Try again** and **Copy details**.
- **Adding a computer.** The editor has three groups, Wake, Check and Connect, each with a Test button that uses the real engine: send the wake request now, check the service now, or find the client without opening it. The MAC address, ports and every other value are validated with the same rules as the profiles file, so the dialog cannot save a half-configured computer. Browse… picks an application; the Wake group can be switched off for always-on computers. The editor works in whole seconds for the check interval and the overall limit; a profile file may hold milliseconds, which the editor rounds down if you edit it there.
- **When the client is missing.** The run still shows that the service is ready, then explains where it looked and offers **Locate client…**, which saves the chosen application into the profile.
- **File menu.** New, edit and delete computers; import and export a single profile as JSON; **Load Example** adds the fictional example profile bundled into the app. **Help ▸ How Waking Works** explains local and remote waking in a few paragraphs.
- **Everything underneath** is the engine of the earlier milestones: the wake sender, the Sunshine and TCP checks, the session state machine, the saved profiles and the client launcher, all exercised from the command line too (`wake`, `check`, `connect`, `launch`, `profile`).
- Nothing blocks the window: checks, name lookups, the pauses between packets and the client launch all run on the event loop.
- Tests cover the packet logic, the real socket code over the loopback interface only, the probes against local servers (including a fake Sunshine), the waker, every path of the session with fakes, the explanation text, the profile format with its validation and recovery paths, the client lookup and launch with harmless programs only, the command-line behaviour end to end on loopback, and the window and the editor driven on Qt's offscreen platform with fake probes, wakers and launchers.

- **A real Mac app.** A signed and notarized disk image with its own icon, built against Qt's official binaries so it runs on macOS 13 and later, with the Qt frameworks and the command-line tool inside the bundle.

### New in version 1.1.0

- **Quit When Client Closes.** A checkable item in the **Run** menu, on by default and remembered between launches. When a Wake & Connect run or **Open client** has started the client, the timeline says "Watching Moonlight … — WakeDeck will quit when it closes", and when that process ends, WakeDeck adds a last line and quits. When that happens depends on the launch mode. With "Start streaming an app", WakeDeck runs Moonlight's `stream` command, which ends when the stream ends, for example because the PC went to sleep, so WakeDeck quits then; if Moonlight shows a message when the stream ends, WakeDeck quits once Moonlight itself has exited. With "Open Moonlight", Moonlight goes back to its list of PCs when the stream ends and keeps running, so WakeDeck quits only when you quit Moonlight. Only a process WakeDeck started itself is watched: a client you had already opened yourself is left alone and WakeDeck stays open. A copy WakeDeck opened earlier that is still running counts as its own, so a second Wake & Connect, **Try again** or **Open client** that only brings it to the front watches it again. A new run or step ends the watch, and so does turning the option off. The exit is reported by the kernel (a kqueue process event), so WakeDeck never polls and the window never blocks. The command-line tool is unchanged.

### New in version 1.2.0

- **Send again while waiting.** A sleeping network card can miss the first packets, for example while a switch port is still coming up. After the first wake request has left the Mac, WakeDeck sends the whole request again every 15 seconds while it waits, measured from the end of the previous send, until the service answers, the time limit is reached or you cancel; **Keep waiting** keeps resending. Only one send is ever in flight. The timeline shows "Wake request sent again (2nd time): …", a resend that fails adds a red line and the run goes on, the **Wake request** fact reads "sent 3 times, last at 22:41:37 (…)", and the **What happened** box says "Wake request sent 8 times between 22:41:07 and 22:42:52: …". The editor's Wake group has **Send again every** (5 to 600 seconds, or **Only once**); the details pane shows "again every 15 s while waiting"; the command line takes `--resend-s <n>` (0 sends once).
- **Every local network.** **Send to** in the editor is a choice between **Every local network this Mac is on** and **A specific address** (the default for new computers, as before). With the first, WakeDeck reads the Mac's network interfaces each time it sends, because networks change, and sends the plan to the broadcast address of every IPv4 interface that is up, running and able to broadcast, leaving out loopback and point-to-point links such as VPN tunnels, without duplicates: "10 packets to each of 2 local networks (192.168.1.255, 10.0.1.255), ports 9 x5, 7 x5". When the Mac is on no such network the send fails with "this Mac is not connected to a local network that supports broadcast". The command line takes `--local-networks` instead of `--to`, and `wake --dry-run --local-networks` lists the addresses it would use without sending anything.
- **Profiles format version 2** with the two new settings; see [Saved profiles](#saved-profiles).
- **Notify When Ready.** A checkable item in the **Run** menu, on by default and remembered. When a Wake & Connect run ends while you are in another application, WakeDeck shows one notification and bounces its Dock icon: "Living Room PC is ready", "… did not answer", "Could not wake …" or "… is ready, but the client did not open", each with one sentence from the run. A cancelled run, **Check now**, **Wake only** and **Open client** never notify, and nothing is shown while WakeDeck is the active application, since the window already says it. macOS asks once whether WakeDeck may show notifications, the first time one is due; if they are switched off, the timeline says so once per launch, "Notifications are switched off for WakeDeck (System Settings ▸ Notifications)", which is where to switch them back on.
- **Progress while waiting.** From the moment a run sends the wake request, or starts waiting for a computer without one, the **This run** box shows a bar with the time, for example "0:12 of up to 2:00", and the Dock icon shows the same bar. Both update once a second and disappear when the run ends or the window closes. The bar never fills up while WakeDeck is still waiting: only the service answering ends the wait. A run whose first check already succeeds shows no progress. Without a learned time the bar counts from the start of the run, so at a timeout it reads the full limit; with one, it counts from the first wake request, as the history does. A run that ends ready is judged by whether WakeDeck was the active application when the service answered, so the client taking the focus afterwards does not cause a notification.
- **Learned timing.** WakeDeck remembers how long each computer took from the first wake request to the service answering, for the last 20 runs in which the service answered after a wake request, also when the client then did not open, since the time is the computer's (a run that was ready at once, timed out, failed to wake or was cancelled adds nothing). Without a wake configured, the learned time is not used and the bar runs against the limit. After the wake request the timeline says "Usually ready about 0:30 after the wake request (last 5 runs)", the median of the last ten, or "Last time: 0:26" while there are fewer than three; the progress then reads "usually about 0:30 — 0:12 so far" and fills against that time. When the wait passes twice the usual time, and at least the usual time plus 20 seconds, one line says "Taking longer than usual". The times are kept in `history.json` beside `profiles.json` (a store called `work.json` gets `work.history.json`): computer names and times only. Deleting a computer removes its times, renaming it in the editor moves them, and a damaged history file is simply started again. The file is read, changed and written as a whole, so if the app and the command line finish a run at the same moment one of the two times can be lost; it is a convenience, and such a loss only makes the usual time a little less exact. `connect --profile` shows the same lines and records its runs in the same file.
- **Tailscale awareness.** When the check host is a Tailscale address (100.64.0.0/10 or a `*.ts.net` name), WakeDeck first runs Tailscale's own command-line tool on this Mac, `status --json`, and reads its answer; nothing is stored or sent anywhere, and the tool gets at most 3 seconds. If Tailscale is not connected on the Mac, the run ends at once, before any check or wake request: "Tailscale is not connected on this Mac (state: Stopped), so … cannot be reached. Turn Tailscale on and try again.", with the **Service** fact "not checked: Tailscale is off on this Mac" and **Try again**; `connect` exits with code 6. If it is connected, one line says what Tailscale shows, for example "Tailscale is connected; it shows living-room-pc offline, last seen 3 h ago", and the run goes on. If the run times out, Tailscale is asked once more and the **What happened** box adds what it shows then, attributed to Tailscale: the computer online ("If that is current (Tailscale can take a while to notice that a computer went to sleep), the computer is awake and the service on port N is what did not answer"), still offline (so it most likely did not wake), or Tailscale no longer connected on the Mac. While the run finishes that last read, **Keep waiting** is gone and the wait can no longer be extended. **Check now** in the window and `check` on the command line add one line with what Tailscale shows when the service is not ready. If the tool is not installed, fails or is slow, everything works exactly as without it. The tool is looked for at `/Applications/Tailscale.app/Contents/MacOS/Tailscale`, `/usr/local/bin/tailscale` and `/opt/homebrew/bin/tailscale`; the environment variable `WAKEDECK_TAILSCALE_CLI` names another one (the tests use it for fake tools).
- **`--diagnose`.** `WakeDeck.app/Contents/MacOS/WakeDeck --diagnose <file>`, or `open -n -W WakeDeck.app --args --diagnose <file>` to start it the way Finder does, writes a short report on what the app sees of the system and exits without opening a window; see [How to Compile & Run](#how-to-compile--run).

### New in version 1.3.0

- **Dock Menu & `wakedeck://` URL Scheme.** Right-click WakeDeck's Dock icon for one-click **Wake & Connect — &lt;Computer&gt;** actions for every saved computer, or **Cancel** when a run is active. WakeDeck registers the `wakedeck://` URL scheme for Apple Shortcuts, Spotlight, browser bookmarks, and Stream Deck. URLs follow `wakedeck://<action>/<percent-encoded-profile-name>` (or `wakedeck://<action>?profile=<name>`), with actions `connect` (Wake & Connect), `check` (Check now), `wake` (Wake only), and `open` (Open client). `--diagnose` reports `urlScheme=wakedeck` when run on the app bundle.
- **Duplicate & Reorder Computers.** `File ▸ Duplicate Computer…` (`Cmd+D`) opens the editor prefilled from the selected computer with a collision-free suggested name ("… Copy", "… Copy 2"); `wakedeck-cli profile duplicate <name> [<new-name>] [--replace]` provides CLI parity. Computers can be reordered in the sidebar via drag-and-drop or with `File ▸ Move Up` (`Cmd+Opt+Up`) and `File ▸ Move Down` (`Cmd+Opt+Down`), or in CLI with `wakedeck-cli profile move <name> --to <top|bottom|1..N>`. Editing or renaming a computer preserves its exact position in the list.
- **Tailscale Peer Picker & Moonlight Import.** The computer editor has a **Choose from Tailscale…** button that queries Tailscale peers asynchronously and sets the check host (preferring DNS name or IPv4) and default computer name. **File ▸ Import from Moonlight…** and `wakedeck-cli profile import-moonlight [--from <file>] [--replace] [--dry-run]` parse Moonlight preferences (plist and ini formats), importing Sunshine hosts, ports, MAC addresses (both binary byte arrays and strings, skipping `00:00:00:00:00:00`), and streaming apps.
- **Check All Computers.** `Run ▸ Check All Computers` (`Cmd+Shift+K`) runs asynchronous readiness probes across all saved computers concurrently using `BatchChecker`, reporting each result to the timeline and updating per-computer status badges in the sidebar. On the command line, `wakedeck-cli check --all` checks all saved computers and prints a summary, and `wakedeck-cli check --profile <name>` checks one saved computer.
- **Profile Schema Version 2 Preserved.** No changes to `profiles.json` object keys; full forward and backward compatibility with version 1.2.0 is maintained.

### New in version 1.3.1

- **macOS Menu Roles and `Cmd+Q` Fix.** Explicitly assigns `QAction::NoRole` to **Quit When Client Closes** and every regular action in the **File**, **Run**, and **Help** menus, preventing Qt's macOS heuristics from promoting "Quit When Client Closes" into the application menu's Quit item. Adds an explicit **Quit WakeDeck** action (`Cmd+Q`, `QuitRole`) in the **File** menu (merged by macOS into **WakeDeck ▸ Quit WakeDeck**) wired to application shutdown that stops client watching, cancels any active session, clears Dock progress, closes the window, and invokes the quit hook. Sets `AboutRole` explicitly on **About WakeDeck**.
- **Sidebar Badge and Reorder Cleanup.** Editing, renaming, or deleting a saved computer clears its stale **Check All** status badge (`ready` / `not ready`) from memory and sidebar item text, while untouched computers retain their badges. Drag-and-drop reordering in the computer list refreshes the enabled states of **Move Up** and **Move Down** immediately.
- **Profile Schema Version 2 Preserved.** No changes to `profiles.json` schema or keys.

## How It Works

A magic packet is a UDP datagram of 102 bytes: six `0xFF` bytes followed by the target's MAC address repeated sixteen times. A sleeping PC has no working IP address. Its network card watches the wire for that pattern and powers the machine on, so the packet must arrive on the PC's own network segment, which normally means a broadcast on that LAN.

- **Same network.** Your Mac and the PC share a Wi-Fi or Ethernet network: send to the network's broadcast address, for example `192.168.1.255`, and the card sees it. From 1.2.0, **Every local network this Mac is on** finds those broadcast addresses by itself each time, so the same profile works on whichever home network the Mac is connected to. Most Wi-Fi adapters cannot wake a powered-off machine; Ethernet usually can.
- **From elsewhere.** The internet and VPNs route packets to running hosts and do not carry LAN broadcasts, and a sleeping PC is not on your VPN at all. Something always on inside the home network has to emit the broadcast for you: a router with a wake feature or a port forward to the PC's reserved address, or a small always-on device (a Raspberry Pi, a NAS) that relays the packet. WakeDeck sends to whatever destination you configure and does not assume how it reaches the PC.

Once the PC is up, a VPN such as Tailscale gives it a stable address from anywhere. That address is what the readiness check uses, and what Moonlight connects to.

### In the app

First time: open WakeDeck and click **Add…**. Give the computer a name. In **Wake**, enter its MAC address and choose where the wake request goes: **A specific address**, usually the broadcast address of its network, or, when the Mac is on the same network as the computer, **Every local network this Mac is on**; the ports and counts default to 9 and 7, five packets each, and **Send again every** to 15 seconds while WakeDeck waits (**Only once** turns that off). In **Check**, choose Sunshine or a TCP port and enter the computer's Tailscale name or address. In **Connect**, choose Moonlight and either "Open Moonlight" or "Start streaming an app" with the app's name as Moonlight shows it, for example Desktop. The three Test buttons tell you at once whether the packets left, whether the service answers right now, and whether the client was found. Click OK. The profile is saved in `~/Library/Application Support/WakeDeck/profiles.json`.

Every day: select the computer and click **Wake & Connect**, or double-click it. If the service is already ready, the client opens within a second and the timeline says no wake was needed. If the computer is asleep, the timeline shows the check, the wake request with its time, the checks that follow, the moment the service answered, and the launch. If nothing answers in the configured time, the **What happened** box says exactly what was observed and what that usually means.

When you are done: put the PC to sleep or close the client. With **Run ▸ Quit When Client Closes** checked, the default, WakeDeck quits as soon as the client it opened has closed, so nothing is left running on your Mac: in streaming mode when the stream has ended and Moonlight has exited, in "Open Moonlight" mode when you quit Moonlight, which otherwise stays on its list of PCs. Uncheck it to keep WakeDeck open; the choice is remembered.

While you wait: you can switch to another application. The bar in the **This run** box and on the Dock icon shows how long WakeDeck has been waiting and the limit, or, once WakeDeck has seen the computer wake a few times, how long it usually takes, and with **Run ▸ Notify When Ready** checked, the default, a notification says how the run ended: ready, no answer, wake request not sent, or ready with the client not opened. The first time a notification is due, macOS asks whether WakeDeck may show notifications; you can change the answer later in **System Settings ▸ Notifications ▸ WakeDeck**. Uncheck the menu item to have no notifications from WakeDeck at all; the choice is remembered.

### Wake & Connect, step by step

1. **Tailscale.** Only for a Tailscale host: read Tailscale's status on this Mac (at most 3 seconds). Not connected ends the run here; connected adds what Tailscale shows about the computer; no answer from the tool changes nothing.
2. **Check.** Ask the service once. For Sunshine that is an HTTP `GET /serverinfo` on port 47989, the same request Moonlight uses to show a PC as online; the reply is XML with the host name and a state such as `SUNSHINE_SERVER_FREE`, and a reply that is not Sunshine's counts as *not ready*. For any other service it is a plain TCP connection to its port.
3. **Wake.** If the service did not answer and a wake is configured, send the magic packets. "Wake request sent" is reported as its own event, followed by what is usual for this computer once WakeDeck has seen it wake.
4. **Wait.** Check again every few seconds (default 3 s) until the service answers or the limit (default 2 minutes) runs out. Meanwhile the wake request is sent again every 15 seconds by default; a resend that fails is reported and the wait goes on, and one still under way when the service answers is ignored. Each check is classified:
   - *refused*: something at that address refused the connection, usually the computer itself, awake but with the service not listening yet;
   - *timed out* or *unreachable*: nothing answered, so the computer is still off, or not reachable from here;
   - *name not resolved*: the host name could not be turned into an address, often a VPN that is not connected;
   - *not ready*: something answered on the port, but not with the expected reply.
5. **Launch.** Open the client configured for this computer. A computer without a client stops at "ready".

When the wait runs out, the summary says exactly what was observed, for example "40 checks, 40 timed out", when the wake request was sent, and what that pattern usually means. If the host looks like a Tailscale address it says so, and adds what Tailscale itself shows at that moment. The text never claims the computer is on or off as WakeDeck's own finding; it says what answered and what did not, and what Tailscale reports, in Tailscale's name.

A typical run as the timeline shows it, with placeholder values:

```
22:41:05  Tailscale is connected; it shows living-room-pc offline, last seen 3 h ago
22:41:05  Checking Sunshine at living-room-pc.example-tailnet.ts.net:47989
22:41:07  Not ready: timed out (no answer within 2000 ms)
22:41:07  Wake request sent: 10 packets to 192.168.1.255 (ports 9 x5, 7 x5)
22:41:07  Usually ready about 0:30 after the wake request (last 5 runs)
22:41:07  Waiting for Sunshine at living-room-pc.example-tailnet.ts.net:47989 (up to 2:00)
22:41:12  Check 2: timed out (waiting 0:07 of 2:00)
22:41:17  Check 3: timed out (waiting 0:12 of 2:00)
22:41:22  Wake request sent again (2nd time): 10 packets to 192.168.1.255 (ports 9 x5, 7 x5)
22:41:37  Wake request sent again (3rd time): 10 packets to 192.168.1.255 (ports 9 x5, 7 x5)
22:41:48  Sunshine at living-room-pc.example-tailnet.ts.net:47989 ready: hostname LIVING-ROOM-PC, state free (after 0:43)
22:41:48  Launching Moonlight, stream "Desktop" on living-room-pc.example-tailnet.ts.net
22:41:48  Launched Moonlight, stream "Desktop" on living-room-pc.example-tailnet.ts.net (process 48213)
22:41:48  Watching Moonlight, stream "Desktop" on living-room-pc.example-tailnet.ts.net (process 48213) — WakeDeck will quit when it closes
```

### Opening the client

Moonlight has two launch modes. **Open** asks the system to open the application bundle, or to bring it to the front if it is already running, exactly as a double click in Finder would. **Stream** runs Moonlight's own executable with `stream <host> <app>`, Moonlight's command-line form, so the stream starts without any clicking; the executable is run directly because arguments handed to an already-running application are not delivered to it. A **custom** client is an absolute path to an executable or a .app bundle plus optional arguments; a bundle without arguments is opened, anything with arguments is run, and `{host}` in an argument becomes the computer's address. The host used is the one the service check used, so the client connects to the same address that just answered. Moonlight is found through Launch Services by its bundle identifier, then in /Applications and ~/Applications, or at the path saved in the profile.

For **Quit When Client Closes**, the launcher reports which process it started. An executable that WakeDeck runs, in stream mode or as a custom client, is that process. An opened bundle is the process the system started for it, unless a copy with the same bundle identifier was already running before WakeDeck asked: then the system only brought that copy to the front and started nothing. The launcher still reports the running copy's process, and the window watches it only if it is the very process WakeDeck started earlier, which it follows for as long as it lives; a copy you opened yourself is never watched, and WakeDeck stays open when it closes.

### Saved profiles

A profile is the whole configuration of one computer. The file is plain JSON, one object per profile inside a `profiles` list, and looks like the example below (shortened). Every field has a default except the name and the check host, and every value is validated when the file is read, so a mistyped MAC address, port or field name is reported by name instead of producing a half-configured computer. The client settings follow one set of rules whether they come from the app, a file or the command line: a custom client needs an absolute path, streaming needs the name of the app, and those options apply to the client they belong to.

```json
{
    "name": "Living Room PC (example)",
    "wake":  { "mac": "00:00:5E:00:53:2A", "destinationMode": "address", "destination": "192.168.1.255",
               "ports": [9, 7], "repeat": 5, "intervalMs": 100, "resendSeconds": 15 },
    "check": { "kind": "sunshine", "host": "living-room-pc.example.net", "port": 47989, "timeoutMs": 2000 },
    "pollIntervalMs": 3000,
    "overallTimeoutMs": 120000,
    "client": { "preset": "moonlight", "launchMode": "stream", "appName": "Desktop" }
}
```

`destinationMode` is `"address"` (the default) or `"localNetworks"`; with `"localNetworks"` the `destination` is not used: one left in the file, for example after changing only the mode by hand, is ignored and written back empty. On the command line `--to` and `--local-networks` still exclude each other. `resendSeconds` is 0 (send once) or 5 to 600, default 15.

The file format has a version. WakeDeck 1.2 writes version 2, the store and every profile in it, which adds `destinationMode` and `resendSeconds`; it reads version 1 files, from WakeDeck 1.0 and 1.1, with those two at their defaults. Because 1.0 and 1.1 refuse version 2 files, the first save that replaces a version 1 file first copies it to `profiles.v1-backup.json` in the same folder; that copy is made once and never overwritten, so it stays the file the older versions can read. To go back to 1.1, quit WakeDeck and rename `profiles.v1-backup.json` to `profiles.json` (computers added since are then missing). Exported profiles are version 2 too.

A few rules protect the file. It is written atomically, so a crash while saving leaves the previous version intact. A file that is not valid JSON, or has no profiles list, is renamed to `profiles.json.corrupt-<date>` (never overwriting an earlier one) and WakeDeck starts with an empty list, telling you where the old file went. A file whose `schemaVersion` is newer than this WakeDeck understands is refused and not modified, and so is an exported single profile mistaken for a store or a file that cannot be read: nothing is ever saved over such a file, so while it is in place the app refuses to add, change or remove computers and says why. Single unreadable entries are skipped with a warning and one copy of the original file is kept beside it.

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
| `src/engine/QtWaker.h/.cpp` | `WakeConfig` with its destination mode and resend interval; the `Waker` interface and `QtWaker`: resolves the destination asynchronously, or asks an injectable provider for the local broadcast addresses, and sends one step of the plan per timer tick, reusing the core's packet and plan. |
| `src/engine/LocalNetworks.h/.cpp` | `chooseBroadcastAddresses`, the pure rules for "every local network" (IPv4, up, running, can broadcast, not loopback, not point-to-point, no duplicates), and `readLocalInterfaces`, which describes this Mac's interfaces from `QNetworkInterface`. |
| `src/engine/Explain.h/.cpp` | `Observations` and `explainTimeout`, the "What happened" text, with what Tailscale showed at the end; `formatDuration`; `looksLikeTailscale`. |
| `src/engine/Tailscale.h/.cpp` | `parseTailscaleStatus` and `findTailscalePeer`, the pure reading of `tailscale status --json` and the matching of the check host to a device; `describeTailscale`; `ProcessTailscaleReader`, which runs the tool through `QProcess` with a 3 second limit; the reader factory the session uses. |
| `src/engine/TimingHistory.h/.cpp` | `TimingHistory`: the history file beside the profiles store, written atomically, with the last 20 times per computer; the typical time (median of the last ten), the "Usually ready" text and the "Taking longer than usual" threshold. |
| `src/engine/Moonlight.h/.cpp` | `MoonlightHost` and `readMoonlightSettings`/`readMoonlightFile`: parses Moonlight's preferences (`QSettings` / plist / ini), extracting addresses, ports, MACs (6-byte binary or string), and streaming apps, and converting them to `Profile`. |
| `src/engine/BatchChecker.h/.cpp` | `BatchChecker`: asynchronous batch probe runner that checks multiple profiles concurrently and emits individual item updates and a final summary without blocking the event loop. |
| `src/engine/Session.h/.cpp` | `Session`, the Wake & Connect state machine (Checking, Waking, Waiting, Launching, Done, Failed), its timeline events and outcomes, the resends while it waits, the Tailscale reads before the first check and at a timeout, the learned-timing lines and the time from the wake request to ready; the `Launcher` interface with `NoLauncher`. |
| `src/engine/Profile.h/.cpp` | `Profile` and `ClientConfig`, the saved-computer value types; `toJson` and `profileFromJson` with full validation; `suggestedDuplicateName` and `moveProfile`; `clientConfigError`, the one set of client rules; name helpers. |
| `src/engine/ProfileStore.h/.cpp` | Reads and writes the profiles file atomically, with the schema version, recovery of unreadable files and single-profile import and export. |
| `src/engine/AppLocator.h`, `src/mac/AppLocator.mm` | The platform interface and its macOS implementation in Objective-C++: Launch Services, `NSBundle`, `NSWorkspace`. The engine's only file that speaks to AppKit. |
| `src/engine/ClientLauncher.h/.cpp` | `resolveLaunch`, which turns a `ClientConfig` and a host into the exact program and arguments or an explanation of what was searched; `ClientLauncher`, the `Launcher` that runs it and reports the process it started. |
| `src/engine/ProcessWatcher.h/.cpp` | `ProcessWatcher`: reports on the event loop when one process exits, through a kqueue (`EVFILT_PROC`, `NOTE_EXIT`) whose descriptor a `QSocketNotifier` watches; a process already gone is reported too. Owns and closes the kqueue. |
| `src/app/UrlCommand.h/.cpp` | `parseWakeDeckUrl`: parses and validates `wakedeck://` URLs for `connect`, `check`, `wake`, and `open` actions with percent-decoding. |
| `src/app/main.cpp` | Creates the application and the window, or runs `--diagnose` without a window. Handles `QEvent::FileOpen` for incoming `wakedeck://` URLs. |
| `src/app/MainWindow.h/.cpp` | The window: the list of computers, the details pane, the run controls, the two facts, the timeline and the "What happened" box; menus for import, export, the example, help and **Quit When Client Closes**, which it keeps in `QSettings` and carries out with a `ProcessWatcher`. Takes the factories for a run as a parameter, and the quit as a replaceable hook, so tests drive it with fakes. **Notify When Ready** and the wait progress, with the notifier, the Dock progress and the "is WakeDeck the active application" test replaceable in the same way. |
| `src/app/SystemServices.h/.cpp`, `src/mac/SystemServices.mm` | The `Notifier` and `DockProgress` interfaces with do-nothing versions, and their macOS implementations in Objective-C++: `UNUserNotificationCenter`, used only by an application bundle with an identifier and only on Qt's `cocoa` platform, and an `NSDockTile` view that draws the application icon and the bar itself. The same drawing writes the `--diagnose` picture. |
| `src/app/WaitProgress.h/.cpp` | `waitProgress`, the one pure function behind the bar and the Dock: fraction and text from the time waited, the typical time and the limit. |
| `src/app/Diagnose.h/.cpp` | The `--diagnose` report, Tailscale's state and device count included. |
| `src/app/ProfileEditorDialog.h/.cpp` | The add/edit dialog with the Wake, Check and Connect groups, their Test buttons and the same validation as the file format. |
| `src/cli/main.cpp` | Command dispatch and the synchronous `wake` command. |
| `src/cli/Flags.h/.cpp` | The small option parser shared by every command: values, switches, positional words, help anywhere. |
| `src/cli/SessionFlags.h/.cpp` | Turns flags into `SessionConfig` and `ClientConfig`, so `connect`, `launch` and `profile add` accept the same options. |
| `src/cli/ConnectCommand.h/.cpp`, `src/cli/LaunchCommand.h/.cpp`, `src/cli/ProfileCommand.h/.cpp` | The `check`, `connect`, `launch` and `profile` commands. |
| `examples/example-profile.json` | A complete fictional profile; bundled into the app for **Load Example**, and importable from the command line. |
| `tests/check.h`, `tests/test_core.cpp`, `tests/test_loopback.cpp` | Framework-free unit tests of the core and the loopback socket test (Milestone 1). |
| `tests/test_engine.cpp`, `tests/test_profiles.cpp`, `tests/test_launcher.cpp` | Qt Test suites for the engine, the profiles and the launcher. |
| `tests/fake_agent.mm`, `tests/agent_bundle.h` | A windowless AppKit process, and the helper that wraps it in a background-only bundle, so `test_launcher` and `test_gui` can try the open path and the process watcher without any visible application. |
| `tests/test_gui.cpp` | Qt Test suite for the window and the editor on the offscreen platform, with fake probes, wakers and launchers and a temporary profiles folder. |
| `tests/cli_test.sh` | Runs the built tool through usage errors, `--dry-run`, the `profile` command on a temporary store, `launch` with harmless clients, and `check`/`connect` on loopback ports. |
| `packaging/Info.plist.in` | The app's Info.plist: identifier, version, icon, minimum macOS, the local-network explanation. |
| `packaging/icon/` | `make-icon.swift` draws the icon with CoreGraphics; `make-icns.sh` turns it, or any 1024 × 1024 PNG, into `WakeDeck.icns`, which is committed. |
| `packaging/licenses/` | The GNU LGPL v3 and GPL v3 texts and the third-party notice for Qt, copied into the app bundle. |
| `packaging/get-qt.sh` | Downloads Qt's official macOS binaries for release builds with aqtinstall, no Qt account needed. |
| `packaging/make-release.sh` | Builds, tests, bundles, signs, notarizes and staples the app, and produces the disk image with its checksum. |
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

To see what the app itself sees of the system, start it in its windowless diagnose mode: `open -n -W build/WakeDeck.app --args --diagnose /tmp/wakedeck-diagnose.txt` (an absolute path; `open` starts the app as Finder would). It writes `version=`, `platform=`, `bundleId=`, `notifications=` (`authorized`, `denied`, `notDetermined` or `unavailable`, read without asking for permission), `dockTile=`, `tailscaleCli=` (the tool's path or `not found`), `tailscaleState=` (for example `Running`, or `error: ` and the reason) and `tailscalePeers=` (how many devices Tailscale lists; never a name or an address) lines, draws the Dock progress at 40 % into `/tmp/wakedeck-diagnose.txt.dock.png`, and exits; it touches no profile and no setting.

Qt installed elsewhere: `make QT_PREFIX=/path/to/Qt/6.x/macos`. Another build directory: `make BUILD_DIR=/path/outside/the/repo`. The app built this way is unsigned and links the Qt frameworks from your Qt installation; it is for development on your own Mac.

### Making a release

The disk image is built against Qt's official binaries, not Homebrew's: Homebrew builds Qt for the macOS version it runs on, while Qt 6.11.3's official binaries run on macOS 13.0. Qt 6.12's official binaries need macOS 14.4, so 6.11.3 is the newest that keeps the macOS 13 promise. The maintainer needs, once: a **Developer ID Application** certificate in the login keychain, created by the Apple Developer account holder, and notarization credentials stored with `xcrun notarytool store-credentials WakeDeckNotary`.

```bash
sh packaging/get-qt.sh "$HOME/Library/Developer/WakeDeck-Qt"   # about 1.4 GB, outside iCloud
make release QT_DIR="$HOME/Library/Developer/WakeDeck-Qt/Qt/6.11.3/macos"
```

`make-release.sh` configures a fresh Release build for macOS 13.0 and Apple Silicon, runs every test against it, copies the app and the command-line tool into a staging folder, lets `macdeployqt` copy the Qt frameworks and plug-ins into the bundle, and fails if any library still points outside the bundle or any binary needs a newer macOS than 13.0. It signs every library, framework and executable from the inside out with the hardened runtime and a secure timestamp, submits the app to Apple's notary service and staples the ticket, builds the disk image with an Applications shortcut, signs it, notarizes and staples it too, and finally asks Gatekeeper for its verdict on both. The signing identity is read from the keychain and the notarization credentials stay there, so nothing personal enters the repository. `SKIP_NOTARIZE=1` makes a signed image without contacting Apple, and `ARCHS="arm64;x86_64"` builds a universal app, untested on Intel Macs so far.

Commands of the tool:

```
wakedeck-cli wake    --mac <address> --to <host>|--local-networks [--ports 9,7] [--repeat 5] [--interval-ms 100] [--dry-run]
wakedeck-cli check   --host <host> [--port 47989] [--kind sunshine|tcp] [--timeout-ms 2000]
wakedeck-cli check   --profile <name> [--store <file>] [--timeout-ms <ms>]
wakedeck-cli check   --all [--store <file>] [--timeout-ms <ms>]
wakedeck-cli connect --check-host <host> [--check-port 47989] [--check sunshine|tcp] [--probe-timeout-ms 2000]
                     [--poll-s 3] [--timeout-s 120] [--mac <address> --to <host>|--local-networks [--ports 9,7] [--repeat 5]
                     [--interval-ms 100] [--resend-s 15]]
                     [--client none|moonlight|custom] [--client-path <path>] [--launch open|stream] [--app <name>] [--client-args "<arguments>"]
wakedeck-cli connect --profile <name> [--poll-s 3] [--timeout-s 120]
wakedeck-cli launch  --profile <name> [--dry-run]
wakedeck-cli launch  --host <host> --client moonlight|custom [--client-path <path>] [--launch open|stream] [--app <name>]
                     [--client-args "<arguments>"] [--dry-run]
wakedeck-cli profile list | show <name> | remove <name> | import <file.json> [--replace] | export <name> <file.json>
wakedeck-cli profile duplicate <name> [<new-name>] [--replace] [--store <file>]
wakedeck-cli profile move <name> --to <top|bottom|1..N> [--store <file>]
wakedeck-cli profile import-moonlight [--from <file>] [--replace] [--dry-run] [--store <file>]
wakedeck-cli profile add --name <name> --check-host <host> [check, wake and client options as for connect] [--notes <text>] [--replace]
wakedeck-cli <command> --help
```

`--local-networks` replaces `--to` (exactly one of the two goes with `--mac`): the wake request goes to the broadcast address of every local network of this Mac, read again for every send. `wake --dry-run --local-networks` lists those addresses and says that no packet was sent; reading the interface list is not network traffic. `--resend-s` sets how often `connect` sends the wake request again while it waits, 0 for once or 5 to 600 seconds, default 15; `profile add` saves it.

Every `profile` form, `connect --profile`, `launch --profile`, and `check --profile`/`--all` accept `--store <file>` to use another profiles file; the default is the same file the app uses, `~/Library/Application Support/WakeDeck/profiles.json`, so computers added in the app are available to the tool and vice versa. Profile names are matched without regard to case.

`connect --profile` reads the learned timing from the history file beside the store and records there a run whose service answered after a wake request, also when the client then failed; `profile remove` removes the computer's times too. Ad-hoc `connect` records nothing. For a Tailscale host, `connect` and `check` run Tailscale's own `status --json` on this Mac, as the app does.

Exit codes. `wake`: 0 all packets sent, 1 usage error, 2 destination not resolved or no local network found, 3 socket error or some packets not sent. `check`: 0 ready (or all ready with `--all`), 1 usage error, 2 not ready (or some not ready with `--all`). `connect`: 0 ready, and the client launched if one is configured, 1 usage error, 2 timed out or profile not found, 3 wake request failed, 4 client not found or not launched, 6 Tailscale is not connected on this Mac (Tailscale hosts only; nothing was checked or sent). `launch`: 0 launched or would be, 1 usage error, 2 client not found or not started. `profile`: 0 done, 1 usage error, 2 not found, already exists, Moonlight file missing or empty, or a file problem.

## Verification

`make test` runs seven tests through ctest. The Qt Test suites are counted in test functions (each private slot, without `initTestCase`); data rows are named where a function has them. The other three count their checks.

- `test_core` (66 checks): MAC parsing, packet layout including the full hex dump decoded back, port-list parsing, plan order, `sendWake` through a recording sink including a simulated failing port, numeric address resolution. No sockets, no name lookups.
- `test_loopback` (25 checks): a UDP listener on `127.0.0.1` receives three packets from the real `UdpSender` and checks every byte; then descriptor creation is blocked for a moment to check that a sender which cannot open its socket says so.
- `test_engine` (75 test functions, Qt Test): Sunshine reply parsing; the TCP and Sunshine probes against servers on `127.0.0.1`, including a fake Sunshine, a wrong service, an HTTP error and a silent server; the waker over loopback; the session with scripted fakes through every outcome; the explanation text, durations and the Tailscale heuristic. For 1.2.0: the choice of broadcast addresses from synthetic interfaces; real interface reading; the waker with local network broadcasts; resends at the interval that stop at ready, at the time limit and on cancel; Tailscale parser and reader fakes; learned timing in the session. For 1.3.0: `preferredTailscaleHost` formatting rules; `readMoonlightSettings` and `readMoonlightFile` parsing binary 6-byte and string MAC addresses, address priority, Moonlight application launch modes, `hostsbackup` fallback, and ini files; `BatchChecker` asynchronous multi-profile runs, empty profile handling, and timeout overrides.
- `test_profiles` (38 test functions, one of them with 39 data rows, so 76 test cases, Qt Test): the JSON round trip, defaults, 39 invalid inputs each rejected by field name, the shipped example, and the store's save, load and recovery paths in a temporary folder. For 1.2.0: a version 1 profile and store loading with new defaults, version 2 round trip with local networks and resend bounds, version 3 store refusal, and timing history persistence. For 1.3.0: `suggestedDuplicateName` generating collision-free names; `moveProfile` reordering profiles while maintaining bounds and relative order.
- `test_launcher` (24 test functions, Qt Test): Launch Services lookups, a fake `.app` bundle in a temporary folder, `resolveLaunch` for every client kind including relative paths, launches of `/usr/bin/true` with its process id reported and of the fake bundle's script, the fake bundle opened through the system, a completion skipped for a deleted caller, and the session's launch step; a background-only fake agent opened through the system, whose new process is reported and watched until it ends, while a second open of the running copy reports no new process but names the running one; and the process watcher on `/bin/sleep 1`, on a process already gone, and stopped or destroyed before the exit. No window ever appears.
- `test_gui` (68 test functions, Qt Test, offscreen platform): the window with an empty store, with saved computers, adding, removing and reloading, **Load Example** from the bundled resource, a corrupt store kept as a warning for the user, a failed save leaving list and memory in step; **Wake & Connect** clicked for real on a laid-out window with fake probes, wakers and launchers; **Quit When Client Closes**; **Notify When Ready**; progress in window and on Dock icon; and `--diagnose`. For 1.3.0: `parseWakeDeckUrl` URL parsing; `openUrl` handling connect, check, wake, and open actions with busy refusal and invalid configuration checks; `FakeDockMenu` verifying state and signal tracking; `updateProfile` in-place list order preservation on rename; `duplicateProfile` and `moveCurrentProfile` actions; refused store safety; Tailscale peer picker in the editor; Moonlight import dialog and host creation; `checkAllComputers` batch checking in the window with timeline updates and sidebar badges; and `--diagnose` reporting `urlScheme=wakedeck`. For 1.3.1: `quitActionAndMenuRoles` verifying macOS menu role assignments, `Cmd+Q` shortcut, and clean run cancellation on quit; and `editingRenamingAndDeletingClearCheckAllBadges` verifying stale badge clearing across profile edits, renames, and deletions.
- `tests/cli_test.sh` (221 checks): usage errors, help and `--dry-run` output of every command; profiles with every local network and a resend interval, a version 1 store kept once as `profiles.v1-backup.json` and a version 3 store refused untouched; the `profile` command against a temporary store; `launch` with `/usr/bin/true` and `/bin/echo`; and, when `python3` is available to open ports, end-to-end runs of `check`, `connect`, `connect --profile` and `connect` with a custom client against closed and open loopback ports. For 1.3.0: version string 1.3.0; `profile duplicate` auto-naming and collision handling; `profile move` with `top`, `bottom`, and numeric indices; `profile import-moonlight` dry-run, skipping, replacing, and untouched store on skip; `check` argument exclusivity; loopback tests for `check --profile` and `check --all`.

Not covered automatically: a real notification and the real Dock icon (the tests use fakes and never reach the notification center; `--diagnose` checks the permission state and draws the Dock picture), opening a real graphical application such as Moonlight, WakeDeck quitting for real when such a client closes (the tests replace the quit with a hook), the look of the window on a real screen, the plain TCP check's own *timed out* path, the *unreachable* and *name not resolved* classifications of the real probes, and a real wake reaching a real PC; for 1.2.0, a wake request actually sent to this Mac's local networks (the tests read the interface list but send only to `127.0.0.1`) and a resend at its real interval of seconds from the app or the command line (the engine and window tests use milliseconds); Tailscale's real tool (the tests use fake ones; `--diagnose` shows whether the signed app can run it), and "Taking longer than usual" at its real threshold of 20 seconds or more. The author has run the full flow against a real PC from the command line: wake over Wake-on-LAN, Sunshine answering over Tailscale 26 seconds later.

The release script adds its own checks: the same seven tests against a fresh release build, no library linking outside the bundle, every binary running on macOS 13.0, a strict deep signature check, Apple's notarization verdict for the app and for the disk image, stapled tickets validated, and Gatekeeper's assessment of both.

### Acceptance test

A release is ready when a second person can do the following on their own Mac, with only the disk image and this README, without opening Terminal or editing any file:

1. Install WakeDeck by dragging it to Applications, and open it without any security workaround.
2. Add their own computer in the editor in a few minutes, with the three Test buttons all answering.
3. With the computer asleep, click **Wake & Connect** and see "Wake request: sent", then "Service: ready", then the client opening.
4. With the computer already awake, see the client open within a few seconds and the timeline say no wake was needed.
5. With a deliberately wrong MAC address, see the wake request reported as sent and the service as timed out, with a **What happened** explanation instead of a success.
6. With the client moved away, see **Locate client…** offered and working.
7. Export the profile and find nothing secret in it.
8. With the computer asleep, switch to another application after **Wake & Connect**: see the bar on the Dock icon, then one notification when the computer is ready; the timeline shows "Wake request sent again" lines while waiting.
9. After two or three wakes, see "Usually ready about …" or "Last time: …" after the wake request, and the bar's text use that time.
10. With a Tailscale host and Tailscale switched off on the Mac, see the run stop at once with "Tailscale is not connected on this Mac"; with it on, see what Tailscale shows about the computer.
11. Right-click WakeDeck's Dock icon and trigger Wake & Connect directly from the Dock menu.
12. Duplicate a computer with **File ▸ Duplicate Computer…** (`Cmd+D`) and reorder computers in the list with **File ▸ Move Up** / **Move Down** or drag-and-drop.
13. Pick Tailscale peers with **Choose from Tailscale…** in the editor, or import existing Sunshine setups with **File ▸ Import from Moonlight…**.
14. Check all computers at once with **Run ▸ Check All Computers** (`Cmd+Shift+K`).

## Current Scope

Version 1.3.1: the six milestones of 1.0.0, the Quit When Client Closes option of 1.1.0, the resends, local networks, profiles format version 2, Notify When Ready, wait progress, learned timing, and Tailscale awareness of 1.2.0, the Dock menu, `wakedeck://` URL scheme, computer duplication and reordering, Tailscale peer picker, Moonlight import, and batch check of 1.3.0, and the macOS menu role and sidebar badge cleanups of 1.3.1. Profile schema version 2 is preserved.

Not here, and worth knowing:

- Apple Silicon only in the published disk image; Intel Macs would need a universal build that has not been tested.
- No automatic updates; new versions are downloaded by hand.
- Stream mode runs Moonlight's executable directly, so the arguments always arrive even if Moonlight is already open; a second Moonlight window may then appear until the first is closed.
- Quit When Client Closes watches only the client process WakeDeck started. In Open mode with a Moonlight already running that WakeDeck did not open, WakeDeck stays open; a custom client that hands its work to another program and exits at once makes WakeDeck quit at that moment, so turn the option off for such a client. If the client closes while a file dialog (Import, Export or Locate client) is open, WakeDeck quits once that dialog is closed.
- Tailscale awareness reads what Tailscale's own tool reports on this Mac; while the editor provides a "Choose from Tailscale…" button to pick a peer's host, WakeDeck cannot tell why an offline device is unreachable.
- IPv4 only. SecureOn passwords are not supported. "Every local network" sends only on networks this Mac is on; it cannot reach a computer behind a VPN or on another network.
- macOS only, by design: the client launcher uses Launch Services and NSWorkspace.
- Development builds against Homebrew's Qt run only on the macOS version Homebrew built Qt for; the published disk image uses Qt's official binaries and runs on macOS 13 and later.

## Concepts Demonstrated

- Byte-level data with the C++17 standard library: `std::array<std::uint8_t, N>`, `std::optional` for parse results, `std::string_view` parsing, `std::from_chars`.
- Resource management with RAII: a socket descriptor and a kqueue descriptor, each owned by one object, released in its destructor, non-copyable.
- Interfaces separating policy from mechanism: `DatagramSink`, `Probe`, `Waker`, `Launcher`, with factories so tests substitute fakes, in the engine and in the window alike.
- Event-driven programming without threads: Qt signals and slots, timers, asynchronous sockets and name lookups, a kernel event queue (kqueue) for another process's exit; a state machine (`Session`) that advances only when something finishes; a window that never blocks.
- A serialization boundary: value types in memory, JSON on disk, validation at the edge with messages that name the field, a schema version for the future, atomic writes and recovery that never deletes the user's data.
- A platform layer: a plain C++ interface implemented once in Objective-C++, so AppKit, blocks and automatic reference counting stay in one file, and a system completion handler that hops back onto the Qt event loop before touching any object.
- A graphical interface kept thin: Qt Widgets, layouts and dialogs that only display what the engine reports, testable on the offscreen platform without a screen.
- BSD sockets and HTTP by hand on the test side: `getaddrinfo`, `socket`, `setsockopt(SO_BROADCAST)`, `sendto`, `bind`, `recvfrom`, a fake HTTP server built on `QTcpServer`.
- Build systems: a Makefile with pattern rules in Milestone 1, then CMake targets, static libraries, Qt's automatic moc, a second language in one target, a macOS application bundle and embedded resources, with the Makefile kept as the entry point.
- Shipping software on macOS: deployment targets and how to verify them in the binaries (`vtool`), bundling frameworks with `macdeployqt`, code signing from the inside out, the hardened runtime, notarization and stapling, and Gatekeeper's assessment.

## License

MIT, see [LICENSE](./LICENSE). WakeDeck uses the Qt framework 6.11.3 under the GNU LGPL v3, linked dynamically and unmodified. The published app carries the LGPL v3 and GPL v3 texts and a third-party notice in `WakeDeck.app/Contents/Resources/Licenses` (from [packaging/licenses](./packaging/licenses/)), and the About box points there. Qt's complete source code for that version is at download.qt.io/official_releases/qt/6.11/6.11.3/single/. Qt's libraries sit in `WakeDeck.app/Contents/Frameworks` and `Contents/PlugIns` and can be replaced with your own build; because the app is signed with the hardened runtime, macOS then requires the modified app to be signed again, for example ad hoc with `codesign --force --deep --sign - WakeDeck.app`, as the notice explains.
