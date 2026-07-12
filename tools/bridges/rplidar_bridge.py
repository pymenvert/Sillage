#!/usr/bin/env python3
"""Bridge a Slamtec RPLIDAR (serial) into Sillage through the UDP bridge driver.

Until the native serial driver lands, this 40-line script makes any RPLIDAR a
first-class Sillage sensor:

    pip install rplidar-roboticia
    python rplidar_bridge.py COM3                  # Windows
    python rplidar_bridge.py /dev/ttyUSB0          # Linux (dialout group)

Engine side:
    sillage-engine --no-sim --udp-sensor 9911@0.2,0.2,0.0 ...

Datagram format (see engine/src/drivers/udpbridge/udp_bridge.h):
    {"a0": <start angle rad>, "da": <step rad>, "d": [<mm>, ...]}
"""
import json
import math
import socket
import sys

from rplidar import RPLidar  # type: ignore

PORT = 9911
BINS = 720  # half-degree resolution

def main() -> None:
    device = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    lidar = RPLidar(device)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"RPLIDAR {device} -> udp://{host}:{PORT} ({BINS} bins)")
    try:
        for scan in lidar.iter_scans(max_buf_meas=5000):
            distances = [0] * BINS
            for _quality, angle_deg, dist_mm in scan:
                index = int(angle_deg / 360.0 * BINS) % BINS
                if dist_mm > 0:
                    distances[index] = int(dist_mm)
            payload = json.dumps(
                {"a0": 0.0, "da": 2.0 * math.pi / BINS, "d": distances},
                separators=(",", ":"))
            sock.sendto(payload.encode(), (host, PORT))
    finally:
        lidar.stop()
        lidar.disconnect()

if __name__ == "__main__":
    main()
