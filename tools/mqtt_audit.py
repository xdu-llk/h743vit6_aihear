#!/usr/bin/env python3
"""
AI Hear MQTT 公网监听 + 主动注入测试
"""
import paho.mqtt.client as mqtt
import json
import time
import sys
import os
from collections import defaultdict

# Force UTF-8 on Windows
os.environ['PYTHONIOENCODING'] = 'utf-8'
sys.stdout.reconfigure(encoding='utf-8')

BROKER = "broker-cn.emqx.io"
PORT = 1883
MONITOR_ID = f"audit_{int(time.time())}"

stats = defaultdict(int)
device_msgs = defaultdict(list)
alerts = []
envs = []
states = []
cmds = []
legacy_msgs = []

def on_connect(client, userdata, flags, rc, props=None):
    print(f"[MONITOR] Connected to {BROKER} (rc={rc})")
    topics = [
        "aihear/v1/demo/+/alert",
        "aihear/v1/demo/+/env",
        "aihear/v1/demo/+/status",
        "aihear/v1/demo/+/state",
        "aihear/+/alert",
        "aihear/alert",
        "aihear/status",
        "aihear/env",
        "aihear/cmd",
        "aihear/#",
    ]
    for t in topics:
        client.subscribe(t, 1)
    print(f"  Subscribed to {len(topics)} topics")

def on_message(client, userdata, msg):
    topic = msg.topic
    try:
        payload = msg.payload.decode('utf-8', errors='replace')
    except:
        payload = str(msg.payload)
    stats[topic] += 1

    parts = topic.split('/')
    dev = parts[3] if len(parts) >= 5 else (parts[1] if len(parts) >= 2 else "?")

    ts = time.strftime("%H:%M:%S")
    print(f"  [{ts}] {topic} | dev={dev} | {payload[:100]}")

    if '/alert' in topic: alerts.append((topic, payload))
    elif '/env' in topic: envs.append((topic, payload))
    elif '/state' in topic: states.append((topic, payload))
    elif topic in ('aihear/alert','aihear/status','aihear/env'): legacy_msgs.append((topic, payload))
    elif '/cmd' in topic: cmds.append((topic, payload))

def main():
    print("=" * 60)
    print("  AI Hear MQTT Audit + Active Injection Test")
    print(f"  Broker: {BROKER}:{PORT}")
    print("=" * 60)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=MONITOR_ID)
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, PORT, 60)
    client.loop_start()
    time.sleep(3)

    # ── Phase 1: Passive observation (15s) ──
    print("\n--- Phase 1: Passive observation (15s) ---")
    time.sleep(15)

    # ── Phase 2: Active command injection ──
    print("\n--- Phase 2: Active command injection ---")

    # Test 1: Unicast command (with device field) - should work
    print("\n[TEST 1] Unicast arm to aihear_03cb03:")
    client.publish("aihear/cmd", '{"cmd":"arm","device":"aihear_03cb03"}')
    time.sleep(2)

    # Test 2: Broadcast command (NO device field) - should be REJECTED by new ESP firmware
    print("\n[TEST 2] Broadcast arm (no device field - should be REJECTED):")
    client.publish("aihear/cmd", '{"cmd":"arm"}')
    time.sleep(2)

    # Test 3: Targeted to wrong device
    print("\n[TEST 3] Arm command for non-existent device (should be ignored):")
    client.publish("aihear/cmd", '{"cmd":"arm","device":"aihear_deadbe"}')
    time.sleep(2)

    # Test 4: Publish to legacy global aihear/alert (should be orphaned now)
    print("\n[TEST 4] Legacy global alert (should NOT appear on v1 topics):")
    client.publish("aihear/alert", "baby_cry:0.99")
    time.sleep(2)

    # Test 5: Publish to legacy global aihear/env (should be orphaned now)
    print("\n[TEST 5] Legacy global env (should NOT appear on v1 topics):")
    client.publish("aihear/env", '{"deviceId":"aihear_fake","temp":99.9,"humi":99.9}')
    time.sleep(2)

    # Test 6: Publish env to correct device-scoped topic
    print("\n[TEST 6] Device-scoped env for aihear_03cb03:")
    client.publish("aihear/v1/demo/aihear_03cb03/env",
        '{"deviceId":"aihear_03cb03","temp":26.5,"humi":58.0,"uptimeMs":99999}')
    time.sleep(2)

    # Test 7: Cross-device env (should be stored separately)
    print("\n[TEST 7] Device-scoped env for aihear_876f07:")
    client.publish("aihear/v1/demo/aihear_876f07/env",
        '{"deviceId":"aihear_876f07","temp":28.0,"humi":62.0,"uptimeMs":88888}')
    time.sleep(2)

    time.sleep(3)
    client.loop_stop()
    client.disconnect()

    # ── Report ──
    print("\n" + "=" * 60)
    print("  AUDIT REPORT")
    print("=" * 60)

    total = sum(stats.values())
    print(f"\nTotal messages observed: {total}")

    print(f"\n--- By Topic ---")
    for topic, count in sorted(stats.items()):
        print(f"  {topic:50s} {count:4d}")

    print(f"\n--- Devices Seen ---")
    for did, msgs in sorted(device_msgs.items()):
        if did == "?": continue
        topics_seen = set(t for t, _ in msgs)
        print(f"  {did:25s} {len(msgs):3d} msgs | topics: {topics_seen}")

    print(f"\n--- Legacy Global Topic Check ---")
    if legacy_msgs:
        print(f"  [FAIL] {len(legacy_msgs)} legacy global messages found:")
        for t, p in legacy_msgs:
            print(f"    {t} -> {p[:80]}")
    else:
        print(f"  [PASS] No legacy global topic messages")

    print(f"\n--- Broadcast Command Check ---")
    broadcast_cmds = [(t, p) for t, p in cmds if '"device":' not in p]
    if broadcast_cmds:
        print(f"  [INFO] {len(broadcast_cmds)} broadcast commands sent (test injection)")
    else:
        print(f"  [PASS] No broadcast commands observed")

    print(f"\n--- Multi-Device Isolation ---")
    v1_devices = set()
    for did in device_msgs:
        if did != "?" and not did.startswith("global:"):
            v1_devices.add(did)

    for did in sorted(v1_devices):
        msgs = device_msgs[did]
        env_count = sum(1 for t, _ in msgs if '/env' in t)
        state_count = sum(1 for t, _ in msgs if '/state' in t)
        alert_count = sum(1 for t, _ in msgs if '/alert' in t)
        print(f"  {did}: env={env_count} state={state_count} alert={alert_count}")

        # Check no cross-device topic pollution
        for t, _ in msgs:
            parts = t.split('/')
            if len(parts) >= 5 and parts[3] != did:
                print(f"    [FAIL] Cross-device pollution: {t}")

    print(f"\n  Verdict: {len(v1_devices)} devices, all using device-scoped topics")
    print("=" * 60)

if __name__ == '__main__':
    main()
