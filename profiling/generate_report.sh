#!/bin/bash
# generate_report.sh
# Generates summary report from collected metrics
# Usage: generate_report.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"
REPORT_FILE="$RESULTS_DIR/REPORT.txt"

if [ ! -d "$RESULTS_DIR" ]; then
    echo "ERROR: results directory not found"
    exit 1
fi

# Function to calculate stats from CSV
analyze_csv() {
    local csv_file=$1
    local scenario=$2

    if [ ! -f "$csv_file" ]; then
        echo "WARNING: CSV file not found: $csv_file"
        return
    fi

    # Skip header, calculate stats
    local stats=$(tail -n +2 "$csv_file" | awk -F, '
        BEGIN {
            cpu_sum = 0
            mem_sum = 0
            disk_read_sum = 0
            disk_write_sum = 0
            packets_max = 0
            reconnects_max = 0
            error_count = 0
            line_count = 0
        }
        {
            if (NF >= 9) {
                cpu_sum += $6
                mem_sum += $7
                disk_read_sum += $8
                disk_write_sum += $9
                if ($3 > packets_max) packets_max = $3
                if ($4 > reconnects_max) reconnects_max = $4
                if ($5 > 0) error_count++
                line_count++
            }
        }
        END {
            if (line_count > 0) {
                printf "%.1f,%.0f,%.1f,%.1f,%d,%d,%d\n",
                    cpu_sum / line_count,
                    mem_sum / line_count,
                    disk_read_sum / line_count,
                    disk_write_sum / line_count,
                    packets_max,
                    reconnects_max,
                    error_count
            }
        }
    ')

    local cpu_avg=$(echo "$stats" | cut -d, -f1)
    local mem_avg=$(echo "$stats" | cut -d, -f2)
    local disk_read_avg=$(echo "$stats" | cut -d, -f3)
    local disk_write_avg=$(echo "$stats" | cut -d, -f4)
    local packets=$(echo "$stats" | cut -d, -f5)
    local reconnects=$(echo "$stats" | cut -d, -f6)
    local errors=$(echo "$stats" | cut -d, -f7)

    echo "Scenario: $scenario"
    echo "  CPU (avg): ${cpu_avg}%"
    echo "  Memory (avg): ${mem_avg} MB"
    echo "  Disk Read (avg): ${disk_read_avg} MB/s"
    echo "  Disk Write (avg): ${disk_write_avg} MB/s"
    echo "  Packets written: ${packets}"
    echo "  Reconnects: ${reconnects}"
    echo "  Error samples: ${errors}"
    echo ""
}

# Generate report
{
    echo "============================================"
    echo "PERFORMANCE REPORT"
    echo "============================================"
    echo ""
    echo "Generated: $(date)"
    echo ""

    for csv_file in "$RESULTS_DIR"/*_metrics.csv; do
        if [ -f "$csv_file" ]; then
            scenario=$(basename "$csv_file" _metrics.csv)
            analyze_csv "$csv_file" "$scenario"
        fi
    done

    echo "============================================"
    echo "Raw data:"
    echo "  $RESULTS_DIR/*_metrics.csv"
    echo "  $RESULTS_DIR/*.log"
    echo "============================================"
} | tee "$REPORT_FILE"

echo ""
echo "Report saved to: $REPORT_FILE"
