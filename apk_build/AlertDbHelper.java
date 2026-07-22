package com.aihear.app;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * SQLite 持久化告警记录 + 喂养记录（v3: 喂养表增加 device_id）
 */
public class AlertDbHelper extends SQLiteOpenHelper {

    private static final String DB_NAME    = "aihear.db";
    private static final int    DB_VERSION = 4;

    // ── 告警表 ──
    private static final String TABLE_ALERT = "alerts";
    private static final String COL_ID      = "_id";
    private static final String COL_TS      = "timestamp";
    private static final String COL_CLASS   = "class_name";
    private static final String COL_SCORE   = "score";
    private static final String COL_TOPIC   = "topic";
    private static final String COL_DEVICE  = "device_id";
    private static final String COL_EVENT   = "event_id";

    // ── 喂养表 ──
    private static final String TABLE_FEED = "feeding";
    private static final String COL_FEED_ID     = "_id";
    private static final String COL_FEED_TS     = "timestamp";
    private static final String COL_FEED_NOTE   = "note";
    private static final String COL_FEED_DEVICE = "device_id";

    // ── 环境数据聚合表 ──
    private static final String TABLE_ENV_HOURLY  = "env_hourly";
    private static final String COL_ENV_DEVICE    = "device_id";
    private static final String COL_ENV_HOUR_TS   = "hour_ts";
    private static final String COL_ENV_TEMP_AVG  = "temp_avg";
    private static final String COL_ENV_TEMP_MAX  = "temp_max";
    private static final String COL_ENV_TEMP_MIN  = "temp_min";
    private static final String COL_ENV_HUMI_AVG  = "humi_avg";
    private static final String COL_ENV_HUMI_MAX  = "humi_max";
    private static final String COL_ENV_HUMI_MIN  = "humi_min";
    private static final String COL_ENV_SAMPLES   = "sample_cnt";

    public AlertDbHelper(Context ctx) {
        super(ctx, DB_NAME, null, DB_VERSION);
    }

    @Override
    public void onCreate(SQLiteDatabase db) {
        db.execSQL("CREATE TABLE " + TABLE_ALERT + " (" +
            COL_ID    + " INTEGER PRIMARY KEY AUTOINCREMENT, " +
            COL_TS    + " INTEGER NOT NULL, " +
            COL_CLASS + " TEXT NOT NULL, " +
            COL_SCORE + " REAL, " +
            COL_TOPIC + " TEXT, " +
            COL_DEVICE + " TEXT NOT NULL DEFAULT 'legacy', " +
            COL_EVENT + " TEXT)");

        db.execSQL("CREATE TABLE " + TABLE_FEED + " (" +
            COL_FEED_ID     + " INTEGER PRIMARY KEY AUTOINCREMENT, " +
            COL_FEED_TS     + " INTEGER NOT NULL, " +
            COL_FEED_NOTE   + " TEXT, " +
            COL_FEED_DEVICE + " TEXT NOT NULL DEFAULT 'legacy')");

        db.execSQL("CREATE INDEX idx_alert_ts ON " + TABLE_ALERT + "(" + COL_TS + " DESC)");
        db.execSQL("CREATE INDEX idx_alert_device_ts ON " + TABLE_ALERT +
            "(" + COL_DEVICE + "," + COL_TS + " DESC)");
        db.execSQL("CREATE UNIQUE INDEX idx_alert_event ON " + TABLE_ALERT +
            "(" + COL_EVENT + ") WHERE " + COL_EVENT + " IS NOT NULL");
        db.execSQL("CREATE INDEX idx_feed_ts ON " + TABLE_FEED + "(" + COL_FEED_TS + " DESC)");
        db.execSQL("CREATE INDEX idx_feed_device_ts ON " + TABLE_FEED +
            "(" + COL_FEED_DEVICE + "," + COL_FEED_TS + " DESC)");

        db.execSQL("CREATE TABLE " + TABLE_ENV_HOURLY + " (" +
            COL_ENV_DEVICE  + " TEXT NOT NULL, " +
            COL_ENV_HOUR_TS + " INTEGER NOT NULL, " +
            COL_ENV_TEMP_AVG + " REAL, " +
            COL_ENV_TEMP_MAX + " REAL, " +
            COL_ENV_TEMP_MIN + " REAL, " +
            COL_ENV_HUMI_AVG + " REAL, " +
            COL_ENV_HUMI_MAX + " REAL, " +
            COL_ENV_HUMI_MIN + " REAL, " +
            COL_ENV_SAMPLES  + " INTEGER DEFAULT 0, " +
            "PRIMARY KEY (" + COL_ENV_DEVICE + "," + COL_ENV_HOUR_TS + "))");

        db.execSQL("CREATE INDEX idx_env_hourly_ts ON " + TABLE_ENV_HOURLY +
            "(" + COL_ENV_HOUR_TS + " DESC)");
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldV, int newV) {
        if (oldV < 2) {
            db.execSQL("ALTER TABLE " + TABLE_ALERT + " ADD COLUMN " +
                COL_DEVICE + " TEXT NOT NULL DEFAULT 'legacy'");
            db.execSQL("ALTER TABLE " + TABLE_ALERT + " ADD COLUMN " +
                COL_EVENT + " TEXT");
            db.execSQL("CREATE INDEX idx_alert_device_ts ON " + TABLE_ALERT +
                "(" + COL_DEVICE + "," + COL_TS + " DESC)");
            db.execSQL("CREATE UNIQUE INDEX idx_alert_event ON " + TABLE_ALERT +
                "(" + COL_EVENT + ") WHERE " + COL_EVENT + " IS NOT NULL");
        }
        if (oldV < 3) {
            db.execSQL("ALTER TABLE " + TABLE_FEED + " ADD COLUMN " +
                COL_FEED_DEVICE + " TEXT NOT NULL DEFAULT 'legacy'");
            db.execSQL("CREATE INDEX idx_feed_device_ts ON " + TABLE_FEED +
                "(" + COL_FEED_DEVICE + "," + COL_FEED_TS + " DESC)");
        }
        if (oldV < 4) {
            db.execSQL("CREATE TABLE " + TABLE_ENV_HOURLY + " (" +
                COL_ENV_DEVICE  + " TEXT NOT NULL, " +
                COL_ENV_HOUR_TS + " INTEGER NOT NULL, " +
                COL_ENV_TEMP_AVG + " REAL, " +
                COL_ENV_TEMP_MAX + " REAL, " +
                COL_ENV_TEMP_MIN + " REAL, " +
                COL_ENV_HUMI_AVG + " REAL, " +
                COL_ENV_HUMI_MAX + " REAL, " +
                COL_ENV_HUMI_MIN + " REAL, " +
                COL_ENV_SAMPLES  + " INTEGER DEFAULT 0, " +
                "PRIMARY KEY (" + COL_ENV_DEVICE + "," + COL_ENV_HOUR_TS + "))");
            db.execSQL("CREATE INDEX idx_env_hourly_ts ON " + TABLE_ENV_HOURLY +
                "(" + COL_ENV_HOUR_TS + " DESC)");
        }
    }

