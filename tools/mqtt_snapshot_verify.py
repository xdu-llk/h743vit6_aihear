#!/usr/bin/env python3
"""Trigger one capture and verify its complete JPEG on the MQTT broker."""

import argparse
import base64
import json
import threading
import time
import zlib

import paho.mqtt.client as mqtt


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("device")
    parser.add_argument("--broker", default="broker-cn.emqx.io")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--timeout", type=float, default=30.0)
    args = parser.parse_args()

    topic = f"aihear/v1/demo/{args.device}/snapshot"
    connected = threading.Event()
    completed = threading.Event()
    captures: dict[str, dict[str, object]] = {}
    result: dict[str, object] = {}

    def on_connect(client, userdata, flags, reason_code, properties) -> None:
        if reason_code != 0:
            result["error"] = f"MQTT connect failed: {reason_code}"
            completed.set()
            return
        client.subscribe(topic, qos=0)
        connected.set()

    def on_message(client, userdata, message) -> None:
        payload = message.payload.decode("ascii", errors="strict")
        if payload.startswith("!|"):
            result["error"] = f"capture status: {payload}"
            completed.set()
            return
        capture_id, seq_text, total_text, crc_text, encoded = payload.split("|", 4)
        seq = int(seq_text)
        total = int(total_text)
        expected_crc = int(crc_text, 16)
        state = captures.setdefault(
            capture_id,
            {"total": total, "crc": expected_crc, "chunks": {}},
        )
        if state["total"] != total or state["crc"] != expected_crc:
            result["error"] = "inconsistent snapshot metadata"
            completed.set()
            return
        chunks = state["chunks"]
        assert isinstance(chunks, dict)
        chunks[seq] = base64.b64decode(encoded, validate=True)
        if len(chunks) != total:
            return
        if any(index not in chunks for index in range(total)):
            return
        jpeg = b"".join(chunks[index] for index in range(total))
        actual_crc = zlib.crc32(jpeg) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            result["error"] = (
                f"CRC mismatch: expected={expected_crc:08x} actual={actual_crc:08x}"
            )
        elif not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
            result["error"] = "invalid JPEG SOI/EOI"
        elif len(jpeg) > 60 * 1024:
            result["error"] = f"JPEG exceeds 60KB: {len(jpeg)}"
        else:
            result.update(
                capture_id=capture_id,
                chunks=total,
                bytes=len(jpeg),
                crc=f"{actual_crc:08x}",
            )
        completed.set()

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id=f"aihear_snapshot_test_{int(time.time())}",
    )
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, args.port, 30)
    client.loop_start()
    try:
        if not connected.wait(10):
            raise RuntimeError("MQTT subscribe timed out")
        payload = json.dumps(
            {"cmd": "capture", "device": args.device}, separators=(",", ":")
        )
        publish = client.publish("aihear/cmd", payload, qos=0)
        publish.wait_for_publish(timeout=10)
        if not completed.wait(args.timeout):
            raise RuntimeError("snapshot did not complete before timeout")
        if "error" in result:
            raise RuntimeError(str(result["error"]))
        print(
            "snapshot PASS "
            f"id={result['capture_id']} chunks={result['chunks']} "
            f"bytes={result['bytes']} crc={result['crc']}"
        )
    finally:
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
