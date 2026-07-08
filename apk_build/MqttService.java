package com.aihear.app;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Vibrator;
import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * AI Hear — MQTT 后台监护服务
 * 使用 MQTT 3.1.1 协议连接 HiveMQ 公共 Broker，订阅 aihear/alert 主题。
 * 收到 baby_cry 告警时发送系统通知 + 震动，所有告警持久化到 SQLite。
 */
public class MqttService extends Service {

    private static final String TAG = "AIMqtt";

    // ── MQTT broker 配置（可由 WebView 设置） ──
    public static String sBrokerHost = "broker-cn.emqx.io";
    public static int    sBrokerPort = 1883;
    private static final int    KEEPALIVE_SEC = 60;
    private static final String TOPIC_ALERT  = "aihear/alert";
    private static final String TOPIC_STATUS = "aihear/status";

    // ── 通知 ID：前台服务与告警分开，互不影响 ──
    private static final String CH_ID = "aihear_channel";
    private static final int    NID_FOREGROUND = 1;
    private static final int    NID_ALERT      = 2;

    // ── 告警冷却 10 秒，避免刷屏 ──
    private static final long   ALERT_COOLDOWN_MS = 10_000;

    // ── Broadcast action ──
    static final String ACTION_STOP = "com.aihear.STOP";

    // ── 线程与状态 ──
    private Thread       mqttThread;
    private volatile boolean running   = false;
    private volatile boolean connected = false;
    private volatile int     connStatus = 0;  // 0=断开 1=连接中 2=已连接
    public  static volatile int sConnStatus = 0;     // 供 WebView 读取
    public  static volatile int sMsgCount   = 0;     // 收到的 MQTT 消息计数

    private Handler   mainHandler;
    private Vibrator  vibrator;
    private long      lastAlertMs = 0;
    private AlertDbHelper db;
    private PowerManager.WakeLock wakeLock;

    // ── 对外可读状态 ──
    String lastAlertClass = "";
    double lastAlertScore = 0;
    long   lastAlertTs    = 0;

    // 10 分钟去重
    private String lastDbClass = "";
    private long   lastDbMs    = 0;

    // ═══════════════════════════════════════════════════════════════
    // 生命周期
    // ═══════════════════════════════════════════════════════════════

