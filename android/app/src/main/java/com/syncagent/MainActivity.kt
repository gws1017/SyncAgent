package com.syncagent

import android.Manifest
import android.app.NativeActivity
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.view.inputmethod.InputMethodManager
import androidx.core.app.ActivityCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import java.util.concurrent.LinkedBlockingQueue

class MainActivity : NativeActivity() {
    companion object {
        const val EVENT_CHANNEL_ID = "sync_events"
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ActivityCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
            != PackageManager.PERMISSION_GRANTED
        ) {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1001)
        }

        createEventChannel()
        startSyncService()
    }

    fun showSoftInput() {
        val imm = getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        imm.showSoftInput(window.decorView, 0)
    }

    private val unicodeQueue = LinkedBlockingQueue<Int>()

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (event.action == KeyEvent.ACTION_DOWN)
            unicodeQueue.offer(event.getUnicodeChar(event.metaState))
        return super.dispatchKeyEvent(event)
    }

    fun pollUnicodeChar(): Int = unicodeQueue.poll() ?: 0

    // 네이티브(C++)에서 게임 이벤트가 생겼을 때 호출 — PC 버전의 트레이 토스트에 대응.
    // 알림 문구는 게임 이벤트 텍스트 그대로 쓰지만, 발신자 정보("sync agent")는 위장 유지.
    fun postEventNotification(text: String) {
        val notification = NotificationCompat.Builder(this, EVENT_CHANNEL_ID)
            .setContentTitle("sync agent")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setAutoCancel(true)
            .setTimeoutAfter(15_000)
            .build()
        try {
            NotificationManagerCompat.from(this)
                .notify((System.currentTimeMillis() % 100000).toInt(), notification)
        } catch (e: SecurityException) {
            // 알림 권한이 아직 없으면 조용히 무시 (게임 진행 자체는 계속됨)
        }
    }

    private fun createEventChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                EVENT_CHANNEL_ID, "Sync events", NotificationManager.IMPORTANCE_DEFAULT
            )
            channel.description = "sync agent 이벤트 알림"
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }

    private fun startSyncService() {
        val intent = Intent(this, SyncService::class.java)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(intent)
        } else {
            startService(intent)
        }
    }
}
