#!/usr/bin/env bash
# Measure the budgets from docs/PERFORMANCE.md and write a markdown report.
#
# IME acceptance is manual; it is not covered here. Run this inside a desktop
# session (Wayland or X11); the profiled process needs a display.
set -euo pipefail

# Numeric formatting must not depend on the user's locale (EPOCHREALTIME,
# awk, sort -g). LC_CTYPE stays untouched so the editor still sees a UTF-8
# locale, which matters for its IME and font handling.
export LC_NUMERIC=C

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${BIN:-$ROOT/build/txt-editor}"
FIXTURES="${FIXTURES:-/tmp/txt-editor-bench}"
RUNS="${RUNS:-10}"
OPENS="${OPENS:-3}"
IDLE_SECONDS="${IDLE_SECONDS:-10}"
OUT_FILE="${OUT_FILE:-$ROOT/docs/benchmark-$(date +%F).txt}"

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

LAST_MS=0

usage() {
    cat <<'EOF'
usage: scripts/bench.sh [--generate]

  --generate   create the 10 MB and 100 MB fixtures in $FIXTURES
  --help       show this text

Environment: BIN, FIXTURES, RUNS (startup repetitions, default 10),
OPENS (file open repetitions, default 3), IDLE_SECONDS (default 10), OUT_FILE.
EOF
}

generate() {
    mkdir -p "$FIXTURES"
    local spec bytes out
    for spec in 10485760:ten-mb.txt 104857600:hundred-mb.txt; do
        bytes="${spec%%:*}"
        out="$FIXTURES/${spec#*:}"
        echo "[bench] generating $(basename "$out") (~$((bytes / 1048576)) MiB)"
        LC_ALL=C awk -v target="$bytes" 'BEGIN {
            for (n = 1; written < target; ++n) {
                line = sprintf("line %07d: the quick brown fox 敏捷的棕色狐狸 jumps over the lazy dog\n", n)
                printf "%s", line
                written += length(line)
            }
        }' > "$out"
    done
    ls -lh "$FIXTURES"/ten-mb.txt "$FIXTURES"/hundred-mb.txt
}

# Launches the editor, waits for the profile mark, then kills it. Sets LAST_MS
# to the wall clock from fork to that mark and leaves the run log in $LOG.
run_and_wait() {
    local mark="$1" file="${2:-}" line start
    local -a cmd=("$BIN")
    if [[ -n "$file" ]]; then
        cmd+=("$file")
    fi

    : > "$LOG"
    start=$EPOCHREALTIME
    coproc APP { exec env TXT_EDITOR_PROFILE=1 "${cmd[@]}" 2>&1 >/dev/null; }
    while IFS= read -r line <&"${APP[0]}"; do
        printf '%s\n' "$line" >> "$LOG"
        if [[ "$line" == *"$mark"* ]]; then
            break
        fi
    done
    LAST_MS="$(awk -v a="$start" -v b="$EPOCHREALTIME" 'BEGIN { printf "%.1f", (b - a) * 1000 }')"
    kill "$APP_PID" 2>/dev/null || true
    wait "$APP_PID" 2>/dev/null || true
}

# Prints the t= value (in ms) of the line whose mark matches exactly. A mark
# emitted by MarkBytes() carries a " (N bytes)" suffix and also matches.
mark_ms() {
    awk -v m="$1" '
        {
            line = $0
            sub(/^.*ms /, "", line)
            if (line == m || index(line, m " ") == 1) {
                value = $0
                sub(/^.*t=/, "", value)
                sub(/ms.*$/, "", value)
                print value
                exit
            }
        }' "$LOG"
}

# Percentile of the given values: pct 50 1 2 3 ...
pct() {
    local p="$1"
    shift
    printf '%s\n' "$@" | sort -g | awk -v p="$p" '
        { v[NR] = $1 }
        END {
            i = int(p * NR / 100 + 0.999999)
            if (i < 1) i = 1
            if (i > NR) i = NR
            printf "%.1f", v[i]
        }'
}

verdict() {  # value limit
    awk -v v="$1" -v l="$2" 'BEGIN { print (v <= l) ? "OK" : "OVER BUDGET" }'
}

startup_series() {
    local i
    local -a values=()
    for ((i = 0; i < RUNS; ++i)); do
        run_and_wait "window shown"
        values+=("$LAST_MS")
        printf '[bench] startup %d/%d: %s ms\n' "$((i + 1))" "$RUNS" "$LAST_MS" >&2
    done
    COLD_P50="$(pct 50 "${values[@]}")"
    COLD_P95="$(pct 95 "${values[@]}")"
    # "Warm" drops the first launch: it is the only one that may still miss the
    # page cache for the freshly built binary.
    WARM_P50="$(pct 50 "${values[@]:1}")"
}

