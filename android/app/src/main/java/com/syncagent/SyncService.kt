package com.syncagent

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.graphics.drawable.IconCompat

// 포그라운드 서비스 — 앱을 최소화해도 시스템이 프로세스를 죽이지 않게 붙잡아둔다.
// 안드로이드 정책상 상시 알림 하나는 떠 있어야 하는데, 기본 문구/아이콘은 정직하게
// "Text RPG"로 표시하고, 프라이버시 모드를 켰을 때만 "sync agent"로 바뀐다
// (MainActivity.updatePrivacyPresentation()이 네이티브 쪽 토글을 감지해서 호출).
class SyncService : Service() {
    companion object {
        const val CHANNEL_ID = "sync_service"
        const val NOTIFICATION_ID = 1
        const val WIDGET_UPDATE_MS = 60_000L

        // 실행 중인 서비스 인스턴스 참조. startForeground()는 인스턴스 메서드라
        // MainActivity에서 직접 부르려면 이게 필요하다.
        @Volatile private var instance: SyncService? = null

        // 포그라운드 서비스에 딸린 알림은 그냥 notify()나 startForeground() 재호출만으로는
        // "갱신"으로 처리돼서 일부 기기에서 아이콘이 안 바뀐다 (텍스트는 바뀌는데 아이콘 View는
        // 그대로 재사용됨). stopForeground(REMOVE)로 알림을 완전히 떼어냈다가 startForeground로
        // 다시 붙이면 시스템이 "새 알림"으로 취급해서 아이콘까지 확실히 다시 그린다.
        fun updatePresentation(context: Context, privacyMode: Boolean) {
            val notification = buildNotification(context, privacyMode)
            val svc = instance
            try {
                if (svc != null) {
                    svc.stopForeground(Service.STOP_FOREGROUND_REMOVE)
                    svc.startForeground(NOTIFICATION_ID, notification)
                } else {
                    NotificationManagerCompat.from(context).notify(NOTIFICATION_ID, notification)
                }
            } catch (e: SecurityException) {
                // 알림 권한이 아직 없으면 조용히 무시 — 여기서 예외를 안 잡으면
                // JNI 호출 쪽(네이티브)에 예외가 걸린 채로 남아서 이후 호출이 다 씹힌다.
            }
        }

        private fun buildNotification(context: Context, privacyMode: Boolean): android.app.Notification {
            val title = if (privacyMode) "sync agent" else "Text RPG"
            val text  = if (privacyMode) "백그라운드 동기화 중" else "백그라운드에서 진행 중"
            val iconRes = if (privacyMode) android.R.drawable.stat_notify_sync else R.drawable.ic_game_notif
            // 리소스 ID를 그대로 setSmallIcon(Int)에 넘기면 일부 OEM이 아이콘을
            // ID 기준으로 캐싱해서 안 바뀔 수 있음 — 매번 새 Bitmap을 디코드해서
            // IconCompat으로 넘기면 캐시 우회가 잘 됨.
            val iconBitmap = BitmapFactory.decodeResource(context.resources, iconRes)
            val icon = IconCompat.createWithBitmap(iconBitmap)

            val tapIntent = Intent(context, NotificationTapReceiver::class.java)
            val tapPending = PendingIntent.getBroadcast(
                context, 0, tapIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )

            return NotificationCompat.Builder(context, CHANNEL_ID)
                .setContentTitle(title)
                .setContentText(text)
                .setSmallIcon(icon)
                .setPriority(NotificationCompat.PRIORITY_LOW)
                .setOngoing(true)
                .setContentIntent(tapPending)
                .build()
        }
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
        instance = this
        createChannel()
        startForeground(NOTIFICATION_ID, buildNotification(this, false))

        // 프로세스가 살아있는 동안 1분마다 홈화면 위젯 갱신
        handler.post(widgetUpdater)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return START_STICKY
    }

    override fun onDestroy() {
        instance = null
        handler.removeCallbacks(widgetUpdater)
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, "Background sync", NotificationManager.IMPORTANCE_LOW
            )
            channel.description = "Text RPG를 백그라운드에서 계속 실행시켜 둠"
            channel.setShowBadge(false)
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }
}
