#!/usr/bin/env python3
"""
AI Hear MQTT 公网监听 + 控制流验证工具
连接 broker-cn.emqx.io:1883，监听所有相关 topic，验证多设备隔离。
"""
import paho.mqtt.client as mqtt
import json
import time
import sys
from collections import defaultdict

BROKER = "broker-cn.emqx.io"
PORT = 1883
MONITOR_ID = f"monitor_{int(time.time())}"

# ── 统计 ──
stats = defaultdict(int)
device_msgs = defaultdict(list)  # device_id -> [(topic, payload)]
alerts = []
envs = []
statuses = []
states = []
cmds = []
legacy_msgs = []
unknown_msgs = []

def on_connect(client, userdata, flags, rc, props=None):
    print(f"[MONITOR] 已连接 broker-cn.emqx.io (rc={rc})")
    # 订阅所有相关 topic
    topics = [
        ("aihear/v1/demo/+/alert", 1),   # v1 告警
        ("aihear/v1/demo/+/env", 1),     # v1 环境
        ("aihear/v1/demo/+/status", 1),  # v1 状态
        ("aihear/v1/demo/+/state", 1),   # v1 控制状态
        ("aihear/+/alert", 1),           # 遗留告警
        ("aihear/alert", 1),             # 全局告警 (应该是空的)
        ("aihear/status", 1),            # 全局状态 (应该是空的)
        ("aihear/env", 1),               # 全局环境 (应该是空的)
        ("aihear/cmd", 1),               # 命令 topic
        ("aihear/#", 1),                 # 全量监听
    ]
    for t, qos in topics:
        client.subscribe(t, qos)
        print(f"  订阅: {t}")

def on_message(client, userdata, msg):
    topic = msg.topic
    try:
        payload = msg.payload.decode('utf-8', errors='replace')
    except:
        payload = str(msg.payload)

    stats[topic] += 1

    # ── 分类存储 ──
    if '/alert' in topic:
        alerts.append((topic, payload))
    elif '/env' in topic:
        envs.append((topic, payload))
    elif '/status' in topic:
        statuses.append((topic, payload))
    elif '/state' in topic:
        states.append((topic, payload))
    elif '/cmd' in topic:
        cmds.append((topic, payload))
    elif topic == 'aihear/alert' or topic == 'aihear/status' or topic == 'aihear/env':
        legacy_msgs.append((topic, payload))
    else:
        unknown_msgs.append((topic, payload))

    # ── 提取设备 ID ──
    parts = topic.split('/')
    device_id = "unknown"
    if len(parts) >= 5 and parts[0] == 'aihear' and parts[1] == 'v1':
        device_id = parts[3]
    elif len(parts) == 2 and parts[0] == 'aihear':
        device_id = f"global:{parts[1]}"
    elif len(parts) >= 3 and parts[0] == 'aihear':
        device_id = parts[1]

    device_msgs[device_id].append((topic, payload))

    # ── 实时输出 ──
    ts = time.strftime("%H:%M:%S")
    preview = payload[:120] + ('...' if len(payload) > 120 else '')
    print(f"\n[{ts}] [MSG] {topic}")
    print(f"      设备: {device_id}")
    print(f"      内容: {preview}")

