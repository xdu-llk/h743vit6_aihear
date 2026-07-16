package com.aihear.app;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;
import android.util.Log;
import java.lang.ref.WeakReference;
import org.json.JSONArray;

public class AndroidBridge {

    private final WeakReference<Activity> activityRef;
    private final WebView webView;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    public AndroidBridge(Activity activity, WebView wv) {
        this.activityRef = new WeakReference<>(activity);
        this.webView = wv;
    }

    private Activity getActivity() {
        Activity a = activityRef.get();
        if (a == null || a.isFinishing() || a.isDestroyed()) return null;
        return a;
    }

    // ─── 服务控制（全包裹 try-catch） ───

    @JavascriptInterface
    public void startService() {
        mainHandler.post(() -> {
            try {
                Activity a = getActivity();
                if (a != null) MqttService.start(a);
            } catch (Exception e) {
                Log.e("AIBridge", "startService error: " + e.getMessage());
                toast("启动失败: " + e.getMessage());
            }
        });
    }

    @JavascriptInterface
    public void stopService() {
        mainHandler.post(() -> {
            try {
                Activity a = getActivity();
                if (a != null) MqttService.stopService(a);
            } catch (Exception e) {
                Log.e("AIBridge", "stopService error: " + e.getMessage());
            }
        });
    }

    @JavascriptInterface
    public void stopAlarm() {
        mainHandler.post(() -> {
            try {
                Activity a = getActivity();
                if (a != null) {
                    android.content.Intent i = new android.content.Intent(MqttService.ACTION_STOP);
                    a.sendBroadcast(i);
                }
            } catch (Exception e) {
                Log.e("AIBridge", "stopAlarm error: " + e.getMessage());
            }
        });
    }

    // ─── 告警数据 ───

    @JavascriptInterface
    public String getBoundDevices() {
        try {
            Activity a = getActivity();
            return a == null ? "[]" : DeviceRegistry.getDevices(a).toString();
        } catch (Exception e) { return "[]"; }
    }

    @JavascriptInterface
    public boolean addBoundDevice(String deviceId) {
        Activity a = getActivity();
        return a != null && DeviceRegistry.add(a, deviceId);
    }

    @JavascriptInterface
    public boolean removeBoundDevice(String deviceId) {
        Activity a = getActivity();
        return a != null && DeviceRegistry.remove(a, deviceId);
    }

    @JavascriptInterface
    public boolean setDeviceAlias(String deviceId, String alias) {
        Activity a = getActivity();
        return a != null && DeviceRegistry.setAlias(a, deviceId, alias);
    }

    @JavascriptInterface
    public String getDeviceSummaries() {
        JSONArray result = new JSONArray();
        AlertDbHelper db = null;
        try {
            Activity a = getActivity();
            if (a == null) return "[]";
            db = new AlertDbHelper(a);
            for (String id : DeviceRegistry.getDeviceSet(a)) {
                org.json.JSONObject item = new org.json.JSONObject();
                item.put("deviceId", id);
                item.put("alias", DeviceRegistry.getAlias(a, id));
                item.put("lastSeen", MqttService.getDeviceLastSeen(id));
                item.put("todayAlerts", db.countAlertsTodayForDevice(id));
                JSONArray latest = db.getAlertsForDevice(id, 1);
                if (latest.length() > 0) item.put("lastAlert", latest.getJSONObject(0));
                result.put(item);
            }
            return result.toString();
        } catch (Exception e) {
            return "[]";
        } finally {
            if (db != null) db.close();
        }
    }

    @JavascriptInterface
    public String getAlerts() {
        try {
            Activity a = getActivity();
            if (a == null) return "[]";
            AlertDbHelper db = new AlertDbHelper(a);
            String json = db.getAlerts(100).toString();
            db.close();
            return json;
        } catch (Exception e) { return "[]"; }
    }

    @JavascriptInterface
    public String getAlertsForDevice(String deviceId) {
        try {
            Activity a = getActivity();
            String id = DeviceRegistry.normalize(deviceId);
            if (a == null || id.isEmpty()) return "[]";
            AlertDbHelper db = new AlertDbHelper(a);
            String json = db.getAlertsForDevice(id, 500).toString();
            db.close();
            return json;
        } catch (Exception e) { return "[]"; }
    }

    @JavascriptInterface
    public void deleteAlert(long id) {
        try {
            Activity a = getActivity();
            if (a == null) return;
            AlertDbHelper db = new AlertDbHelper(a);
            db.deleteAlert(id);
            db.close();
        } catch (Exception e) {}
    }

