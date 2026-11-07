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

contains "version string" "wakedeck-cli 0.2.0-m2" -- --version
contains "help lists check" "check" -- --help
contains "help lists connect" "connect" -- --help
contains "connect help names exit codes" "2 timed out" -- connect --help
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
    kill "$LISTENER" 2>/dev/null
    wait "$LISTENER" 2>/dev/null
    rm -f "$PORTFILE"
else
    echo "note: python3 not found; the loopback end-to-end cases were skipped"
fi

echo "cli_test: $((total - failed))/$total checks passed"
[ "$failed" -eq 0 ]