open_series() {
    local fixture="$1" i begin end load
    local -a values=()
    for ((i = 0; i < OPENS; ++i)); do
        run_and_wait "load done" "$fixture"
        begin="$(mark_ms "load begin")"
        end="$(mark_ms "load done")"
        if [[ -z "$begin" || -z "$end" ]]; then
            echo "[bench] no load marks in the profile log of $(basename "$fixture")" >&2
            echo "[bench] run TXT_EDITOR_PROFILE=1 $BIN $fixture and inspect stderr" >&2
            return 1
        fi
        load="$(awk -v a="$begin" -v b="$end" 'BEGIN { printf "%.1f", b - a }')"
        values+=("$load")
        printf '[bench] open %s %d/%d: %s ms\n' "$(basename "$fixture")" \
            "$((i + 1))" "$OPENS" "$load" >&2
    done
    pct 50 "${values[@]}"
}

idle_rss() {
    env TXT_EDITOR_PROFILE=1 "$BIN" "$FIXTURES/ten-mb.txt" >/dev/null 2>&1 &
    local pid=$!
    sleep "$IDLE_SECONDS"
    ps -o rss= -p "$pid" | awk '{ printf "%.0f", $1 / 1024 }'
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
}

main() {
    case "${1:-}" in
        --generate) generate; return ;;
        --help|-h) usage; return ;;
        "") ;;
        *) usage >&2; exit 2 ;;
    esac

    if [[ ! -x "$BIN" ]]; then
        echo "[bench] $BIN is missing; run ./scripts/build.sh" >&2
        exit 1
    fi
    if [[ -z "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]]; then
        echo "[bench] no display; run this inside a desktop session" >&2
        exit 1
    fi
    local fixture
    for fixture in "$FIXTURES/ten-mb.txt" "$FIXTURES/hundred-mb.txt"; do
        if [[ ! -f "$fixture" ]]; then
            echo "[bench] $fixture is missing; run ./scripts/bench.sh --generate" >&2
            exit 1
        fi
    done

    sync
    startup_series
    TEN_MB="$(open_series "$FIXTURES/ten-mb.txt")"
    HUNDRED_MB="$(open_series "$FIXTURES/hundred-mb.txt")"
    RSS_MB="$(idle_rss)"

    local wx_version compiler session distro
    wx_version="$("$ROOT/third_party/wx-install/bin/wx-config" --version 2>/dev/null || echo '?')"
    compiler="$(g++ --version | head -1)"
    session="${XDG_SESSION_TYPE:-unknown}"
    distro="$(. /etc/os-release >/dev/null 2>&1 && printf '%s' "${PRETTY_NAME:-unknown}")"

    mkdir -p "$(dirname "$OUT_FILE")"
    {
        printf '# Benchmark report — %s\n\n' "$(date +%F)"
        printf -- '- Machine: %s (%s)\n' "$(uname -srm)" "$distro"
        printf -- '- Session: %s\n' "$session"
        printf -- '- Compiler: %s\n' "$compiler"
        printf -- '- wxWidgets: %s\n' "$wx_version"
        printf -- '- Repetitions: %s startups, %s file opens, RSS after %s s idle\n' \
            "$RUNS" "$OPENS" "$IDLE_SECONDS"
        printf -- '- Definitions: cold = fork to `window shown` wall clock over all runs; '
        printf 'warm = the same excluding run 1; open = in-process `load done - load begin`; '
        printf 'RSS = VmRSS after the idle period.\n'
        printf -- '- Caveat: the page cache was not dropped (needs root), so startup values are '
        printf 'optimistic. IME checks are manual; see docs/PERFORMANCE.md.\n\n'
        printf '| Cold start p50 | Cold start p95 | Warm p50 | 10 MB open | 100 MB open | Idle RSS |\n'
        printf '|---|---|---|---|---|---|\n'
        printf '| %s ms | %s ms | %s ms | %s ms | %s ms | %s MB |\n\n' \
            "$COLD_P50" "$COLD_P95" "$WARM_P50" "$TEN_MB" "$HUNDRED_MB" "$RSS_MB"
        printf 'Budget check (docs/PERFORMANCE.md; the 100 MB file uses large-file mode):\n\n'
        printf -- '- cold start p95 %s ms <= 200 ms: %s\n' "$COLD_P95" "$(verdict "$COLD_P95" 200)"
        printf -- '- warm start p50 %s ms <= 80 ms: %s\n' "$WARM_P50" "$(verdict "$WARM_P50" 80)"
        printf -- '- 10 MB open %s ms <= 300 ms: %s\n' "$TEN_MB" "$(verdict "$TEN_MB" 300)"
        printf -- '- 100 MB open %s ms <= 1500 ms: %s\n' "$HUNDRED_MB" "$(verdict "$HUNDRED_MB" 1500)"
        printf -- '- idle RSS %s MB <= 60 MB: %s\n' "$RSS_MB" "$(verdict "$RSS_MB" 60)"
    } > "$OUT_FILE"

    echo "[bench] written to $OUT_FILE"
    printf '| %s | %s | %s | %s | %s | %s |\n' \
        "$COLD_P50" "$COLD_P95" "$WARM_P50" "$TEN_MB" "$HUNDRED_MB" "$RSS_MB"
}

main "$@"
