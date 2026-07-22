#!/usr/bin/env python3
"""
AI Hear 多设备端到端验证 — 告警/环境/音乐/布防 全部控制流
"""
import paho.mqtt.client as mqtt
import json
import time
import sys
import os
from collections import defaultdict

os.environ['PYTHONIOENCODING'] = 'utf-8'
sys.stdout.reconfigure(encoding='utf-8')

BROKER = "broker-cn.emqx.io"
PORT = 1883
AUDIT_ID = f"e2e_test_{int(time.time())}"

# Track per-device messages
log = defaultdict(list)

def on_msg(c, u, m):
    t = m.topic
    p = m.payload.decode(errors='replace')
    ts = time.strftime("%H:%M:%S")

    # Extract device
    parts = t.split('/')
    if len(parts) >= 5 and parts[0] == 'aihear' and parts[1] == 'v1':
        dev = parts[3]
        ch = parts[4]
    else:
        dev = "global"
        ch = t

    log[dev].append((ts, ch, p[:150]))

    # Color-code
    if ch == 'alert':   icon = '[ALERT]'
    elif ch == 'env':   icon = '[ENV]  '
    elif ch == 'state': icon = '[STATE]'
    elif ch == 'status':icon = '[STATUS]'
    elif ch == 'cmd':   icon = '[CMD]  '
    else:               icon = '[?]    '

    print(f"  [{ts}] {icon} {dev:20s} | {p[:100]}")

def publish_and_wait(c, topic, payload, desc, wait=3):
    print(f"\n>>> {desc}")
    print(f"    publish: {topic} -> {payload[:80]}")
    c.publish(topic, payload)
    time.sleep(wait)

