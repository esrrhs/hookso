#!/bin/bash
# Integration tests for hookso. Run from the repository root:
#   ./test/run_tests.sh

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOOKSO="$ROOT/hookso"
TEST_DIR="$ROOT/test"
TEST_BIN="$TEST_DIR/test"
LIBTEST="$TEST_DIR/libtest.so"
LIBTESTNEW="$TEST_DIR/libtestnew.so"
OUT="${TMPDIR:-/tmp}/hookso_target_out.$$"
ERR="${TMPDIR:-/tmp}/hookso_err.$$"
TARGET_PID=""
PASS=0
FAIL=0

pass() {
    echo "PASS: $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "FAIL: $1"
    FAIL=$((FAIL + 1))
}

cleanup() {
    if [ -n "${TARGET_PID:-}" ]; then
        kill "$TARGET_PID" 2>/dev/null || true
        wait "$TARGET_PID" 2>/dev/null || true
        TARGET_PID=""
    fi
    rm -f "$OUT" "$ERR"
}
trap cleanup EXIT

target_alive() {
    kill -0 "$TARGET_PID" 2>/dev/null
}

wait_log() {
    local pat=$1
    local i
    for i in $(seq 1 50); do
        if grep -q -- "$pat" "$OUT" 2>/dev/null; then
            return 0
        fi
        if ! target_alive; then
            echo "target died while waiting for: $pat" >&2
            cat "$OUT" >&2
            return 1
        fi
        sleep 0.1
    done
    echo "timeout waiting for: $pat" >&2
    tail -30 "$OUT" >&2
    return 1
}

wait_after() {
    local mark=$1
    local pat=$2
    local i
    for i in $(seq 1 50); do
        if tail -n +"$((mark + 1))" "$OUT" | grep -q -- "$pat"; then
            return 0
        fi
        if ! target_alive; then
            echo "target died while waiting for: $pat" >&2
            tail -30 "$OUT" >&2
            return 1
        fi
        sleep 0.1
    done
    echo "timeout waiting for output after mark: $pat" >&2
    tail -30 "$OUT" >&2
    return 1
}

run_timeout() {
    if command -v timeout >/dev/null 2>&1; then
        timeout 20 "$@"
    else
        "$@"
    fi
}

run_hookso() {
    local rc
    run_timeout "$HOOKSO" "$@" >"$ERR.out" 2>"$ERR"
    rc=$?
    if [ $rc -ne 0 ]; then
        echo "hookso failed ($rc): $*" >&2
        cat "$ERR" >&2
        return $rc
    fi
    cat "$ERR.out"
    return 0
}

retry_hookso() {
    local i rc
    for i in 1 2 3 4 5 6 7 8; do
        run_timeout "$HOOKSO" "$@" >"$ERR.out" 2>"$ERR"
        rc=$?
        if [ $rc -eq 0 ]; then
            cat "$ERR.out"
            return 0
        fi
        if grep -q "try again" "$ERR" 2>/dev/null; then
            sleep 0.05
            continue
        fi
        echo "hookso failed ($rc): $*" >&2
        cat "$ERR" >&2
        return $rc
    done
    echo "hookso retries exhausted: $*" >&2
    cat "$ERR" >&2
    return 1
}

assert_eq() {
    local name=$1
    local got=$2
    local want=$3
    if [ "$got" = "$want" ]; then
        pass "$name"
    else
        fail "$name (got '$got' want '$want')"
    fi
}

assert_has() {
    local name=$1
    local got=$2
    if [ -n "$got" ]; then
        pass "$name"
    else
        fail "$name (empty output)"
    fi
}

if [ ! -x "$HOOKSO" ]; then
    echo "missing $HOOKSO, run ./build.sh first" >&2
    exit 1
fi
if [ ! -x "$TEST_BIN" ]; then
    echo "missing $TEST_BIN, run test/build.sh first" >&2
    exit 1
fi

echo "== usage / invalid input (no attach) =="
if "$HOOKSO" >/dev/null 2>&1; then
    fail "usage no args"
else
    pass "usage no args"
fi
if "$HOOKSO" onlyone >/dev/null 2>&1; then
    fail "usage one arg"
else
    pass "usage one arg"
fi

echo "== start target =="
rm -f "$OUT"
if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL "$TEST_BIN" >"$OUT" 2>&1 &
else
    "$TEST_BIN" >"$OUT" 2>&1 &
fi
TARGET_PID=$!
sleep 0.05
if ! wait_log "libtest"; then
    fail "target started"
    echo "PASS=$PASS FAIL=$FAIL"
    exit 1
fi
pass "target started pid=$TARGET_PID"

echo "== invalid command still detaches =="
if "$HOOKSO" notacommand "$TARGET_PID" >/dev/null 2>"$ERR"; then
    fail "invalid command"
else
    pass "invalid command"
fi
if ! target_alive; then
    fail "target alive after invalid command"
else
    pass "target alive after invalid command"
fi

