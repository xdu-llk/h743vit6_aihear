package com.aihear.app;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;

public class MainActivity extends Activity {

    private static final int REQ_NOTIF = 1001;
    private WebView webView;
    private AndroidBridge bridge;

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
        setContentView(webView);

        WebSettings ws = webView.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        ws.setAllowFileAccess(true);
        ws.setAllowContentAccess(true);
        ws.setMediaPlaybackRequiresUserGesture(false); // 允许自动播放音频

        webView.addJavascriptInterface(bridge, "Android");
        webView.setWebChromeClient(new WebChromeClient());

        webView.setWebViewClient(new WebViewClient() {
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
