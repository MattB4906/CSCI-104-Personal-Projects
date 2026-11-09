#!/bin/sh
# Exercises wakedeck-cli's argument handling, exit codes and dry-run output.
# Only --dry-run and invalid inputs are used, so nothing is resolved or sent.
# Exit codes 2 (unresolved destination) and 3 (socket failure) need a real
# network and are not covered here.
#
# Usage: sh tests/cli_test.sh path/to/wakedeck-cli
set -u
CLI=${1:?usage: cli_test.sh <path to wakedeck-cli>}
MAC=00:00:5E:00:53:2A
TO=192.168.1.255
total=0
failed=0

# expect <exit code> <description> -- <arguments...>
expect() {
    want=$1
    what=$2
    shift 3
    out=$("$CLI" "$@" 2>&1)
    got=$?
    total=$((total + 1))
    if [ "$got" -ne "$want" ]; then
        failed=$((failed + 1))
        echo "FAILED: $what: exit code $got, expected $want"
        echo "$out" | head -3
    fi
}

# contains <description> <text that must appear> -- <arguments...>
contains() {
    what=$1
    needle=$2
    shift 3
    out=$("$CLI" "$@" 2>&1)
    total=$((total + 1))
    case "$out" in
        *"$needle"*) ;;
        *) failed=$((failed + 1)); echo "FAILED: $what: output does not contain '$needle'" ;;
    esac
}

expect 0 "--help" -- --help
expect 0 "-h" -- -h
expect 0 "--version" -- --version
expect 0 "wake --help" -- wake --help
expect 0 "dry run" -- wake --mac "$MAC" --to "$TO" --dry-run

expect 1 "no arguments" --
expect 1 "unknown command" -- sleep
expect 1 "missing --mac" -- wake --to "$TO" --dry-run
expect 1 "missing --to" -- wake --mac "$MAC" --dry-run
expect 1 "--mac without a value" -- wake --to "$TO" --mac
expect 1 "invalid MAC" -- wake --mac nope --to "$TO" --dry-run
expect 1 "invalid port list" -- wake --mac "$MAC" --to "$TO" --ports 9,0 --dry-run
expect 1 "repeat 0" -- wake --mac "$MAC" --to "$TO" --repeat 0 --dry-run
expect 1 "repeat 101" -- wake --mac "$MAC" --to "$TO" --repeat 101 --dry-run
expect 1 "interval 10001" -- wake --mac "$MAC" --to "$TO" --interval-ms 10001 --dry-run
expect 1 "unknown option" -- wake --mac "$MAC" --to "$TO" --bogus --dry-run

expect 0 "check --help" -- check --help
expect 0 "connect --help" -- connect --help
expect 1 "check without host" -- check
expect 1 "check with bad port" -- check --host 192.0.2.10 --port 0
expect 1 "check with bad kind" -- check --host 192.0.2.10 --kind bogus
expect 1 "check with unknown option" -- check --host 192.0.2.10 --bogus
expect 1 "connect without check host" -- connect
expect 1 "connect with --mac but no --to" -- connect --check-host 192.0.2.10 --mac "$MAC"
expect 1 "connect with --ports but no wake" -- connect --check-host 192.0.2.10 --ports 9
expect 1 "connect with bad check kind" -- connect --check-host 192.0.2.10 --check bogus
expect 1 "connect with bad timeout" -- connect --check-host 192.0.2.10 --timeout-s 0

contains "version string" "wakedeck-cli 0.4.0-m4" -- --version
contains "help lists check" "check" -- --help
contains "help lists connect" "connect" -- --help
contains "help lists profile" "profile" -- --help
contains "help lists launch" "launch" -- --help

# The launch command, with harmless clients only: /usr/bin/true and /bin/echo.
expect 0 "launch --help" -- launch --help
expect 1 "launch without host or profile" -- launch
expect 1 "launch without client" -- launch --host 127.0.0.1
expect 1 "launch custom without path" -- launch --host 127.0.0.1 --client custom
expect 1 "launch with --store but no --profile" -- launch --host 127.0.0.1 --client custom --client-path /usr/bin/true --store /tmp/x.json
expect 0 "launch dry run of a custom client" -- launch --host 127.0.0.1 --client custom --client-path /usr/bin/true --dry-run
contains "dry run shows the program" "Would run:  /usr/bin/true" -- launch --host 127.0.0.1 --client custom --client-path /usr/bin/true --dry-run
contains "dry run substitutes the host" "/bin/echo hello 127.0.0.1" -- \
    launch --host 127.0.0.1 --client custom --client-path /bin/echo --client-args "hello {host}" --dry-run
expect 0 "launch a custom client" -- launch --host 127.0.0.1 --client custom --client-path /usr/bin/true
contains "launch reports success" "Launched custom client true" -- launch --host 127.0.0.1 --client custom --client-path /usr/bin/true
expect 2 "launch a missing custom client" -- launch --host 127.0.0.1 --client custom --client-path /nonexistent/wakedeck-client
contains "missing client is named" "not found" -- launch --host 127.0.0.1 --client custom --client-path /nonexistent/wakedeck-client
expect 1 "launch rejects a relative client path" -- launch --host 127.0.0.1 --client custom --client-path ok.sh --dry-run
contains "relative path error says absolute" "absolute" -- launch --host 127.0.0.1 --client custom --client-path ok.sh --dry-run
contains "connect help names exit codes" "2 timed out" -- connect --help