    // ── 告警 CRUD ──

    public long insertAlert(long ts, String deviceId, String eventId,
                            String cls, double score, String topic) {
        ContentValues cv = new ContentValues();
        cv.put(COL_TS, ts);
        cv.put(COL_DEVICE, deviceId);
        if (eventId != null && !eventId.isEmpty()) cv.put(COL_EVENT, eventId);
        cv.put(COL_CLASS, cls);
        cv.put(COL_SCORE, score);
        cv.put(COL_TOPIC, topic);
        return getWritableDatabase().insertWithOnConflict(
            TABLE_ALERT, null, cv, SQLiteDatabase.CONFLICT_IGNORE);
    }

    public JSONArray getAlerts(int limit) {
        return getAlertsForDevice(null, limit);
    }

    public JSONArray getAlertsForDevice(String deviceId, int limit) {
        JSONArray arr = new JSONArray();
        SQLiteDatabase db = getReadableDatabase();
        String selection = deviceId == null || deviceId.isEmpty() ? null : COL_DEVICE + "=?";
        String[] args = selection == null ? null : new String[]{deviceId};
        Cursor c = db.query(TABLE_ALERT, null, selection, args, null, null,
            COL_TS + " DESC", String.valueOf(limit));
        while (c.moveToNext()) {
            JSONObject o = new JSONObject();
            try {
                o.put("id",    c.getLong(c.getColumnIndexOrThrow(COL_ID)));
                o.put("time",  c.getLong(c.getColumnIndexOrThrow(COL_TS)));
                o.put("deviceId", c.getString(c.getColumnIndexOrThrow(COL_DEVICE)));
                o.put("eventId", c.getString(c.getColumnIndexOrThrow(COL_EVENT)));
                o.put("cls",   c.getString(c.getColumnIndexOrThrow(COL_CLASS)));
                o.put("score", c.getDouble(c.getColumnIndexOrThrow(COL_SCORE)));
            } catch (Exception ignored) {}
            arr.put(o);
        }
        c.close();
        return arr;
    }