echo "== find (file path + soname) =="
FIND_FILE=$(run_hookso find "$TARGET_PID" "$LIBTEST" libtest) || true
assert_has "find by file path" "$FIND_FILE"
LIBTEST_ADDR=$(echo "$FIND_FILE" | awk '{print $2}')
assert_has "find address" "$LIBTEST_ADDR"

FIND_ARGS=$(run_hookso find "$TARGET_PID" "$LIBTEST" libtest_args) || true
assert_has "find libtest_args" "$FIND_ARGS"

# soname-only lookup reads ELF from memory; some loaders do not map section headers.
if FIND_MEM=$(run_timeout "$HOOKSO" find "$TARGET_PID" libtest.so libtest 2>"$ERR"); then
    pass "find by soname (mem path)"
else
    if grep -q "Input/output error" "$ERR"; then
        pass "find by soname (mem path not mapped, file path used)"
    else
        cat "$ERR" >&2
        fail "find by soname (mem path)"
    fi
fi

echo "== syscall write =="
SYS_OUT=$(run_hookso syscall "$TARGET_PID" 1 i=1 s="HOOKSO_SYSCALL_OK" i=17) || true
assert_eq "syscall write retval" "$SYS_OUT" "17"
if wait_log "HOOKSO_SYSCALL_OK"; then
    pass "syscall write visible in target"
else
    fail "syscall write visible in target"
fi

echo "== call =="
CALL_OUT=$(run_hookso call "$TARGET_PID" "$LIBTEST" libtest i=4242) || true
assert_eq "call libtest retval" "$CALL_OUT" "0"
if wait_log "libtest 4242"; then
    pass "call libtest visible"
else
    fail "call libtest visible"
fi

SUM_OUT=$(run_hookso call "$TARGET_PID" "$LIBTEST" libtest_args i=1 i=2 i=3 i=4 i=5 i=6) || true
assert_eq "call libtest_args sum" "$SUM_OUT" "21"

U64_OUT=$(run_hookso call "$TARGET_PID" "$LIBTEST" libtest_u64) || true
assert_eq "call libtest_u64 (uint64 printf)" "$U64_OUT" "4294967297"

echo "== too many args =="
if "$HOOKSO" call "$TARGET_PID" "$LIBTEST" libtest_args i=1 i=2 i=3 i=4 i=5 i=6 i=7 >/dev/null 2>"$ERR"; then
    fail "too many args rejected"
else
    pass "too many args rejected"
fi
if ! target_alive; then
    fail "target alive after too many args"
else
    pass "target alive after too many args"
fi

if "$HOOKSO" setfuncp "$TARGET_PID" notanumber 1 >/dev/null 2>"$ERR"; then
    fail "invalid number rejected"
else
    pass "invalid number rejected"
fi

echo "== arg / argp (including 4th argument rcx) =="
ARG1=$(retry_hookso arg "$TARGET_PID" "$LIBTEST" libtest 1) || true
assert_has "arg libtest 1" "$ARG1"

ARG4=$(retry_hookso arg "$TARGET_PID" "$LIBTEST" libtest_args 4) || true
assert_eq "arg libtest_args 4 (rcx)" "$ARG4" "40"

ARG6=$(retry_hookso arg "$TARGET_PID" "$LIBTEST" libtest_args 6) || true
assert_eq "arg libtest_args 6" "$ARG6" "60"

ARGP=$(retry_hookso argp "$TARGET_PID" "$LIBTEST_ADDR" 1) || true
assert_has "argp libtest 1" "$ARGP"

echo "== dlopen / dlclose =="
HANDLE=$(run_hookso dlopen "$TARGET_PID" "$LIBTESTNEW") || true
assert_has "dlopen handle" "$HANDLE"
if grep -q "libtestnew.so" "/proc/$TARGET_PID/maps"; then
    pass "dlopen mapped libtestnew.so"
else
    fail "dlopen mapped libtestnew.so"
fi
DCLOSE=$(run_hookso dlclose "$TARGET_PID" "$HANDLE") || true
assert_eq "dlclose handle echoed" "$DCLOSE" "$HANDLE"
if grep -q "libtestnew.so" "/proc/$TARGET_PID/maps"; then
    fail "dlclose unmapped libtestnew.so"
else
    pass "dlclose unmapped libtestnew.so"
fi

echo "== dlcall =="
DLCALL=$(run_hookso dlcall "$TARGET_PID" "$LIBTESTNEW" libtestnew i=777) || true
assert_eq "dlcall libtestnew retval" "$DLCALL" "0"
if wait_log "libtestnew 777"; then
    pass "dlcall libtestnew visible"
else
    fail "dlcall libtestnew visible"
fi
DLSUM=$(run_hookso dlcall "$TARGET_PID" "$LIBTESTNEW" libtestnew_sum i=1 i=2 i=3 i=4 i=5 i=6) || true
assert_eq "dlcall libtestnew_sum" "$DLSUM" "21"

