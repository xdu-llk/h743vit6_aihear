package com.aihear.app;

import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteOpenHelper;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * SQLite 持久化告警记录
 */
public class AlertDbHelper extends SQLiteOpenHelper {

    private static final String DB_NAME    = "aihear.db";
    private static final int    DB_VERSION = 1;

    // ── 告警表 ──
    private static final String TABLE_ALERT = "alerts";
    private static final String COL_ID      = "_id";
    private static final String COL_TS      = "timestamp";
    private static final String COL_CLASS   = "class_name";
    private static final String COL_SCORE   = "score";
    private static final String COL_TOPIC   = "topic";

    // ── 喂养表 ──
    private static final String TABLE_FEED = "feeding";
    private static final String COL_FEED_ID   = "_id";
    private static final String COL_FEED_TS   = "timestamp";
    private static final String COL_FEED_NOTE = "note";

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
            COL_TOPIC + " TEXT)");

        db.execSQL("CREATE TABLE " + TABLE_FEED + " (" +
            COL_FEED_ID   + " INTEGER PRIMARY KEY AUTOINCREMENT, " +
            COL_FEED_TS   + " INTEGER NOT NULL, " +
            COL_FEED_NOTE + " TEXT)");

        db.execSQL("CREATE INDEX idx_alert_ts ON " + TABLE_ALERT + "(" + COL_TS + " DESC)");
        db.execSQL("CREATE INDEX idx_feed_ts ON " + TABLE_FEED + "(" + COL_FEED_TS + " DESC)");
    }

    @Override
    public void onUpgrade(SQLiteDatabase db, int oldV, int newV) {
        db.execSQL("DROP TABLE IF EXISTS " + TABLE_ALERT);
        db.execSQL("DROP TABLE IF EXISTS " + TABLE_FEED);
        onCreate(db);
    }

    // ── 告警 CRUD ──

    public long insertAlert(long ts, String cls, double score, String topic) {
        ContentValues cv = new ContentValues();
        cv.put(COL_TS, ts);
        cv.put(COL_CLASS, cls);
        cv.put(COL_SCORE, score);
        cv.put(COL_TOPIC, topic);
        return getWritableDatabase().insert(TABLE_ALERT, null, cv);
    }

    public JSONArray getAlerts(int limit) {
        JSONArray arr = new JSONArray();
        SQLiteDatabase db = getReadableDatabase();
        Cursor c = db.query(TABLE_ALERT, null, null, null, null, null, COL_TS + " DESC", String.valueOf(limit));
        while (c.moveToNext()) {
            JSONObject o = new JSONObject();
            try {
                o.put("id",    c.getLong(c.getColumnIndexOrThrow(COL_ID)));
                o.put("time",  c.getLong(c.getColumnIndexOrThrow(COL_TS)));
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

    // ── 喂养 CRUD ──

    public long insertFeeding(long ts, String note) {
        ContentValues cv = new ContentValues();
        cv.put(COL_FEED_TS, ts);
        cv.put(COL_FEED_NOTE, note);
        return getWritableDatabase().insert(TABLE_FEED, null, cv);
    }

    public JSONArray getFeedings(int limit) {
        JSONArray arr = new JSONArray();
        SQLiteDatabase db = getReadableDatabase();
        Cursor c = db.query(TABLE_FEED, null, null, null, null, null, COL_FEED_TS + " DESC", String.valueOf(limit));
        while (c.moveToNext()) {
            JSONObject o = new JSONObject();
            try {
                o.put("id",   c.getLong(c.getColumnIndexOrThrow(COL_FEED_ID)));
                o.put("time", c.getLong(c.getColumnIndexOrThrow(COL_FEED_TS)));
                o.put("note", c.getString(c.getColumnIndexOrThrow(COL_FEED_NOTE)));
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
        long dayStart = todayStart();
        SQLiteDatabase db = getReadableDatabase();
        Cursor c = db.rawQuery("SELECT COUNT(*) FROM " + TABLE_ALERT + " WHERE " + COL_TS + " >= ?",
            new String[]{String.valueOf(dayStart)});
        int n = 0;
        if (c.moveToFirst()) n = c.getInt(0);
        c.close();
        return n;
    }

    public int countFeedingToday() {
        long dayStart = todayStart();
        SQLiteDatabase db = getReadableDatabase();
        Cursor c = db.rawQuery("SELECT COUNT(*) FROM " + TABLE_FEED + " WHERE " + COL_FEED_TS + " >= ?",
            new String[]{String.valueOf(dayStart)});
        int n = 0;
        if (c.moveToFirst()) n = c.getInt(0);
        c.close();
        return n;
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