    @Override
    public void onCreate() {
        super.onCreate();
        Log.i(TAG, "onCreate");
        mainHandler = new Handler(Looper.getMainLooper());
        vibrator    = (Vibrator) getSystemService(VIBRATOR_SERVICE);
        db          = new AlertDbHelper(this);

        // WakeLock 防止息屏断网
        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AIHear:MQTT");
        wakeLock.acquire();

        createChannel();

        // Android 14+ 要求注册广播时指定导出标志
        IntentFilter f = new IntentFilter(ACTION_STOP);
        if (Build.VERSION.SDK_INT >= 34) {
            registerReceiver(new AlarmReceiver(), f, RECEIVER_NOT_EXPORTED);
        } else {
            registerReceiver(new AlarmReceiver(), f);
        }

        // 必须在 5 秒内调 startForeground
        startForeground(NID_FOREGROUND, buildForegroundNote());
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i(TAG, "onStartCommand flags=" + flags + " startId=" + startId);
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopAlarm();
            return START_NOT_STICKY;
        }
        // 避免重复线程
        if (mqttThread == null || !mqttThread.isAlive()) {
            startMqttLoop();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "onDestroy");
        running = false;
        if (mqttThread != null) {
            mqttThread.interrupt();
            try { mqttThread.join(2000); } catch (InterruptedException ignored) {}
        }
        try { unregisterReceiver(new AlarmReceiver()); } catch (Exception ignored) {}
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        if (db != null) { db.close(); }
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }

    // ═══════════════════════════════════════════════════════════════
    // MQTT 主循环（后台线程）
    // ═══════════════════════════════════════════════════════════════

    private void startMqttLoop() {
        running = true;
        mqttThread = new Thread(() -> {
            int backoff = 1;
            int maxBackoff = 120;

            while (running) {
                try {
                    updateConnStatus(1);
                    String host = sBrokerHost;
                    int    port = sBrokerPort;
                    Log.i(TAG, "连接 " + host + ":" + port);

                    // ── 纯 TCP Socket ──
                    java.net.Socket sock = new java.net.Socket();
                    sock.connect(new InetSocketAddress(host, port), 10_000);
                    sock.setSoTimeout(0);

                    OutputStream out = sock.getOutputStream();
                    InputStream  in  = sock.getInputStream();

                    // ── MQTT CONNECT ──
                    byte[] connectPkt = buildConnect();
                    out.write(connectPkt);
                    out.flush();

                    // 读 CONNACK（4 字节）
                    byte[] connack = readExact(in, 4);
                    if (connack == null || connack.length < 4) {
                        sock.close();
                        backoff = backoff(backoff, maxBackoff);
                        continue;
                    }
                    int connackType  = (connack[0] >> 4) & 0x0F;
                    int connackRc    = connack[3] & 0xFF;
                    if (connackType != 2 || connackRc != 0) {
                        Log.w(TAG, "CONNACK 失败 type=" + connackType + " rc=" + connackRc);
                        sock.close();
                        backoff = backoff(backoff, maxBackoff);
                        continue;
                    }
                    Log.i(TAG, "CONNACK OK");

                    // ── MQTT SUBSCRIBE ──
                    byte[] subPkt = buildSubscribe(TOPIC_ALERT);
                    out.write(subPkt);
                    out.flush();

                    // 读 SUBACK（5 字节）
                    byte[] suback = readExact(in, 5);
                    if (suback == null || suback.length < 5) {
                        sock.close();
                        backoff = backoff(backoff, maxBackoff);
                        continue;
                    }
                    int subackType = (suback[0] >> 4) & 0x0F;
                    int subackRc   = suback[4] & 0xFF;
                    if (subackType != 9 || subackRc > 2) {
                        Log.w(TAG, "SUBACK 失败 type=" + subackType + " rc=" + subackRc);
                        sock.close();
                        backoff = backoff(backoff, maxBackoff);
                        continue;
                    }
                    Log.i(TAG, "SUBACK OK — 已订阅 " + TOPIC_ALERT);

                    // ── 连接成功 ──
                    backoff = 1;
                    updateConnStatus(2);
                    lastAlertMs = 0; // 首次重连立即允许告警

                    // 心跳 timing
                    long lastPingMs = System.currentTimeMillis();
                    long keepaliveMs = KEEPALIVE_SEC * 1000L;

                    // ── 读取循环 ──
                    while (running) {
                        long now = System.currentTimeMillis();

                        // ── 发送 PINGREQ ──
                        if (now - lastPingMs >= keepaliveMs) {
                            out.write(new byte[]{(byte)0xC0, 0x00});
                            out.flush();
                            lastPingMs = now;
                            // 读 PINGRESP
                            byte[] pingResp = readExact(in, 2);
                            if (pingResp == null) {
                                Log.w(TAG, "PINGRESP 超时，重连");
                                break; // 退出内循环，触发重连
                            }
                            lastPingMs = System.currentTimeMillis(); // 收到响应重置
                        }

                        // ── 等待并读取一个 MQTT 包 ──
                        byte[] pkt = readMqttPacket(in, keepaliveMs);
                        if (pkt == null) {
                            // 超时，下一轮发 PINGREQ
                            continue;
                        }
                        lastPingMs = System.currentTimeMillis(); // 任何包都算心跳

                        int pktType = (pkt[0] >> 4) & 0x0F;
                        switch (pktType) {
                            case 3: { // PUBLISH
                                MqttPublish pub = parsePublish(pkt);
                                if (pub != null) {
                                    Log.i(TAG, "PUBLISH topic=" + pub.topic + " payload=" + pub.payload);
                                    if (TOPIC_ALERT.equals(pub.topic)) {
                                        handleAlert(pub.payload);
                                    } else if (TOPIC_STATUS.equals(pub.topic)) {
                                        Log.d(TAG, "设备状态: " + pub.payload);
                                    }
                                }
                                break;
                            }
                            case 13: // PINGRESP
                                Log.v(TAG, "PINGRESP");
                                break;
                            default:
                                Log.v(TAG, "收到 type=" + pktType);
                                break;
                        }
                    }

                    // 内循环退出 → 关闭 socket 准备重连
                    try { sock.close(); } catch (Exception ignored) {}

                } catch (Exception e) {
                    Log.e(TAG, "MQTT 异常: " + e.getMessage());
                    backoff = backoff(backoff, maxBackoff);
                }

                updateConnStatus(0);
            }
            Log.i(TAG, "MQTT 线程退出");
        }, "mqtt-thread");
        mqttThread.setDaemon(true);
        mqttThread.start();
    }

    // ═══════════════════════════════════════════════════════════════
    // MQTT 协议 — 编码
    // ═══════════════════════════════════════════════════════════════

    private byte[] buildConnect() {
        String clientId = "aihear_" + System.currentTimeMillis();
        byte[] cidBytes = clientId.getBytes();

        // Variable header: ProtocolName(6) + ProtocolLevel(1) + Flags(1) + KeepAlive(2)
        byte[] varHeader = new byte[]{
            // Protocol Name "MQTT"
            0x00, 0x04, 'M','Q','T','T',
            // Protocol Level 4 (MQTT 3.1.1)
            0x04,
            // Connect Flags: CleanSession=1
            0x02,
            // Keep Alive (seconds)
            (byte)(KEEPALIVE_SEC >> 8), (byte)(KEEPALIVE_SEC & 0xFF)
        };

        int remainingLen = varHeader.length + 2 + cidBytes.length; // +2 for clientId length
        byte[] rlBytes = encodeRemainingLength(remainingLen);

        int totalLen = 1 + rlBytes.length + remainingLen;
        byte[] pkt = new byte[totalLen];
        int pos = 0;

        // Fixed header
        pkt[pos++] = 0x10; // CONNECT
        System.arraycopy(rlBytes, 0, pkt, pos, rlBytes.length);
        pos += rlBytes.length;

        // Variable header
        System.arraycopy(varHeader, 0, pkt, pos, varHeader.length);
        pos += varHeader.length;

        // Payload: ClientId
        pkt[pos++] = (byte)(cidBytes.length >> 8);
        pkt[pos++] = (byte)(cidBytes.length & 0xFF);
        System.arraycopy(cidBytes, 0, pkt, pos, cidBytes.length);

        return pkt;
    }

    private byte[] buildSubscribe(String topic) {
        byte[] topicBytes = topic.getBytes();
        int remainingLen = 2 + 2 + topicBytes.length + 1; // pktId(2) + topicLen(2) + topic + qos(1)
        byte[] rlBytes = encodeRemainingLength(remainingLen);

        int totalLen = 1 + rlBytes.length + remainingLen;
        byte[] pkt = new byte[totalLen];
        int pos = 0;

        pkt[pos++] = (byte)0x82; // SUBSCRIBE + QoS1
        System.arraycopy(rlBytes, 0, pkt, pos, rlBytes.length);
        pos += rlBytes.length;

        // Packet Identifier
        pkt[pos++] = 0x00; pkt[pos++] = 0x01;

        // Topic Filter
        pkt[pos++] = (byte)(topicBytes.length >> 8);
        pkt[pos++] = (byte)(topicBytes.length & 0xFF);
        System.arraycopy(topicBytes, 0, pkt, pos, topicBytes.length);
        pos += topicBytes.length;

        // QoS 0
        pkt[pos] = 0x00;

        return pkt;
    }

    // ═══════════════════════════════════════════════════════════════
    // MQTT 协议 — 解码
    // ═══════════════════════════════════════════════════════════════

    /** 从流中精确读取 n 字节，支持超时 */
    private byte[] readExact(InputStream in, int n) {
        byte[] buf = new byte[n];
        int off = 0;
        long deadline = System.currentTimeMillis() + 30_000; // 30s 超时
        try {
            while (off < n && running) {
                if (System.currentTimeMillis() > deadline) return null;
                int avail = in.available();
                if (avail > 0) {
                    int r = in.read(buf, off, Math.min(n - off, avail));
                    if (r < 0) return null;
                    off += r;
                } else {
                    Thread.sleep(50);
                }
            }
            return (off == n) ? buf : null;
        } catch (Exception e) {
            return null;
        }
    }

    /**
     * 从流中读取一个完整 MQTT 包。
     * 返回完整的包字节数组，或 null 表示超时（调用者应继续循环）。
     */
    private byte[] readMqttPacket(InputStream in, long timeoutMs) {
        try {
            long deadline = System.currentTimeMillis() + timeoutMs;

            // 等待第一个字节（固定头）
            while (in.available() == 0 && running) {
                if (System.currentTimeMillis() > deadline) return null;
                Thread.sleep(50);
            }
            if (!running) return null;

            int firstByte = in.read();
            if (firstByte < 0) throw new Exception("EOF");

            // 读取 Remaining Length
            int multiplier = 1;
            int remainingLen = 0;
            int rlBytes = 0;
            while (rlBytes < 4) {
                while (in.available() == 0 && running) {
                    if (System.currentTimeMillis() > deadline + 5000) return null;
                    Thread.sleep(50);
                }
                if (!running) return null;
                int b = in.read();
                if (b < 0) throw new Exception("EOF in RL");
                remainingLen += (b & 0x7F) * multiplier;
                multiplier *= 128;
                rlBytes++;
                if ((b & 0x80) == 0) break;
            }

            // 读取剩余部分
            int totalLen = 1 + rlBytes + remainingLen;
            byte[] pkt = new byte[totalLen];
            pkt[0] = (byte) firstByte;

            // 重新编码 RL 放回包中
            byte[] rlEncoded = encodeRemainingLength(remainingLen);
            System.arraycopy(rlEncoded, 0, pkt, 1, rlEncoded.length);

            if (remainingLen > 0) {
                byte[] payload = readExact(in, remainingLen);
                if (payload == null) throw new Exception("EOF in payload");
                System.arraycopy(payload, 0, pkt, 1 + rlBytes, remainingLen);
            }
            return pkt;

        } catch (Exception e) {
            Log.w(TAG, "readMqttPacket 失败: " + e.getMessage());
            return null;
        }
    }

    /** 解析 PUBLISH 包，提取 topic 和 payload */
    private MqttPublish parsePublish(byte[] pkt) {
        try {
            int qos = (pkt[0] >> 1) & 0x03;

            // 跳过固定头和 remaining length
            int pos = 1;
            while ((pkt[pos] & 0x80) != 0) pos++;
            pos++;

            // Topic
            int topicLen = ((pkt[pos] & 0xFF) << 8) | (pkt[pos + 1] & 0xFF);
            pos += 2;
            String topic = new String(pkt, pos, topicLen, "UTF-8");
            pos += topicLen;

            // PacketId (QoS > 0)
            if (qos > 0) pos += 2;

            // Payload
            String payload = new String(pkt, pos, pkt.length - pos, "UTF-8");
            return new MqttPublish(topic, payload);

        } catch (Exception e) {
            Log.w(TAG, "parsePublish 失败: " + e.getMessage());
            return null;
        }
    }

    private static class MqttPublish {
        final String topic;
        final String payload;
        MqttPublish(String t, String p) { topic = t; payload = p; }
    }

    // ═══════════════════════════════════════════════════════════════
    // 工具方法
    // ═══════════════════════════════════════════════════════════════

    private byte[] encodeRemainingLength(int len) {
        // MQTT variable-length encoding, little-endian 7-bit groups
        java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream(4);
        do {
            int d = len % 128;
            len /= 128;
            if (len > 0) d |= 0x80;
            bos.write(d);
        } while (len > 0);
        return bos.toByteArray();
    }

    private int backoff(int current, int max) {
        int next = Math.min(current * 2, max);
        Log.i(TAG, "重连等待 " + next + "s");
        long deadline = System.currentTimeMillis() + next * 1000L;
        while (running && System.currentTimeMillis() < deadline) {
            try { Thread.sleep(500); } catch (InterruptedException e) { break; }
        }
        return next;
    }

    private SSLSocketFactory createTrustAllSslFactory() throws Exception {
        TrustManager[] trustAll = new TrustManager[]{
            new X509TrustManager() {
                public void checkClientTrusted(X509Certificate[] c, String a) {}
                public void checkServerTrusted(X509Certificate[] c, String a) {}
                public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; }
            }
        };
        SSLContext ctx = SSLContext.getInstance("TLS");
        ctx.init(null, trustAll, new java.security.SecureRandom());
        return ctx.getSocketFactory();
    }

    private void updateConnStatus(int status) {
        connStatus = status;
        sConnStatus = status;
        connected = (status == 2);
        // 更新前台通知文字
        String text = status == 2 ? "🟢 MQTT 已连接" :
                      status == 1 ? "🟡 MQTT 连接中..." : "🔴 MQTT 已断开";
        Notification note = buildNotification(text, false);
        ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).notify(NID_FOREGROUND, note);
    }

    // ═══════════════════════════════════════════════════════════════
    // 告警处理
    // ═══════════════════════════════════════════════════════════════

    private void handleAlert(String payload) {
        // 解析 class:score 格式
        String cls = payload.trim();
        double score = 0;
        int colonIdx = cls.indexOf(':');
        if (colonIdx > 0) {
            try { score = Double.parseDouble(cls.substring(colonIdx + 1).trim()); } catch (Exception e) {}
            cls = cls.substring(0, colonIdx).trim();
        }

        long now = System.currentTimeMillis();
        sMsgCount++; // MQTT 消息接收计数
        final boolean isAlert = cls.contains("baby_cry") || cls.contains("help") || cls.contains("cry");

        // ── 入库：10 分钟去重（统计用，不影响实时通知） ──
        boolean shouldInsert = !cls.equals(lastDbClass) || (now - lastDbMs) >= 600_000;
        if (shouldInsert) {
            lastDbClass = cls;
            lastDbMs    = now;
            db.insertAlert(now, cls, score, TOPIC_ALERT);
        }

        lastAlertClass = cls;
        lastAlertScore = score;
        lastAlertTs    = now;

        // ── 通知：10 秒冷却（实时告警不被去重吞掉） ──
        final String fCls = cls;
        final double fScore = score;
        final long fNow = now;

        mainHandler.post(() -> {
            // 通知所有监听者（WebView JS 桥接）
            notifyJsListeners(fCls, fScore);

            if (!isAlert) return;

            // 防抖
            if (fNow - lastAlertMs < ALERT_COOLDOWN_MS) {
                Log.d(TAG, "告警冷却中，跳过");
                return;
            }
            lastAlertMs = fNow;

            // Android 通知
            ((NotificationManager) getSystemService(NOTIFICATION_SERVICE))
                .notify(NID_ALERT, buildAlertNote(fCls, fScore));

            // 震动
            if (vibrator != null) {
                vibrator.vibrate(new long[]{0, 500, 200, 500, 200, 500, 200, 1000}, -1);
            }
        });
    }

    /** 停止告警（通知 + 震动），但不断开 MQTT */
    void stopAlarm() {
        if (vibrator != null) vibrator.cancel();
        ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).cancel(NID_ALERT);
    }

    // ═══════════════════════════════════════════════════════════════
    // JS 回调（通知 WebView 有新告警）
    // ═══════════════════════════════════════════════════════════════

    private static final List<Runnable> jsListeners = new ArrayList<>();

    static void addJsListener(Runnable r) { synchronized (jsListeners) { jsListeners.add(r); } }
    static void removeJsListener(Runnable r) { synchronized (jsListeners) { jsListeners.remove(r); } }

    private void notifyJsListeners(String cls, double score) {
        synchronized (jsListeners) {
            for (Runnable r : jsListeners) r.run();
        }
    }

    // ═══════════════════════════════════════════════════════════════
    // 通知构建
    // ═══════════════════════════════════════════════════════════════

    private void createChannel() {
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationChannel ch = new NotificationChannel(
                CH_ID, "AI Hear 监护", NotificationManager.IMPORTANCE_HIGH);
            ch.setDescription("MQTT 连接状态与告警通知");
            ch.enableVibration(true);
            ((NotificationManager) getSystemService(NOTIFICATION_SERVICE)).createNotificationChannel(ch);
        }
    }

    private Notification buildForegroundNote() {
        return buildNotification("🟡 MQTT 连接中...", false);
    }

    private Notification buildNotification(String text, boolean isAlert) {
        Intent i = new Intent(this, MainActivity.class);
        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 23) flags |= PendingIntent.FLAG_IMMUTABLE;
        PendingIntent pi = PendingIntent.getActivity(this, 0, i, flags);

        Notification.Builder b = new Notification.Builder(this, CH_ID)
            .setContentTitle("AI Hear")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentIntent(pi)
            .setOngoing(!isAlert);

        if (isAlert) {
            Intent si = new Intent(this, AlarmReceiver.class);
            si.setAction(ACTION_STOP);
            PendingIntent sp = PendingIntent.getBroadcast(this, 1, si, flags);
            b.addAction(android.R.drawable.ic_media_pause, "确认", sp);
            b.setSmallIcon(android.R.drawable.ic_dialog_alert);
            b.setPriority(Notification.PRIORITY_HIGH);
        }

        return b.build();
    }

    private Notification buildAlertNote(String cls, double score) {
        String text = String.format("🚼 检测到哭声！(%s, %.0f%%)", cls, score * 100);
        return buildNotification(text, true);
    }

    /** 静态启动辅助 */
    static void start(Context ctx) {
        Intent i = new Intent(ctx, MqttService.class);
        if (Build.VERSION.SDK_INT >= 26) {
            ctx.startForegroundService(i);
        } else {
            ctx.startService(i);
        }
    }

    static void stopService(Context ctx) {
        Intent i = new Intent(ctx, MqttService.class);
        ctx.stopService(i);
    }
}
