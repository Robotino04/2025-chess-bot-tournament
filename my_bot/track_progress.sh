#!/usr/bin/env bash
set -euo pipefail

# Hide cursor
tput civis
trap 'tput cnorm; stty sane' EXIT

# Track progress
start_time=$(date +%s.%N)
current=0
total=1

# Get terminal width
get_terminal_width() {
    tput cols
}

# Format seconds to HH:MM:SS
format_time() {
    local t=$1
    local hours=$((t / 3600))
    local minutes=$(((t % 3600) / 60))
    local seconds=$((t % 60))
    printf "%02d:%02d:%02d" "$hours" "$minutes" "$seconds"
}

# Draw colored progress bar
draw_progress_bar() {
    local percent=$1
    local elapsed=$2
    local eta=$3
    local speed=$4

    local width=$(get_terminal_width)
    local elapsed_str=$(format_time "${elapsed%.*}")
    local eta_str=$(format_time "${eta%.*}")
    local speed_str
    speed_str=$(printf "%.2f" "$speed")

    local stats=" ${percent}% | elapsed: ${elapsed_str} | ETA: ${eta_str} | speed: ${speed_str}/s "
    local stats_len=${#stats}

    local bar_width=$((width - stats_len - 2)) # 2 for brackets
    ((bar_width < 10)) && bar_width=10

    local filled=$((bar_width * percent / 100))
    local empty=$((bar_width - filled))

    local bar="\e[42m\e[30m$(printf "%0.s█" $(seq 1 $filled))\e[0m$(printf "%0.s░" $(seq 1 $empty))"

    printf "\r[%b]%b" "$bar" "$stats"
}

# Parse command output for progress
parse_output() {
    local line="$1"
    if [[ $line =~ Started[[:space:]]+game[[:space:]]+([0-9]+)[[:space:]]+of[[:space:]]+([0-9]+) ]]; then
        current=${BASH_REMATCH[1]}
        total=${BASH_REMATCH[2]}
    fi
}

# Reserve bottom line for progress bar
tput cup $(($(tput lines) - 1)) 0
tput sc

# Run command and process output
"$@" 2>&1 | while IFS= read -r line; do
    # Parse progress
    parse_output "$line"

    # Timing and stats
    now=$(date +%s.%N)
    elapsed=$(awk "BEGIN {print $now - $start_time}")
    speed=$(awk "BEGIN {if ($elapsed>0) printf \"%.2f\", $current/$elapsed; else print 0}")
    if (($(awk "BEGIN{print ($speed>0)}"))); then
        eta=$(awk "BEGIN {printf \"%.2f\", ($total - $current)/$speed}")
    else
        eta=0
    fi

    percent=$((current * 100 / total))

    # Draw progress bar at bottom
    tput rc
    draw_progress_bar "$percent" "$elapsed" "$eta" "$speed"

    # Print output above the progress bar
    tput cup $(($(tput lines) - 2)) 0
    tput el
    echo "$line"

    # Scroll output above bar
    tput csr 0 $(($(tput lines) - 2))
done

# Move cursor to next line and restore screen/cursor
echo
tput cnorm
