package com.syncagent

import android.Manifest
import android.app.ActivityManager
import android.app.NativeActivity
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.BitmapFactory
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
        CloudSync.init(this)
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
    // 제목/아이콘은 프라이버시 모드를 그대로 반영 (기본은 정직하게 "Text RPG").
    fun postEventNotification(text: String, privacyMode: Boolean) {
        val title = if (privacyMode) "sync agent" else "Text RPG"
        val icon  = if (privacyMode) android.R.drawable.stat_notify_sync else R.drawable.ic_game_notif
        val notification = NotificationCompat.Builder(this, EVENT_CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(text)
            .setSmallIcon(icon)
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

    // 네이티브에서 프라이버시 모드가 바뀔 때마다 호출 — 상시 알림 + 최근 앱 목록
    // (TaskDescription)의 이름/아이콘을 동기화한다. 런처 아이콘 자체는 안 바뀜
    // (OS 레벨 고정값이라 못 바꾸고, 어차피 최초 실행 후엔 거의 안 누르는 곳이라 괜찮음).
    fun updatePrivacyPresentation(privacyMode: Boolean) {
        // 네이티브(C++)가 JNI로 이 함수를 호출하는 스레드는 안드로이드 UI 스레드가 아니라
        // 게임 렌더링용 네이티브 글루 스레드다. setTaskDescription()은 UI 스레드에서 호출해야
        // 정상 반영되므로 runOnUiThread로 넘겨준다 (이게 최근 앱 카드 아이콘이 안 바뀌던 원인).
        runOnUiThread {
            SyncService.updatePresentation(applicationContext, privacyMode)

            val label = if (privacyMode) "sync agent" else "Text RPG"
            val iconRes = if (privacyMode) R.drawable.ic_sync else R.drawable.ic_game
            val bitmap = BitmapFactory.decodeResource(resources, iconRes)

            // Bitmap 생성자는 API 28+에서 deprecated지만 모든 버전(24+)에서 동일하게 동작함 —
            // Builder.setIcon(Icon) 오버로드가 이 compileSdk 스텁에서 인식이 안 돼서 이쪽으로 통일.
            @Suppress("DEPRECATION")
            setTaskDescription(ActivityManager.TaskDescription(label, bitmap))
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

    // ---- 네이티브(cloud_sync_android.cpp)가 JNI로 호출하는 클라우드 동기화 래퍼 ----
    fun cloudGetSavedCode(): String = CloudSync.getSavedCode()
    fun cloudGenerateCode(): String = CloudSync.generateCode()
    fun cloudSetCode(code: String) { CloudSync.setCode(code) }
    fun cloudUpload(code: String, saveText: String): String = CloudSync.upload(code, saveText)
    fun cloudDownload(code: String): String = CloudSync.download(code)
}
