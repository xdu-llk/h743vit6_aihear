package com.aihear.app;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * 接收停止告警广播。
 * 由通知栏的"确认"按钮或 WebView 的停止按钮触发。
 */
public class AlarmReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (MqttService.ACTION_STOP.equals(intent.getAction())) {
            // 发广播给 Service（Service 内部 registerReceiver 监听了这个 action）
            Intent toService = new Intent(context, MqttService.class);
            toService.setAction(MqttService.ACTION_STOP);
            context.startService(toService);
        }
    }
}
