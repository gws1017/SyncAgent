package com.syncagent

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

// 상시 알림("sync agent 백그라운드 동기화 중")을 탭했을 때 처리한다.
// 한 번 눌렀을 땐 무반응(위장 유지) — 세 번 연속으로 눌러야만 대시보드가 열린다.
// 남이 실수로/호기심에 한 번 눌러봐도 아무 일도 안 일어남.
//
// 알림을 탭하면 안드로이드가 알림창(셰이드)을 자동으로 닫아버리는 건 OS 기본 동작이라
// 앱에서 막을 수 없다. 그래서 매번 알림창을 다시 내려서 눌러야 하므로 텀을 넉넉하게
// 잡아야 한다 (너무 짧으면 세 번째 탭 전에 시간 초과로 카운트가 리셋되어버림).
class NotificationTapReceiver : BroadcastReceiver() {
    companion object {
        private var tapCount = 0
        private var lastTapTime = 0L
        private const val TAP_WINDOW_MS = 8000L
        private const val REQUIRED_TAPS = 3
    }

    override fun onReceive(context: Context, intent: Intent) {
        val now = System.currentTimeMillis()
        if (now - lastTapTime > TAP_WINDOW_MS) tapCount = 0
        lastTapTime = now
        tapCount++

        if (tapCount >= REQUIRED_TAPS) {
            tapCount = 0
            val launch = Intent(context, MainActivity::class.java)
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            context.startActivity(launch)
        }
        // 1~2번째 탭: 아무 것도 하지 않음
    }
}
