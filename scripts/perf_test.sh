#!/usr/bin/env bash
set -Eeuo pipefail

# Raspberry Pi RTSP server performance measurement
# Usage: ./perf_test.sh <label> <duration_s> [interval_s=1]

LABEL="${1:-}"
DURATION="${2:-}"
INTERVAL="${3:-1}"
IFACE="${IFACE:-wlan0}"
RTSP_PORT="${RTSP_PORT:-8554}"
RESULT_DIR="${RESULT_DIR:-results}"

usage() {
    echo "Usage: $0 <label> <duration_s> [interval_s=1]" >&2
    exit 2
}

[[ -n "$LABEL" && -n "$DURATION" ]] || usage
[[ "$LABEL" =~ ^[a-zA-Z0-9._-]+$ ]] || { echo "Invalid label: use letters, numbers, dot, underscore or hyphen." >&2; exit 2; }
[[ "$DURATION" =~ ^[0-9]+$ ]] && (( DURATION > 0 )) || usage
[[ "$INTERVAL" =~ ^[0-9]+([.][0-9]+)?$ ]] || usage
awk -v x="$INTERVAL" 'BEGIN { exit !(x > 0) }' || usage

mkdir -p "$RESULT_DIR"
OUT="${RESULT_DIR}/${LABEL}.csv"
if [[ -e "$OUT" ]]; then
    echo "Output already exists: $OUT (will not overwrite)" >&2
    exit 1
fi

# Find exactly one process listening on the RTSP TCP port.
mapfile -t PIDS < <(sudo ss -ltnp "sport = :${RTSP_PORT}" 2>/dev/null \
    | grep -oE 'pid=[0-9]+' | cut -d= -f2 | sort -u)