    public boolean deleteAlert(long id) {
        return getWritableDatabase().delete(TABLE_ALERT, COL_ID + "=?", new String[]{String.valueOf(id)}) > 0;
    }

    public void clearAlerts() {
        getWritableDatabase().delete(TABLE_ALERT, null, null);
    }

    public void clearAlertsForDevice(String deviceId) {
        getWritableDatabase().delete(TABLE_ALERT, COL_DEVICE + "=?", new String[]{deviceId});
    }

    public void assignLegacyAlertsToDevice(String deviceId) {
        ContentValues cv = new ContentValues();
        cv.put(COL_DEVICE, deviceId);
        getWritableDatabase().update(
            TABLE_ALERT, cv, COL_DEVICE + "='legacy'", null);
    }

    // ── 喂养 CRUD ──

    public long insertFeeding(long ts, String deviceId, String note) {
        ContentValues cv = new ContentValues();
        cv.put(COL_FEED_TS, ts);
        cv.put(COL_FEED_DEVICE, deviceId == null || deviceId.isEmpty() ? "legacy" : deviceId);
        cv.put(COL_FEED_NOTE, note);
        return getWritableDatabase().insert(TABLE_FEED, null, cv);
    }

    public JSONArray getFeedings(int limit) {
        return getFeedingsForDevice(null, limit);
    }

    public JSONArray getFeedingsForDevice(String deviceId, int limit) {
        JSONArray arr = new JSONArray();
        SQLiteDatabase db = getReadableDatabase();
        String selection = deviceId == null || deviceId.isEmpty() ? null : COL_FEED_DEVICE + "=?";
        String[] args = selection == null ? null : new String[]{deviceId};
        Cursor c = db.query(TABLE_FEED, null, selection, args, null, null,
            COL_FEED_TS + " DESC", String.valueOf(limit));
        while (c.moveToNext()) {
            JSONObject o = new JSONObject();
            try {
                o.put("id",       c.getLong(c.getColumnIndexOrThrow(COL_FEED_ID)));
                o.put("time",     c.getLong(c.getColumnIndexOrThrow(COL_FEED_TS)));
                o.put("deviceId", c.getString(c.getColumnIndexOrThrow(COL_FEED_DEVICE)));
                o.put("note",     c.getString(c.getColumnIndexOrThrow(COL_FEED_NOTE)));
            } catch (Exception ignored) {}
            arr.put(o);
        }
        c.close();
        return arr;
    }

    public boolean deleteFeeding(long id) {
        return getWritableDatabase().delete(TABLE_FEED, COL_FEED_ID + "=?", new String[]{String.valueOf(id)}) > 0;
    }

    // ── 统计 ──

    public int countAlertsToday() {
        return countAlertsTodayForDevice(null);
    }

    public int countAlertsTodayForDevice(String deviceId) {
        long dayStart = todayStart();
        SQLiteDatabase db = getReadableDatabase();
        String sql = "SELECT COUNT(*) FROM " + TABLE_ALERT + " WHERE " + COL_TS + " >= ?";
        String[] args;
        if (deviceId == null || deviceId.isEmpty()) {
            args = new String[]{String.valueOf(dayStart)};
        } else {
            sql += " AND " + COL_DEVICE + "=?";
            args = new String[]{String.valueOf(dayStart), deviceId};
        }
        Cursor c = db.rawQuery(sql, args);
        int n = 0;
        if (c.moveToFirst()) n = c.getInt(0);
        c.close();
        return n;
    }

    public int countFeedingToday() {
        return countFeedingTodayForDevice(null);
    }

    public int countFeedingTodayForDevice(String deviceId) {
        long dayStart = todayStart();
        SQLiteDatabase db = getReadableDatabase();
        String sql = "SELECT COUNT(*) FROM " + TABLE_FEED + " WHERE " + COL_FEED_TS + " >= ?";
        String[] args;
        if (deviceId == null || deviceId.isEmpty()) {
            args = new String[]{String.valueOf(dayStart)};
        } else {
            sql += " AND " + COL_FEED_DEVICE + "=?";
            args = new String[]{String.valueOf(dayStart), deviceId};
        }
        Cursor c = db.rawQuery(sql, args);
        int n = 0;
        if (c.moveToFirst()) n = c.getInt(0);
        c.close();
        return n;
    }

    // ── 环境聚合 ──

