package com.aihear.app;

import android.content.Context;
import android.content.SharedPreferences;
import org.json.JSONArray;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;

final class DeviceRegistry {
    private static final String PREFS = "aihear_devices";
    private static final String KEY_DEVICES = "bound_devices";
    private static final String KEY_ALIAS_PREFIX = "alias_";

    private DeviceRegistry() {}

    static String normalize(String raw) {
        if (raw == null) return "";
        String id = raw.trim().toLowerCase(Locale.US);
        if (id.matches("[0-9a-f]{6}")) id = "aihear_" + id;
        return id.matches("aihear_[0-9a-f]{6}") ? id : "";
    }

    static Set<String> getDeviceSet(Context ctx) {
        SharedPreferences prefs = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        Set<String> saved = prefs.getStringSet(KEY_DEVICES, null);
        return saved == null ? new TreeSet<>() : new TreeSet<>(saved);
    }

    static JSONArray getDevices(Context ctx) {
        JSONArray result = new JSONArray();
        for (String id : getDeviceSet(ctx)) result.put(id);
        return result;
    }

    static boolean add(Context ctx, String raw) {
        String id = normalize(raw);
        if (id.isEmpty()) return false;
        Set<String> devices = getDeviceSet(ctx);
        boolean firstDevice = devices.isEmpty();
        boolean changed = devices.add(id);
        if (changed) {
            save(ctx, devices);
            if (firstDevice) {
                AlertDbHelper db = new AlertDbHelper(ctx);
                db.assignLegacyAlertsToDevice(id);
                db.close();
            }
        }
        return true;
    }

    static boolean remove(Context ctx, String raw) {
        String id = normalize(raw);
        if (id.isEmpty()) return false;
        Set<String> devices = getDeviceSet(ctx);
        boolean changed = devices.remove(id);
        if (changed) {
            save(ctx, devices);
            ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .edit().remove(KEY_ALIAS_PREFIX + id).apply();
        }
        return changed;
    }

    static boolean contains(Context ctx, String raw) {
        String id = normalize(raw);
        return !id.isEmpty() && getDeviceSet(ctx).contains(id);
    }

    static String getAlias(Context ctx, String raw) {
        String id = normalize(raw);
        if (id.isEmpty()) return "";
        String alias = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .getString(KEY_ALIAS_PREFIX + id, "");
        return alias == null || alias.trim().isEmpty() ? id : alias.trim();
    }

    static boolean setAlias(Context ctx, String raw, String rawAlias) {
        String id = normalize(raw);
        if (id.isEmpty() || !getDeviceSet(ctx).contains(id)) return false;
        String alias = rawAlias == null ? "" : rawAlias.trim();
        if (alias.length() > 24) alias = alias.substring(0, 24);
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putString(KEY_ALIAS_PREFIX + id, alias).apply();
        return true;
    }

    private static void save(Context ctx, Set<String> devices) {
        ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit().putStringSet(KEY_DEVICES, new TreeSet<>(devices)).commit();
    }
}