def main():
    print("=" * 65)
    print("  AI Hear — 多设备端到端控制逻辑验证")
    print(f"  Broker: {BROKER}:{PORT}")
    print(f"  Device A: aihear_876f07")
    print(f"  Device B: aihear_03cb03")
    print("=" * 65)

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    c.on_message = on_msg
    c.connect(BROKER, PORT, 60)
    c.subscribe("aihear/#", 1)
    c.loop_start()
    time.sleep(3)

    # ═══════════════════════════════════════════════
    # TEST 1: 环境数据隔离
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 40)
    print("  TEST 1: 环境数据 — 设备级隔离")
    print("=" * 40)

    # Inject env for device A (Device A)
    publish_and_wait(c,
        "aihear/v1/demo/aihear_876f07/env",
        '{"deviceId":"aihear_876f07","temp":28.5,"humi":55.0,"uptimeMs":99999}',
        "T1.1: 注入 A(876f07) 环境数据到设备级 topic")

    # Inject env for device B (legacy)
    publish_and_wait(c,
        "aihear/v1/demo/aihear_03cb03/env",
        '{"deviceId":"aihear_03cb03","temp":22.0,"humi":70.0,"uptimeMs":99999}',
        "T1.2: 注入 B(03cb03) 环境数据到设备级 topic")

    # Check: old firmware publishes to global aihear/env
    print("\n    [CHECK] 等待设备自身环境上报...")
    time.sleep(8)

    # ═══════════════════════════════════════════════
    # TEST 2: 告警 — 设备级隔离
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 40)
    print("  TEST 2: 告警 — 设备级隔离")
    print("=" * 40)

    publish_and_wait(c,
        "aihear/v1/demo/aihear_876f07/alert",
        '{"eventId":"test-001","class":"baby_cry","score":0.95,"uptimeMs":99999}',
        "T2.1: 模拟 A(876f07) 婴儿哭声告警")

    publish_and_wait(c,
        "aihear/v1/demo/aihear_03cb03/alert",
        '{"eventId":"test-002","class":"glass_break","score":0.85,"uptimeMs":99999}',
        "T2.2: 模拟 B(03cb03) 玻璃破碎告警")

    # Try legacy global alert (should be orphaned for Device A)
    publish_and_wait(c,
        "aihear/alert",
        "baby_cry:0.99",
        "T2.3: 遗留全局 aihear/alert (应为孤儿)")

    # ═══════════════════════════════════════════════
    # TEST 3: 布防/撤防 — 命令单播
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 40)
    print("  TEST 3: 布防/撤防 — 单播命令")
    print("=" * 40)

    # Unicast disarm to A
    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"disarm","device":"aihear_876f07"}',
        "T3.1: 单播撤防 A(876f07)")

    # Unicast arm to A
    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"arm","device":"aihear_876f07"}',
        "T3.2: 单播布防 A(876f07)")

    # Unicast disarm to B
    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"disarm","device":"aihear_03cb03"}',
        "T3.3: 单播撤防 B(03cb03)")

    # Unicast arm to B
    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"arm","device":"aihear_03cb03"}',
        "T3.4: 单播布防 B(03cb03)")

    # ═══════════════════════════════════════════════
    # TEST 4: 音乐 — 单播命令
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 40)
    print("  TEST 4: 音乐播放/停止 — 单播命令")
    print("=" * 40)

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"play_music","device":"aihear_876f07"}',
        "T4.1: 单播播放音乐 A(876f07)")

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"play_music","device":"aihear_03cb03"}',
        "T4.2: 单播播放音乐 B(03cb03)")

    time.sleep(3)

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"stop_music","device":"aihear_876f07"}',
        "T4.3: 单播停止音乐 A(876f07)")

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"stop_music","device":"aihear_03cb03"}',
        "T4.4: 单播停止音乐 B(03cb03)")

    # ═══════════════════════════════════════════════
    # TEST 5: 广播命令 — 应被新固件拒绝
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 40)
    print("  TEST 5: 广播命令 — 新固件应拒绝")
    print("=" * 40)

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"disarm"}',
        "T5.1: 广播撤防 (无device字段 — 新fw应拒绝, 旧fw会接受!)")

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"arm","device":"aihear_deadbeef"}',
        "T5.2: 发给不存在设备 deadbeef (两台都应忽略)")

    # ═══════════════════════════════════════════════
    # TEST 6: 多设备状态隔离 (sendCmdAll 模拟)
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 40)
    print("  TEST 6: 一键布防模拟 (逐个单播)")
    print("=" * 40)

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"arm","device":"aihear_876f07"}',
        "T6.1: arm A")
    time.sleep(1)

    publish_and_wait(c,
        "aihear/cmd",
        '{"cmd":"arm","device":"aihear_03cb03"}',
        "T6.2: arm B")

    time.sleep(5)
    c.loop_stop()
    c.disconnect()

    # ═══════════════════════════════════════════════
    # REPORT
    # ═══════════════════════════════════════════════
    print("\n" + "=" * 65)
    print("  VERIFICATION REPORT")
    print("=" * 65)

    # Per-device analysis
    for dev in sorted(log.keys()):
        msgs = log[dev]
        alert_msgs = [(t, c, p) for t, c, p in msgs if c == 'alert']
        env_msgs   = [(t, c, p) for t, c, p in msgs if c == 'env']
        state_msgs = [(t, c, p) for t, c, p in msgs if c == 'state']
        cmd_msgs   = [(t, c, p) for t, c, p in msgs if c == 'cmd']

        print(f"\n  Device: {dev} ({len(msgs)} msgs)")
        print(f"    alert={len(alert_msgs)} env={len(env_msgs)} state={len(state_msgs)} cmd={len(cmd_msgs)}")

        if state_msgs:
            for _, _, p in state_msgs[-3:]:
                try:
                    d = json.loads(p)
                    print(f"      state: armed={d.get('armed')} music={d.get('music')}")
                except:
                    pass

    # Isolation check
    print(f"\n  --- Isolation Check ---")
    a_env = [(t, p) for t, c, p in log.get('aihear_876f07', []) if c == 'env']
    b_env = [(t, p) for t, c, p in log.get('aihear_03cb03', []) if c == 'env']

    a_has_b_data = any('03cb03' in p for _, p in a_env)
    b_has_a_data = any('876f07' in p for _, p in b_env)

    if a_has_b_data: print("  [FAIL] A(876f07) env contains B data")
    else:            print("  [PASS] A(876f07) env isolated")

    if b_has_a_data: print("  [FAIL] B(03cb03) env contains A data")
    else:            print("  [PASS] B(03cb03) env isolated")

    # Global topic check
    global_msgs = log.get('global', [])
    global_topics = set(c for _, c, _ in global_msgs)
    print(f"\n  --- Global Topic Check ---")
    print(f"  Global topics: {global_topics}")
    if 'alert' in global_topics: print("  [WARN] aihear/alert still active (legacy)")
    if 'env' in global_topics:   print("  [WARN] aihear/env still active (legacy)")
    if 'status' in global_topics: print("  [WARN] aihear/status still active (legacy)")

    # Broadcast command check
    bcast_in_log = any('disarm' in p and '"device":' not in p
                       for _, c, p in log.get('aihear/cmd', []) + global_msgs
                       if c in ('cmd', 'aihear/cmd'))
    print(f"\n  --- Broadcast Command ---")
    print(f"  Broadcast sent: True (injected)")
    print(f"  [CHECK] Both devices should have REJECTED broadcast (no device field)")

    print("\n" + "=" * 65)
    print("  Test complete. Check device states above for correctness.")
    print("=" * 65)

if __name__ == '__main__':
    main()
