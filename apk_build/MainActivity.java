package com.aihear.app;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.net.Uri;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;

public class MainActivity extends Activity {

    private static final int REQ_NOTIF = 1001;
    private WebView webView;
    private AndroidBridge bridge;
    private MqttService.AlertListener alertListener;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // ── 全屏沉浸式 ──
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false);
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        // Android P+ 使用刘海屏区域
        if (Build.VERSION.SDK_INT >= 28) {
            getWindow().getAttributes().layoutInDisplayCutoutMode =
                android.view.WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }

        // Android 13+ 请求通知权限（不阻塞启动）
        if (Build.VERSION.SDK_INT >= 33) {
            if (checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(
                    new String[]{android.Manifest.permission.POST_NOTIFICATIONS},
                    REQ_NOTIF);
            }
        }

        webView = new WebView(this);
        bridge  = new AndroidBridge(this, webView);
        alertListener = (deviceId, cls, score) -> runOnUiThread(() -> {
            if (webView == null) return;
            String js = "if(typeof onNewAlert==='function')onNewAlert(" +
                org.json.JSONObject.quote(deviceId) + "," +
                org.json.JSONObject.quote(cls) + "," + Double.toString(score) + ");";
            webView.evaluateJavascript(js, null);
        });
        setContentView(webView);

        WebSettings ws = webView.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        ws.setAllowFileAccess(true);
        ws.setAllowContentAccess(false);
        ws.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        ws.setMediaPlaybackRequiresUserGesture(false); // 允许自动播放音频

        webView.addJavascriptInterface(bridge, "Android");
        webView.setWebChromeClient(new WebChromeClient());

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                Uri url = request.getUrl();
                boolean localApp = "file".equals(url.getScheme()) &&
                    "/android_asset/index.html".equals(url.getPath());
                boolean blank = "about".equals(url.getScheme()) &&
                    "blank".equals(url.getSchemeSpecificPart());
                return !localApp && !blank;
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                // 页面加载完成后，延迟自动恢复（等权限对话框消失）
                view.evaluateJavascript(
                    "setTimeout(function(){" +
                    "if(localStorage.getItem('started')==='1')startSvc();" +
                    "},1000);", v -> {});
            }
        });

        webView.loadUrl("file:///android_asset/index.html");
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (alertListener != null) MqttService.addJsListener(alertListener);
    }

    @Override
    protected void onStop() {
        if (alertListener != null) MqttService.removeJsListener(alertListener);
        super.onStop();
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (webView != null) {
            webView.evaluateJavascript("if(typeof refresh==='function')refresh();", v -> {});
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.removeJavascriptInterface("Android");
            webView.clearHistory();
            webView.clearCache(true);
            webView.loadUrl("about:blank");
            webView.onPause();
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }
}