    public void upsertEnvHourly(String deviceId, long hourTs,
                                 double tempAvg, double tempMax, double tempMin,
                                 double humiAvg, double humiMax, double humiMin,
                                 int samples) {
        ContentValues cv = new ContentValues();
        cv.put(COL_ENV_DEVICE,   deviceId);
        cv.put(COL_ENV_HOUR_TS,  hourTs);
        cv.put(COL_ENV_TEMP_AVG, tempAvg);
        cv.put(COL_ENV_TEMP_MAX, tempMax);
        cv.put(COL_ENV_TEMP_MIN, tempMin);
        cv.put(COL_ENV_HUMI_AVG, humiAvg);
        cv.put(COL_ENV_HUMI_MAX, humiMax);
        cv.put(COL_ENV_HUMI_MIN, humiMin);
        cv.put(COL_ENV_SAMPLES,  samples);
        getWritableDatabase().insertWithOnConflict(
            TABLE_ENV_HOURLY, null, cv, SQLiteDatabase.CONFLICT_REPLACE);
    }

    public JSONArray getEnvHourly(String deviceId, int hours) {
        JSONArray arr = new JSONArray();
        long cutoff = System.currentTimeMillis() - (long)hours * 3600_000L;
        SQLiteDatabase db = getReadableDatabase();
        Cursor c = db.query(TABLE_ENV_HOURLY, null,
            COL_ENV_DEVICE + "=? AND " + COL_ENV_HOUR_TS + ">=?",
            new String[]{deviceId, String.valueOf(cutoff)},
            null, null, COL_ENV_HOUR_TS + " ASC");
        while (c.moveToNext()) {
            JSONObject o = new JSONObject();
            try {
                o.put("hourTs",    c.getLong(c.getColumnIndexOrThrow(COL_ENV_HOUR_TS)));
                o.put("tempAvg",   c.getDouble(c.getColumnIndexOrThrow(COL_ENV_TEMP_AVG)));
                o.put("tempMax",   c.getDouble(c.getColumnIndexOrThrow(COL_ENV_TEMP_MAX)));
                o.put("tempMin",   c.getDouble(c.getColumnIndexOrThrow(COL_ENV_TEMP_MIN)));
                o.put("humiAvg",   c.getDouble(c.getColumnIndexOrThrow(COL_ENV_HUMI_AVG)));
                o.put("humiMax",   c.getDouble(c.getColumnIndexOrThrow(COL_ENV_HUMI_MAX)));
                o.put("humiMin",   c.getDouble(c.getColumnIndexOrThrow(COL_ENV_HUMI_MIN)));
                o.put("samples",   c.getInt(c.getColumnIndexOrThrow(COL_ENV_SAMPLES)));
            } catch (Exception ignored) {}
            arr.put(o);
        }
        c.close();
        return arr;
    }

    public JSONArray getDailyCryAndFeeding(String deviceId, int days) {
        JSONArray arr = new JSONArray();
        SQLiteDatabase db = getReadableDatabase();
        long cutoff = todayStart() - (long)(days - 1) * 86400_000L;
        for (int i = 0; i < days; i++) {
            long dayStart = cutoff + (long)i * 86400_000L;
            long dayEnd   = dayStart + 86400_000L;

            Cursor cryCur = db.rawQuery(
                "SELECT COUNT(*) FROM " + TABLE_ALERT +
                " WHERE " + COL_DEVICE + "=? AND " + COL_CLASS + "='baby_cry'" +
                " AND " + COL_TS + ">=? AND " + COL_TS + "<?",
                new String[]{deviceId, String.valueOf(dayStart), String.valueOf(dayEnd)});
            int cryCount = (cryCur.moveToFirst()) ? cryCur.getInt(0) : 0;
            cryCur.close();

            Cursor feedCur = db.rawQuery(
                "SELECT COUNT(*) FROM " + TABLE_FEED +
                " WHERE " + COL_FEED_DEVICE + "=? AND " + COL_FEED_TS + ">=? AND " + COL_FEED_TS + "<?",
                new String[]{deviceId, String.valueOf(dayStart), String.valueOf(dayEnd)});
            int feedCount = (feedCur.moveToFirst()) ? feedCur.getInt(0) : 0;
            feedCur.close();

            JSONObject o = new JSONObject();
            try {
                o.put("dayTs", dayStart);
                o.put("cryCount", cryCount);
                o.put("feedCount", feedCount);
            } catch (Exception ignored) {}
            arr.put(o);
        }
        return arr;
    }

    private long todayStart() {
        java.util.Calendar cal = java.util.Calendar.getInstance();
        cal.set(java.util.Calendar.HOUR_OF_DAY, 0);
        cal.set(java.util.Calendar.MINUTE, 0);
        cal.set(java.util.Calendar.SECOND, 0);
        cal.set(java.util.Calendar.MILLISECOND, 0);
        return cal.getTimeInMillis();
    }
}
