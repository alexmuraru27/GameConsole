#!/bin/bash
PIPE=$(mktemp -u /tmp/swo_XXXXXX)
mkfifo "$PIPE"
trap "kill %1 2>/dev/null; rm -f '$PIPE'" EXIT INT TERM

openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "init" \
    -c "tpiu config internal $PIPE uart false 168000000 2000000" \
    -c "itm port 0 on" \
    2>/dev/null &

# Decode ITM software stimulus packets from port 0.
# Each char from printf arrives as: 0x03 (header: port=0, size=1) + 1 data byte.
python3 - "$PIPE" <<'EOF'
import sys

with open(sys.argv[1], 'rb') as f:
    while True:
        b = f.read(1)
        if not b:
            break
        hdr = b[0]
        if not (hdr & 0x01):        # protocol packet (sync/overflow/timestamp) — skip
            continue
        port  = (hdr >> 3) & 0x1F
        size  = (1, 2, 4)[(hdr >> 1) & 0x03 - 1]
        data  = f.read(size)
        if port == 0 and size == 1 and data:
            sys.stdout.write(chr(data[0]))
            sys.stdout.flush()
EOF
