package com.syncagent

import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.widget.RemoteViews
import java.io.File

// 홈화면/잠금화면 위젯 — 위장 텍스트로 게임 진행 상황을 보여준다.
//   build #34      = 레벨 34
//   batch 25       = 스테이지 25
//   진행바          = XP 게이지
//   1.2M files cached = 골드
// 데이터는 네이티브(C++)가 틱마다 기록하는 widget.txt(key=value)를 읽는다.
// 게임 로직/세이브 포맷을 코틀린에서 다시 구현하지 않기 위한 구조.
class SyncWidgetProvider : AppWidgetProvider() {

    override fun onUpdate(context: Context, mgr: AppWidgetManager, ids: IntArray) {
        for (id in ids) updateWidget(context, mgr, id)
    }

    companion object {
        // 서비스가 1분마다 호출해서 모든 위젯 인스턴스를 갱신한다.
        fun updateAll(context: Context) {
            val mgr = AppWidgetManager.getInstance(context)
            val ids = mgr.getAppWidgetIds(ComponentName(context, SyncWidgetProvider::class.java))
            for (id in ids) updateWidget(context, mgr, id)
        }

        private fun updateWidget(context: Context, mgr: AppWidgetManager, id: Int) {
            val info = readWidgetInfo(context)
            val views = RemoteViews(context.packageName, R.layout.widget_sync)

            if (info == null) {
                views.setTextViewText(R.id.widget_status, "idle")
                views.setTextViewText(R.id.widget_line1, "no active session")
                views.setProgressBar(R.id.widget_progress, 100, 0, false)
                views.setTextViewText(R.id.widget_line2, "tap to configure")
            } else {
                views.setTextViewText(R.id.widget_status, "active")
                views.setTextViewText(R.id.widget_line1, "build #${info.level} · batch ${info.stage}")
                views.setProgressBar(R.id.widget_progress, 100, (info.xpPct * 100).toInt(), false)
                views.setTextViewText(R.id.widget_line2, "${formatCount(info.gold)} files cached")
            }

            // 위젯 탭 → 앱 열기
            val pi = PendingIntent.getActivity(
                context, 0,
                Intent(context, MainActivity::class.java),
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            views.setOnClickPendingIntent(R.id.widget_root, pi)

            mgr.updateAppWidget(id, views)
        }

        private data class WidgetInfo(val level: Int, val stage: Int, val xpPct: Float, val gold: Long)

        private fun readWidgetInfo(context: Context): WidgetInfo? {
            val f = File(context.filesDir, "widget.txt")
            if (!f.exists()) return null
            var level = -1; var stage = 0; var xpPct = 0f; var gold = 0L
            try {
                f.forEachLine { line ->
                    val i = line.indexOf('=')
                    if (i <= 0) return@forEachLine
                    val k = line.substring(0, i)
                    val v = line.substring(i + 1).trim()
                    when (k) {
                        "level" -> level = v.toIntOrNull() ?: -1
                        "stage" -> stage = v.toIntOrNull() ?: 0
                        "xpPct" -> xpPct = v.toFloatOrNull() ?: 0f
                        "gold"  -> gold = v.toLongOrNull() ?: 0L
                    }
                }
            } catch (e: Exception) {
                return null
            }
            return if (level >= 0) WidgetInfo(level, stage, xpPct, gold) else null
        }

        // 1234567 → "1.2M" 같은 축약 표기 (파일 수처럼 보이게)
        private fun formatCount(n: Long): String = when {
            n >= 1_000_000_000 -> "%.1fB".format(n / 1_000_000_000.0)
            n >= 1_000_000     -> "%.1fM".format(n / 1_000_000.0)
            n >= 1_000         -> "%.1fK".format(n / 1_000.0)
            else               -> n.toString()
        }
    }
}