# Profiles live in a temporary store here; the real one is never touched.
STOREDIR=$(mktemp -d)
STORE="$STOREDIR/profiles.json"
EXAMPLES="$(dirname "$0")/../examples"
expect 0 "profile --help" -- profile --help
expect 1 "profile without subcommand" -- profile --store "$STORE"
expect 1 "profile unknown subcommand" -- profile frobnicate --store "$STORE"
expect 0 "profile list on a missing store" -- profile list --store "$STORE"
contains "empty list says so" "No profiles" -- profile list --store "$STORE"
expect 1 "profile add without name" -- profile add --store "$STORE" --check-host 127.0.0.1
expect 1 "profile add without check host" -- profile add --store "$STORE" --name "Test PC"
expect 1 "profile add with custom client but no path" -- profile add --store "$STORE" --name "Test PC" --check-host 127.0.0.1 --client custom
expect 1 "profile add with stream but no app" -- profile add --store "$STORE" --name "Test PC" --check-host 127.0.0.1 --client moonlight --launch stream
expect 0 "profile add" -- profile add --store "$STORE" --name "Test PC" --check-host 127.0.0.1 --check-port 1 --check tcp \
    --mac "$MAC" --to 127.0.0.1 --repeat 2 --interval-ms 0 --client moonlight --launch stream --app Desktop --notes "loopback test"
expect 2 "profile add twice without --replace" -- profile add --store "$STORE" --name "test pc" --check-host 127.0.0.1
expect 0 "profile add with --replace" -- profile add --store "$STORE" --name "Test PC" --check-host 127.0.0.1 --check-port 1 --check tcp \
    --mac "$MAC" --to 127.0.0.1 --repeat 2 --interval-ms 0 --replace
contains "profile list shows the profile" "Test PC" -- profile list --store "$STORE"
contains "profile list shows the wake" "4 packets to 127.0.0.1" -- profile list --store "$STORE"
contains "profile show prints JSON" "\"mac\": \"00:00:5E:00:53:2A\"" -- profile show "Test PC" --store "$STORE"
expect 2 "profile show unknown name" -- profile show "Nobody" --store "$STORE"
expect 1 "client path without a client" -- profile add --store "$STORE" --name "Odd" --check-host 127.0.0.1 --client-path /x
expect 0 "profile export" -- profile export "Test PC" "$STOREDIR/exported.json" --store "$STORE"
expect 2 "export over the store itself is refused" -- profile export "Test PC" "$STORE" --store "$STORE"
contains "store survives the refused export" "Test PC" -- profile list --store "$STORE"
expect 2 "a single-profile file is not a store" -- profile list --store "$STOREDIR/exported.json"
contains "single-profile file is left alone" "\"name\": \"Test PC\"" -- profile show "Test PC" --store "$STORE"
expect 2 "profile import of an existing name" -- profile import "$STOREDIR/exported.json" --store "$STORE"
expect 0 "profile import with --replace" -- profile import "$STOREDIR/exported.json" --replace --store "$STORE"
expect 0 "profile import of the example" -- profile import "$EXAMPLES/example-profile.json" --store "$STORE"
contains "example appears in the list" "Living Room PC (example)" -- profile list --store "$STORE"
expect 2 "connect with an unknown profile" -- connect --profile "Nobody" --store "$STORE"
expect 1 "connect with --profile and a check flag" -- connect --profile "Test PC" --store "$STORE" --check-host 127.0.0.1
expect 1 "connect with --store but no --profile" -- connect --check-host 127.0.0.1 --store "$STORE"
expect 2 "connect from the profile times out on loopback" -- connect --profile "Test PC" --store "$STORE" --poll-s 1 --timeout-s 1
contains "connect from the profile names it" "Profile: Test PC" -- connect --profile "test pc" --store "$STORE" --poll-s 1 --timeout-s 1
contains "connect from the profile sends the wake" "Wake request sent: 4 packets to 127.0.0.1" -- \
    connect --profile "Test PC" --store "$STORE" --poll-s 1 --timeout-s 1
expect 1 "launch with profile and client flag" -- launch --profile "Test PC" --store "$STORE" --client custom --client-path /usr/bin/true
expect 2 "launch with an unknown profile" -- launch --profile "Nobody" --store "$STORE" --dry-run
expect 1 "connect with --profile and a client flag" -- connect --profile "Test PC" --store "$STORE" --client moonlight
expect 0 "profile add with a custom client" -- profile add --store "$STORE" --name "Echo PC" --check-host 127.0.0.1 \
    --client custom --client-path /bin/echo --client-args "hi {host}"
