package com.syncagent

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat

// 포그라운드 서비스 — 앱을 최소화해도 시스템이 프로세스를 죽이지 않게 붙잡아둔다.
// 안드로이드 정책상 상시 알림 하나는 떠 있어야 하는데, "동기화 중" 정도의 문구라
// 위장(게임인 티 안 남)은 그대로 유지된다.
class SyncService : Service() {
    companion object {
        const val CHANNEL_ID = "sync_service"
        const val NOTIFICATION_ID = 1
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()
        val notification = NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("sync agent")
            .setContentText("백그라운드 동기화 중")
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setOngoing(true)
            .build()
        startForeground(NOTIFICATION_ID, notification)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
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
