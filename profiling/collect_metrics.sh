#!/bin/bash
# collect_metrics.sh
# Collects /api/health metrics + system stats every 5 seconds
# Usage: collect_metrics.sh <port> <duration_seconds> <output_file>

set -e

PORT="${1:-8080}"
DURATION="${2:-300}"  # Default 5 minutes
OUTPUT="${3:-metrics.csv}"
INTERVAL=5

# Get PID of streamer process (try multiple patterns)
STREAMER_PID=$(pgrep -f "streamer|/app/streamer" | head -1)
if [ -z "$STREAMER_PID" ]; then
    echo "ERROR: streamer process not found"
    exit 1
fi

# Write CSV header
echo "timestamp,active_streams,total_packets,total_reconnects,error_streams,cpu_percent,memory_mb,disk_read_mb,disk_write_mb" > "$OUTPUT"

START_TIME=$(date +%s)
END_TIME=$((START_TIME + DURATION))

# Get baseline disk I/O stats
read disk_reads disk_writes < <(cat /proc/diskstats | awk '
    $1 ~ /^[0-9]+$/ && $2 == 0 && NF >= 11 {
        reads += $6
        writes += $10
    }
    END { printf "%d %d\n", reads, writes }
')

PREV_DISK_READS=$disk_reads
PREV_DISK_WRITES=$disk_writes
PREV_TIME=$START_TIME

while true; do
    CURRENT_TIME=$(date +%s)
    TIMESTAMP=$(date -u +'%Y-%m-%dT%H:%M:%S')

    # Get API metrics
    API_DATA=$(curl -s "http://localhost:$PORT/api/health" 2>/dev/null || echo "{}")
    
    # Extract metrics - handle both "total_packets_written" and "packets_written" field names
    ACTIVE_STREAMS=$(echo "$API_DATA" | grep -oE '"active_streams":\s*[0-9]+' | grep -oE '[0-9]+')
    TOTAL_PACKETS=$(echo "$API_DATA" | grep -oE '"total_packets_written":\s*[0-9]+|"packets_written":\s*[0-9]+' | grep -oE '[0-9]+')
    TOTAL_RECONNECTS=$(echo "$API_DATA" | grep -oE '"total_reconnects":\s*[0-9]+' | grep -oE '[0-9]+')
    ERROR_STREAMS=$(echo "$API_DATA" | grep -oE '"error_streams":\s*[0-9]+' | grep -oE '[0-9]+')

    # Default values if API unreachable
    ACTIVE_STREAMS=${ACTIVE_STREAMS:-0}
    TOTAL_PACKETS=${TOTAL_PACKETS:-0}
    TOTAL_RECONNECTS=${TOTAL_RECONNECTS:-0}
    ERROR_STREAMS=${ERROR_STREAMS:-0}

    # Get CPU usage for streamer process
    CPU_PERCENT=$(ps -p "$STREAMER_PID" -o %cpu= 2>/dev/null | awk '{printf "%.1f", $1}')
    CPU_PERCENT=${CPU_PERCENT:-0}

    # Get memory usage in MB
    MEMORY_MB=$(ps -p "$STREAMER_PID" -o rss= 2>/dev/null | awk '{printf "%.0f", $1/1024}')
    MEMORY_MB=${MEMORY_MB:-0}

    # Get disk I/O stats
    read disk_reads disk_writes < <(cat /proc/diskstats | awk '
        $1 ~ /^[0-9]+$/ && $2 == 0 && NF >= 11 {
            reads += $6
            writes += $10
        }
        END { printf "%d %d\n", reads, writes }
    ')

    # Calculate I/O rate per second
    TIME_DELTA=$((CURRENT_TIME - PREV_TIME))
    if [ $TIME_DELTA -gt 0 ]; then
        DISK_READ_MB=$(awk "BEGIN {printf \"%.1f\", ($disk_reads - $PREV_DISK_READS) / 1024 / $TIME_DELTA}")
        DISK_WRITE_MB=$(awk "BEGIN {printf \"%.1f\", ($disk_writes - $PREV_DISK_WRITES) / 1024 / $TIME_DELTA}")
    else
        DISK_READ_MB="0.0"
        DISK_WRITE_MB="0.0"
    fi

    # Write CSV line
    echo "$TIMESTAMP,$ACTIVE_STREAMS,$TOTAL_PACKETS,$TOTAL_RECONNECTS,$ERROR_STREAMS,$CPU_PERCENT,$MEMORY_MB,$DISK_READ_MB,$DISK_WRITE_MB" >> "$OUTPUT"

    # Update for next iteration
    PREV_DISK_READS=$disk_reads
    PREV_DISK_WRITES=$disk_writes
    PREV_TIME=$CURRENT_TIME

    # Check if time to exit
    if [ $CURRENT_TIME -ge $END_TIME ]; then
        break
    fi

    sleep $INTERVAL
done

echo "Metrics collected to $OUTPUT"