echo "== replace plt (puts) + setfunc restore =="
MARK=$(wc -l < "$OUT")
REPL_PUTS=$(retry_hookso replace "$TARGET_PID" "$LIBTEST" puts "$LIBTESTNEW" putsnew) || true
assert_has "replace puts" "$REPL_PUTS"
PUTS_BACKUP=$(echo "$REPL_PUTS" | awk '{print $2}')
if wait_after "$MARK" "putsnew"; then
    pass "replace puts active"
else
    fail "replace puts active"
fi
SET_PUTS=$(run_hookso setfunc "$TARGET_PID" "$LIBTEST" puts "$PUTS_BACKUP") || true
assert_has "setfunc restore puts" "$SET_PUTS"

echo "== replace text (libtest) + setfunc restore =="
MARK=$(wc -l < "$OUT")
REPL_TEXT=$(retry_hookso replace "$TARGET_PID" "$LIBTEST" libtest "$LIBTESTNEW" libtestnew) || true
assert_has "replace libtest" "$REPL_TEXT"
TEXT_BACKUP=$(echo "$REPL_TEXT" | awk '{print $2}')
if wait_after "$MARK" "libtestnew"; then
    pass "replace libtest active"
else
    fail "replace libtest active"
fi
SET_TEXT=$(run_hookso setfunc "$TARGET_PID" "$LIBTEST" libtest "$TEXT_BACKUP") || true
assert_has "setfunc restore libtest" "$SET_TEXT"

echo "== trigger syscall/call/dlcall/dlopen/dlclose =="
TR_SYS=$(retry_hookso trigger "$TARGET_PID" "$LIBTEST" libtest syscall 1 i=1 s="HOOKSO_TRIGGER_OK" i=17) || true
assert_eq "trigger syscall retval" "$TR_SYS" "17"
if wait_log "HOOKSO_TRIGGER_OK"; then
    pass "trigger syscall visible"
else
    fail "trigger syscall visible"
fi

TR_CALL=$(retry_hookso trigger "$TARGET_PID" "$LIBTEST" libtest call "$LIBTEST" libtest @1) || true
assert_eq "trigger call retval" "$TR_CALL" "0"

TR_DLCALL=$(retry_hookso trigger "$TARGET_PID" "$LIBTEST" libtest dlcall "$LIBTESTNEW" libtestnew @1) || true
assert_eq "trigger dlcall retval" "$TR_DLCALL" "0"

TR_DLOPEN=$(retry_hookso trigger "$TARGET_PID" "$LIBTEST" libtest dlopen "$LIBTESTNEW") || true
assert_has "trigger dlopen handle" "$TR_DLOPEN"
if grep -q "libtestnew.so" "/proc/$TARGET_PID/maps"; then
    pass "trigger dlopen mapped"
else
    fail "trigger dlopen mapped"
fi
if [ -n "$TR_DLOPEN" ]; then
    TR_DLCLOSE=$(retry_hookso trigger "$TARGET_PID" "$LIBTEST" libtest dlclose "$TR_DLOPEN") || true
    assert_eq "trigger dlclose" "$TR_DLCLOSE" "$TR_DLOPEN"
else
    fail "trigger dlclose"
fi

echo "== replacep + setfuncp + triggerp =="
MYSLEEP_ADDR=$(grep "MYSLEEP_ADDR=" "$OUT" | head -1 | cut -d= -f2)
assert_has "mysleep addr" "$MYSLEEP_ADDR"

MARK=$(wc -l < "$OUT")
REPLP=$(retry_hookso replacep "$TARGET_PID" "$MYSLEEP_ADDR" "$LIBTESTNEW" mysleepnew) || true
assert_has "replacep mysleep" "$REPLP"
REPLP_ADDR=$(echo "$REPLP" | awk '{print $2}')
REPLP_BACKUP=$(echo "$REPLP" | awk '{print $3}')
if wait_after "$MARK" "mysleepnew"; then
    pass "replacep mysleep active"
else
    fail "replacep mysleep active"
fi

SETP=$(run_hookso setfuncp "$TARGET_PID" "$REPLP_ADDR" "$REPLP_BACKUP") || true
assert_has "setfuncp restore mysleep" "$SETP"

# Re-apply replacep so triggerp can hit mysleepnew? triggerp waits on original addr.
# After restore, triggerp on mysleep addr should still work.
TRP=$(retry_hookso triggerp "$TARGET_PID" "$MYSLEEP_ADDR" syscall 1 i=1 s="HOOKSO_TRIGGERP_OK" i=18) || true
assert_eq "triggerp syscall retval" "$TRP" "18"
if wait_log "HOOKSO_TRIGGERP_OK"; then
    pass "triggerp syscall visible"
else
    fail "triggerp syscall visible"
fi

echo "== target still alive =="
if target_alive; then
    pass "target still alive"
else
    fail "target still alive"
fi

echo
echo "PASS=$PASS FAIL=$FAIL"
if [ "$FAIL" -ne 0 ]; then
    echo "target log:" >&2
    tail -50 "$OUT" >&2
    exit 1
fi
exit 0