if (( ${#PIDS[@]} != 1 )); then
    echo "Expected exactly one RTSP server process listening on TCP ${RTSP_PORT}; found ${#PIDS[@]}." >&2
    echo "Check: sudo ss -ltnp 'sport = :${RTSP_PORT}'" >&2
    exit 1
fi
PID="${PIDS[0]}"
[[ -r "/proc/$PID/stat" && -r "/proc/$PID/status" ]] || { echo "Cannot read /proc/$PID" >&2; exit 1; }

read_proc_cpu_ticks() {
    # comm may contain spaces and parentheses; remove through the final ')'.
    local line rest
    IFS= read -r line < "/proc/$PID/stat" || return 1
    rest="${line##*) }"
    local -a fields
    read -r -a fields <<< "$rest"
    # rest starts at field 3 (state); utime=field 14, stime=field 15.
    echo $(( fields[11] + fields[12] ))
}

read_sys_cpu() {
    awk '/^cpu / { idle=$5+$6; total=0; for (i=2;i<=9 && i<=NF;i++) total+=$i; print total, idle; exit }' /proc/stat
}

read_status_value() {
    local key="$1"
    awk -v key="$key" '$1 == key":" {print $2; exit}' "/proc/$PID/status"
}

read_tx_bytes() {
    awk -v iface="$IFACE" '$1 ~ ("^" iface ":$") {gsub(":", "", $1); print $10; found=1} END {if (!found) exit 1}' /proc/net/dev
}

read_wifi_info() {
    local link power freq signal txrate
    link="$(iw dev "$IFACE" link 2>/dev/null || true)"
    power="$(iw "$IFACE" get power_save 2>/dev/null | awk '{print $3}' | head -n1)"
    freq="$(awk '/freq:/ {print $2; exit}' <<< "$link")"
    signal="$(awk '/signal:/ {print $2; exit}' <<< "$link")"
    txrate="$(awk '/tx bitrate:/ {print $3; exit}' <<< "$link")"
    printf '%s,%s,%s,%s' "${power:-unknown}" "${freq:-}" "${signal:-}" "${txrate:-}"
}

read_temp() {
    if command -v vcgencmd >/dev/null 2>&1; then
        vcgencmd measure_temp 2>/dev/null | sed -n 's/^temp=\([-0-9.]*\).*/\1/p'
    fi
}

read_throttled() {
    if command -v vcgencmd >/dev/null 2>&1; then
        vcgencmd get_throttled 2>/dev/null | sed -n 's/^throttled=//p'
    fi
}

# Capture static environment metadata once at the start.
WIFI_INFO="$(read_wifi_info)"
IFS=',' read -r WIFI_POWER WIFI_FREQ WIFI_SIGNAL WIFI_TXRATE <<< "$WIFI_INFO"
if [[ "$WIFI_POWER" == "on" ]]; then
    echo "WARNING: Wi-Fi power_save is ON. Consider: sudo iw $IFACE set power_save off" >&2
fi
if [[ -z "${WIFI_FREQ:-}" ]]; then
    echo "WARNING: $IFACE does not appear to be associated with a Wi-Fi AP." >&2
fi

# Record metadata as CSV comment lines, then the data header.
{
    echo "# pid=$PID"
    echo "# iface=$IFACE"
    echo "# rtsp_port=$RTSP_PORT"
    echo "# wifi_power_save=${WIFI_POWER:-unknown}"
    echo "# wifi_freq_mhz=${WIFI_FREQ:-}"
    echo "# wifi_signal_dbm=${WIFI_SIGNAL:-}"
    echo "# wifi_tx_bitrate_mbps=${WIFI_TXRATE:-}"
    echo "ts,label,rtsp_clients,proc_cpu_pct,sys_cpu_pct,rss_kb,threads,tx_kbps,temp_c,throttled"
} > "$OUT"

CLK_TCK="$(getconf CLK_TCK)"
PREV_PROC="$(read_proc_cpu_ticks)"
read -r PREV_SYS_TOTAL PREV_SYS_IDLE < <(read_sys_cpu)
PREV_TX="$(read_tx_bytes)"
PREV_TIME="$(date +%s.%N)"

COUNT=0
SUM_PROC=0; MAX_PROC=0
SUM_SYS=0; MAX_SYS=0
SUM_RSS=0; MAX_RSS=0
SUM_THREADS=0; MAX_THREADS=0
SUM_TX=0; MAX_TX=0
SUM_TEMP=0; MAX_TEMP=-999
VALID_TEMP=0
STOP=0

on_signal() { STOP=1; }
trap on_signal INT TERM

printf 'Measuring label=%s pid=%s duration=%ss interval=%ss iface=%s\n' \
    "$LABEL" "$PID" "$DURATION" "$INTERVAL" "$IFACE" >&2

START_SECONDS="$(date +%s)"
while (( STOP == 0 )); do
    sleep "$INTERVAL" &
    wait $! || true
    (( STOP == 0 )) || break

    [[ -d "/proc/$PID" ]] || { echo "RTSP server process $PID exited; stopping." >&2; break; }

    NOW="$(date +%s.%N)"
    CUR_PROC="$(read_proc_cpu_ticks)"
    read -r CUR_SYS_TOTAL CUR_SYS_IDLE < <(read_sys_cpu)
    CUR_TX="$(read_tx_bytes)"
    ELAPSED="$(awk -v n="$NOW" -v p="$PREV_TIME" 'BEGIN {printf "%.9f", n-p}')"
    awk -v e="$ELAPSED" 'BEGIN {exit !(e > 0)}' || continue

    PROC_DELTA=$(( CUR_PROC - PREV_PROC ))
    SYS_TOTAL_DELTA=$(( CUR_SYS_TOTAL - PREV_SYS_TOTAL ))
    SYS_IDLE_DELTA=$(( CUR_SYS_IDLE - PREV_SYS_IDLE ))
    TX_DELTA=$(( CUR_TX - PREV_TX ))

    PROC_CPU="$(awk -v d="$PROC_DELTA" -v hz="$CLK_TCK" -v e="$ELAPSED" 'BEGIN {printf "%.2f", d*100/(hz*e)}')"
    SYS_CPU="$(awk -v t="$SYS_TOTAL_DELTA" -v i="$SYS_IDLE_DELTA" 'BEGIN {if(t>0) printf "%.2f", (t-i)*100/t; else print ""}')"
    TX_KBPS="$(awk -v b="$TX_DELTA" -v e="$ELAPSED" 'BEGIN {printf "%.2f", b*8/e/1000}')"

    CLIENTS="$(ss -Htn state established "( sport = :$RTSP_PORT )" 2>/dev/null | wc -l)"
    if ! iw dev "$IFACE" link 2>/dev/null | grep -q '^Connected to '; then
        echo "WARNING: Wi-Fi link on $IFACE is disconnected at $NOW" >&2
    fi
    RSS="$(read_status_value VmRSS)"
    THREADS="$(read_status_value Threads)"
    TEMP="$(read_temp || true)"
    THROTTLED="$(read_throttled || true)"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$NOW" "$LABEL" "$CLIENTS" "$PROC_CPU" "$SYS_CPU" \
        "${RSS:-}" "${THREADS:-}" "$TX_KBPS" "${TEMP:-}" "${THROTTLED:-}" >> "$OUT"

    COUNT=$((COUNT + 1))
    SUM_PROC="$(awk -v a="$SUM_PROC" -v b="$PROC_CPU" 'BEGIN {printf "%.4f",a+b}')"
    MAX_PROC="$(awk -v a="$MAX_PROC" -v b="$PROC_CPU" 'BEGIN {print (a>b)?a:b}')"
    SUM_SYS="$(awk -v a="$SUM_SYS" -v b="${SYS_CPU:-0}" 'BEGIN {printf "%.4f",a+b}')"
    MAX_SYS="$(awk -v a="$MAX_SYS" -v b="${SYS_CPU:-0}" 'BEGIN {print (a>b)?a:b}')"
    SUM_RSS=$((SUM_RSS + ${RSS:-0})); (( RSS > MAX_RSS )) && MAX_RSS="$RSS"
    SUM_THREADS=$((SUM_THREADS + ${THREADS:-0})); (( THREADS > MAX_THREADS )) && MAX_THREADS="$THREADS"
    SUM_TX="$(awk -v a="$SUM_TX" -v b="$TX_KBPS" 'BEGIN {printf "%.4f",a+b}')"
    MAX_TX="$(awk -v a="$MAX_TX" -v b="$TX_KBPS" 'BEGIN {print (a>b)?a:b}')"
    if [[ -n "$TEMP" ]]; then
        SUM_TEMP="$(awk -v a="$SUM_TEMP" -v b="$TEMP" 'BEGIN {printf "%.4f",a+b}')"
        MAX_TEMP="$(awk -v a="$MAX_TEMP" -v b="$TEMP" 'BEGIN {print (a>b)?a:b}')"
        VALID_TEMP=$((VALID_TEMP + 1))
    fi

    PREV_PROC="$CUR_PROC"
    PREV_SYS_TOTAL="$CUR_SYS_TOTAL"
    PREV_SYS_IDLE="$CUR_SYS_IDLE"
    PREV_TX="$CUR_TX"
    PREV_TIME="$NOW"

    ELAPSED_TOTAL="$(awk -v n="$NOW" -v s="$START_SECONDS" 'BEGIN {print n-s}')"
    awk -v e="$ELAPSED_TOTAL" -v d="$DURATION" 'BEGIN {exit !(e >= d)}' && break
 done

if (( COUNT > 0 )); then
    AVG_PROC="$(awk -v s="$SUM_PROC" -v n="$COUNT" 'BEGIN {printf "%.2f",s/n}')"
    AVG_SYS="$(awk -v s="$SUM_SYS" -v n="$COUNT" 'BEGIN {printf "%.2f",s/n}')"
    AVG_RSS="$(awk -v s="$SUM_RSS" -v n="$COUNT" 'BEGIN {printf "%.0f",s/n}')"
    AVG_THREADS="$(awk -v s="$SUM_THREADS" -v n="$COUNT" 'BEGIN {printf "%.2f",s/n}')"
    AVG_TX="$(awk -v s="$SUM_TX" -v n="$COUNT" 'BEGIN {printf "%.2f",s/n}')"
    if (( VALID_TEMP > 0 )); then AVG_TEMP="$(awk -v s="$SUM_TEMP" -v n="$VALID_TEMP" 'BEGIN {printf "%.2f",s/n}')"; else AVG_TEMP="n/a"; MAX_TEMP="n/a"; fi
    echo "SUMMARY label=$LABEL samples=$COUNT proc_cpu_avg/max=${AVG_PROC}/${MAX_PROC}% sys_cpu_avg/max=${AVG_SYS}/${MAX_SYS}% rss_avg/max=${AVG_RSS}/${MAX_RSS}KB threads_avg/max=${AVG_THREADS}/${MAX_THREADS} tx_avg/max=${AVG_TX}/${MAX_TX}kbps temp_avg/max=${AVG_TEMP}/${MAX_TEMP}C csv=$OUT" >&2
else
    echo "No samples collected. CSV: $OUT" >&2
fi