    @JavascriptInterface
    public void clearAlerts() {
        try {
            Activity a = getActivity();
            if (a == null) return;
            AlertDbHelper db = new AlertDbHelper(a);
            db.clearAlerts();
            db.close();
        } catch (Exception e) {}
    }

    @JavascriptInterface
    public void clearAlertsForDevice(String deviceId) {
        try {
            Activity a = getActivity();
            String id = DeviceRegistry.normalize(deviceId);
            if (a == null || id.isEmpty()) return;
            AlertDbHelper db = new AlertDbHelper(a);
            db.clearAlertsForDevice(id);
            db.close();
        } catch (Exception e) {}
    }

    // ─── 喂养记录 ───

    @JavascriptInterface
    public String getFeedings() {
        try {
            Activity a = getActivity();
            if (a == null) return "[]";
            AlertDbHelper db = new AlertDbHelper(a);
            String json = db.getFeedings(100).toString();
            db.close();
            return json;
        } catch (Exception e) { return "[]"; }
    }

    @JavascriptInterface
    public String getFeedingsForDevice(String deviceId) {
        try {
            Activity a = getActivity();
            String id = DeviceRegistry.normalize(deviceId);
            if (a == null || id.isEmpty()) return "[]";
            AlertDbHelper db = new AlertDbHelper(a);
            String json = db.getFeedingsForDevice(id, 100).toString();
            db.close();
            return json;
        } catch (Exception e) { return "[]"; }
    }

    @JavascriptInterface
    public void addFeeding(String deviceId, String note) {
        try {
            Activity a = getActivity();
            String id = DeviceRegistry.normalize(deviceId);
            if (a == null || id.isEmpty()) return;
            AlertDbHelper db = new AlertDbHelper(a);
            db.insertFeeding(System.currentTimeMillis(), id, note == null ? "" : note);
            db.close();
        } catch (Exception e) {}
    }

    @JavascriptInterface
    public void deleteFeeding(long id) {
        try {
            Activity a = getActivity();
            if (a == null) return;
            AlertDbHelper db = new AlertDbHelper(a);
            db.deleteFeeding(id);
            db.close();
        } catch (Exception e) {}
    }

    // ─── 统计 ───

    @JavascriptInterface
    public int getTodayAlerts() {
        try {
            Activity a = getActivity();
            if (a == null) return 0;
            AlertDbHelper db = new AlertDbHelper(a);
            int n = db.countAlertsToday();
            db.close();
            return n;
        } catch (Exception e) { return 0; }
    }

    @JavascriptInterface
    public int getTodayAlertsForDevice(String deviceId) {
        try {
            Activity a = getActivity();
            String id = DeviceRegistry.normalize(deviceId);
            if (a == null || id.isEmpty()) return 0;
            AlertDbHelper db = new AlertDbHelper(a);
            int n = db.countAlertsTodayForDevice(id);
            db.close();
            return n;
        } catch (Exception e) { return 0; }
    }

    @JavascriptInterface
    public long getDeviceLastSeen(String deviceId) {
        return MqttService.getDeviceLastSeen(deviceId);
    }

    @JavascriptInterface
    public int getTodayFeedings() {
        try {
            Activity a = getActivity();
            if (a == null) return 0;
            AlertDbHelper db = new AlertDbHelper(a);
            int n = db.countFeedingToday();
            db.close();
            return n;
        } catch (Exception e) { return 0; }
    }

    @JavascriptInterface
    public int getTodayFeedingsForDevice(String deviceId) {
        try {
            Activity a = getActivity();
            String id = DeviceRegistry.normalize(deviceId);
            if (a == null || id.isEmpty()) return 0;
            AlertDbHelper db = new AlertDbHelper(a);
            int n = db.countFeedingTodayForDevice(id);
            db.close();
            return n;
        } catch (Exception e) { return 0; }
    }

    // ─── 连接状态 ───

    @JavascriptInterface
    public int getConnStatus() {
        return MqttService.sConnStatus;
    }

    @JavascriptInterface
    public int getMsgCount() {
        return MqttService.sMsgCount;
    }

    // ─── 错误提示 ───

    private void toast(String msg) {
        mainHandler.post(() -> {
            String js = "if(typeof showError==='function')showError('" +
                msg.replace("\\","\\\\").replace("'","\\'") + "');";
            webView.evaluateJavascript(js, null);
        });
    }
}
