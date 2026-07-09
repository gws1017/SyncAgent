package com.syncagent

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat

// 포그라운드 서비스 — 앱을 최소화해도 시스템이 프로세스를 죽이지 않게 붙잡아둔다.
// 안드로이드 정책상 상시 알림 하나는 떠 있어야 하는데, "동기화 중" 정도의 문구라
// 위장(게임인 티 안 남)은 그대로 유지된다.
class SyncService : Service() {
    companion object {
        const val CHANNEL_ID = "sync_service"
        const val NOTIFICATION_ID = 1
        const val WIDGET_UPDATE_MS = 60_000L
    }

    private val handler = Handler(Looper.getMainLooper())
    private val widgetUpdater = object : Runnable {
        override fun run() {
            SyncWidgetProvider.updateAll(this@SyncService)
            handler.postDelayed(this, WIDGET_UPDATE_MS)
        }
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()

        // 알림을 탭하면 곧바로 앱을 열지 않고 NotificationTapReceiver로 보낸다.
        // 거기서 "2초 안에 3번 연속 탭"인지 판별해서 그때만 대시보드를 연다 —
        // 누가 한 번 눌러봐도 아무 반응 없음(위장 유지).
        val tapIntent = Intent(this, NotificationTapReceiver::class.java)
        val tapPending = PendingIntent.getBroadcast(
            this, 0, tapIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("sync agent")
            .setContentText("백그라운드 동기화 중")
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .setContentIntent(tapPending)
            .build()
        startForeground(NOTIFICATION_ID, notification)

        // 프로세스가 살아있는 동안 1분마다 홈화면 위젯 갱신
        handler.post(widgetUpdater)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        handler.removeCallbacks(widgetUpdater)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, "Background sync", NotificationManager.IMPORTANCE_LOW
            )
            channel.description = "sync agent를 백그라운드에서 계속 실행시켜 둠"
            channel.setShowBadge(false)
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }
}