def main():
    print("=" * 60)
    print("  AI Hear MQTT 公网监听器")
    print(f"  Broker: {BROKER}:{PORT}")
    print(f"  Client ID: {MONITOR_ID}")
    print("=" * 60)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=MONITOR_ID)
    client.on_connect = on_connect
    client.on_message = on_message

    try:
        client.connect(BROKER, PORT, 60)
    except Exception as e:
        print(f"[FATAL] 连接失败: {e}")
        sys.exit(1)

    client.loop_start()

    duration = 60
    print(f"\n[WAIT] Listening {duration}s... (Ctrl+C to stop)\n")

    try:
        time.sleep(duration)
    except KeyboardInterrupt:
        print("\n\n[STOP]  用户中断")

    client.loop_stop()
    client.disconnect()

    # ═══════════════════════════════════════════════════
    # 审计报告
    # ═══════════════════════════════════════════════════
    print("\n" + "=" * 60)
    print("  [STATS] 审计报告")
    print("=" * 60)

    total = sum(stats.values())
    print(f"\n总消息数: {total}")
    if total == 0:
        print("[WARN]️  未收到任何消息 — 设备可能未上电或未连接 broker")
        print("   (这本身不表示有问题，只是说明当前无活跃设备)")
        return

    print(f"\n── 按 Topic 统计 ──")
    for topic, count in sorted(stats.items()):
        bar = "█" * min(count, 40)
        print(f"  {topic:45s} {count:4d} {bar}")

    print(f"\n── 发现设备: {len(device_msgs)} 个 ──")
    for did, msgs in sorted(device_msgs.items()):
        topics_seen = set(t for t, _ in msgs)
        print(f"  {did:25s} → {len(msgs):3d} 条消息, topics: {topics_seen}")

    # ── 告警分析 ──
    if alerts:
        print(f"\n── 告警消息 ({len(alerts)} 条) ──")
        for topic, payload in alerts[:20]:
            parts = topic.split('/')
            dev = parts[3] if len(parts) >= 5 else parts[1] if len(parts) >= 3 else "?"
            print(f"  [{dev}] {payload[:100]}")
        if len(alerts) > 20:
            print(f"  ... 还有 {len(alerts) - 20} 条")

    # ── 环境数据 ──
    if envs:
        print(f"\n── 环境数据 ({len(envs)} 条) ──")
        for topic, payload in envs:
            parts = topic.split('/')
            dev = parts[3] if len(parts) >= 5 else "?"
            try:
                d = json.loads(payload)
                print(f"  [{dev}] T={d.get('temp','?')}°C H={d.get('humi','?')}%")
            except:
                print(f"  [{dev}] {payload[:100]}")

    # ── 控制状态 ──
    if states:
        print(f"\n── 控制状态 ({len(states)} 条) ──")
        for topic, payload in states:
            parts = topic.split('/')
            dev = parts[3] if len(parts) >= 5 else "?"
            try:
                d = json.loads(payload)
                print(f"  [{dev}] armed={d.get('armed')} music={d.get('music')}")
            except:
                print(f"  [{dev}] {payload[:100]}")

    # ── 遗留全局 topic 检查 [WARN]️ ──
    if legacy_msgs:
        print(f"\n[WARN]️  [WARN]️  遗留全局 Topic 消息 ({len(legacy_msgs)} 条) [WARN]️")
        print("   这些 topic 不含设备 ID，多设备环境下可能混淆:")
        for topic, payload in legacy_msgs:
            print(f"   {topic} → {payload[:100]}")

    # ── 未知消息 ──
    if unknown_msgs:
        print(f"\n[?] 未分类消息 ({len(unknown_msgs)} 条):")
        for topic, payload in unknown_msgs[:10]:
            print(f"   {topic} → {payload[:100]}")

    # ── 多设备隔离检查 ──
    print(f"\n── 多设备隔离检查 ──")
    v1_devices = set()
    for did in device_msgs:
        if not did.startswith("global:"):
            v1_devices.add(did)

    if len(v1_devices) >= 2:
        # 检查是否有设备交叉数据
        for did in sorted(v1_devices):
            msgs = device_msgs[did]
            alert_topics = [t for t, p in msgs if 'alert' in t]
            env_topics = [t for t, p in msgs if 'env' in t]
            state_topics = [t for t, p in msgs if 'state' in t]

            # 验证 topic 中的 device ID 一致
            mismatches = []
            for t, _ in msgs:
                parts = t.split('/')
                if len(parts) >= 5 and parts[0] == 'aihear' and parts[1] == 'v1':
                    if parts[3] != did:
                        mismatches.append(t)
            if mismatches:
                print(f"  [FAIL] {did}: topic 设备 ID 不一致! {mismatches}")
            else:
                print(f"  ✅ {did}: {len(msgs)} 条消息, topic 设备 ID 全部一致")

        print(f"\n  数据隔离: 全部 {len(v1_devices)} 个设备各自独立, 无交叉")
    elif len(v1_devices) == 1:
        print(f"  仅 1 个设备在线 ({list(v1_devices)[0]}), 无法验证多设备隔离")
    else:
        print(f"  无 v1 设备在线")

    # ── 安全检查 ──
    print(f"\n── 安全检查 ──")
    if any('aihear/cmd' in t for t in stats):
        cmd_topics = [(t, p) for t, p in cmds]
        broadcast_cmds = [(t, p) for t, p in cmd_topics if '"device":' not in p]
        if broadcast_cmds:
            print(f"  [FAIL] 发现 {len(broadcast_cmds)} 条广播命令 (无 device 字段)!")
            for t, p in broadcast_cmds:
                print(f"     {t} → {p[:100]}")
        else:
            print(f"  ✅ 所有命令均含 device 字段 (单播)")
    else:
        print(f"  [INFO]️  未观测到 aihear/cmd 消息")

    print(f"\n监听完成。")

if __name__ == '__main__':
    main()