expect 0 "launch --profile dry run" -- launch --profile "Echo PC" --store "$STORE" --dry-run
contains "launch --profile dry run shows the command" "Would run:  /bin/echo hi 127.0.0.1" -- launch --profile "Echo PC" --store "$STORE" --dry-run
expect 0 "launch --profile runs the client" -- launch --profile "Echo PC" --store "$STORE"
contains "launch --profile reports success" "Launched custom client echo hi 127.0.0.1" -- launch --profile "Echo PC" --store "$STORE"
expect 0 "profile remove" -- profile remove "Living Room PC (example)" --store "$STORE"
expect 2 "profile remove twice" -- profile remove "Living Room PC (example)" --store "$STORE"
printf 'this is not json' > "$STORE"
expect 0 "corrupt store is recovered" -- profile list --store "$STORE"
printf 'this is not json' > "$STORE"
expect 0 "corrupt store recovered again within the same second" -- profile list --store "$STORE"
printf 'this is not json' > "$STORE"
contains "corrupt store warns" "It was moved" -- profile list --store "$STORE"
rm -rf "$STOREDIR"
contains "dry run plan line" "10 packets: port 9 x5, port 7 x5, 100 ms apart" -- wake --mac "$MAC" --to "$TO" --dry-run
contains "dry run sends nothing" "nothing was sent" -- wake --mac "$MAC" --to "$TO" --dry-run
contains "dry run hex dump" "FF FF FF FF FF FF 00 00 5E 00 53 2A 00 00 5E 00" -- wake --mac "$MAC" --to "$TO" --dry-run
contains "custom plan line" "6 packets: port 9 x2, port 7 x2, port 12 x2, 0 ms apart" -- \
    wake --mac "$MAC" --to "$TO" --ports 9,7,12 --repeat 2 --interval-ms 0 --dry-run
contains "dash MAC is normalised" "Target MAC    00:00:5E:00:53:2A" -- wake --mac 00-00-5e-00-53-2a --to "$TO" --dry-run
contains "usage error names the option" "unknown option '--bogus'" -- wake --mac "$MAC" --to "$TO" --bogus
contains "usage error names the value" "'nope' is not a valid MAC address" -- wake --mac nope --to "$TO"

# End-to-end runs against loopback ports, when python3 is there to pick a free
# port and to listen on one. Nothing leaves the machine; the wake goes to 127.0.0.1.
if command -v python3 >/dev/null 2>&1; then
    CLOSED=$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')
    expect 2 "check against a closed loopback port" -- check --host 127.0.0.1 --port "$CLOSED" --kind tcp
    contains "check reports refused" "Not ready: refused" -- check --host 127.0.0.1 --port "$CLOSED" --kind tcp
    expect 2 "connect times out on a closed loopback port" -- \
        connect --check-host 127.0.0.1 --check-port "$CLOSED" --check tcp --poll-s 1 --timeout-s 1
    contains "connect explains the timeout" "What happened" -- \
        connect --check-host 127.0.0.1 --check-port "$CLOSED" --check tcp --poll-s 1 --timeout-s 1
    contains "connect reports the wake request" "Wake request sent: 4 packets to 127.0.0.1" -- \
        connect --check-host 127.0.0.1 --check-port "$CLOSED" --check tcp --poll-s 1 --timeout-s 1 \
        --mac "$MAC" --to 127.0.0.1 --repeat 2 --interval-ms 0

    PORTFILE=$(mktemp)
    python3 -I -c 'import socket,time; s=socket.socket(); s.bind(("127.0.0.1",0)); s.listen(1); print(s.getsockname()[1], flush=True); time.sleep(20)' > "$PORTFILE" &
    LISTENER=$!
    sleep 1
    OPEN=$(cat "$PORTFILE")
    expect 0 "check against an open loopback port" -- check --host 127.0.0.1 --port "$OPEN" --kind tcp
    expect 0 "connect to an open loopback port" -- connect --check-host 127.0.0.1 --check-port "$OPEN" --check tcp
    contains "connect says no wake was needed" "no wake request was needed" -- \
        connect --check-host 127.0.0.1 --check-port "$OPEN" --check tcp --mac "$MAC" --to 127.0.0.1
    expect 0 "connect launches a custom client once ready" -- \
        connect --check-host 127.0.0.1 --check-port "$OPEN" --check tcp --client custom --client-path /usr/bin/true --client-args "{host}"
    contains "connect reports the launch" "Launching custom client true 127.0.0.1" -- \
        connect --check-host 127.0.0.1 --check-port "$OPEN" --check tcp --client custom --client-path /usr/bin/true --client-args "{host}"
    expect 4 "connect fails with exit 4 when the client is missing" -- \
        connect --check-host 127.0.0.1 --check-port "$OPEN" --check tcp --client custom --client-path /nonexistent/wakedeck-client
    kill "$LISTENER" 2>/dev/null
    wait "$LISTENER" 2>/dev/null
    rm -f "$PORTFILE"
else
    echo "note: python3 not found; the loopback end-to-end cases were skipped"
fi

echo "cli_test: $((total - failed))/$total checks passed"
[ "$failed" -eq 0 ]
