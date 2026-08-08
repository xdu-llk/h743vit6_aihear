#!/usr/bin/env python3
"""Publish one device-scoped AI Hear command for hardware testing."""

import argparse
import json
import time

import paho.mqtt.client as mqtt


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("command")
    parser.add_argument("device")
    parser.add_argument("--broker", default="broker-cn.emqx.io")
    parser.add_argument("--port", type=int, default=1883)
    args = parser.parse_args()

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"aihear_hwtest_{int(time.time())}",
    )
    client.connect(args.broker, args.port, 30)
    client.loop_start()
    try:
        payload = json.dumps(
            {"cmd": args.command, "device": args.device},
            separators=(",", ":"),
        )
        result = client.publish("aihear/cmd", payload)
        result.wait_for_publish(timeout=10)
        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            raise RuntimeError(f"MQTT publish failed: rc={result.rc}")
        print(f"published aihear/cmd {payload}")
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
