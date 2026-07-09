package com.syncagent

import android.content.Context
import org.json.JSONObject
import java.io.BufferedReader
import java.io.File
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URL

// PC의 cloud_sync_win.cpp와 정확히 같은 Firebase REST 프로토콜(익명 인증 + Realtime
// Database)을 쓴다 — 코드/토큰 저장 위치만 다를 뿐 나머지는 동일한 계약.
// 네이티브(C++)에서 JNI로 이 객체의 메서드를 직접 호출하지 않고 MainActivity의
// 얇은 래퍼를 통해 부른다 (JNI가 개별 오브젝트를 못 찾으므로 activity 인스턴스 경유).
object CloudSync {
    private const val API_KEY = "AIzaSyAqvLIdwydpMnW2faZ3vF9A9slUFVLPaVY"
    private const val DB_HOST = "syncagent-c5fbc-default-rtdb.firebaseio.com"

    private lateinit var codeFile: File
    private lateinit var authFile: File

    fun init(context: Context) {
        codeFile = File(context.filesDir, "synccode.txt")
        authFile = File(context.filesDir, "cloudauth.txt")
    }

    fun getSavedCode(): String = if (codeFile.exists()) codeFile.readText().trim() else ""

    fun setCode(code: String) { codeFile.writeText(code) }

    fun generateCode(): String {
        val code = (100000..999999).random().toString()
        setCode(code)
        return code
    }

    private data class Auth(var idToken: String = "", var refreshToken: String = "", var expiresAt: Long = 0)

    private fun loadAuth(): Auth {
        val a = Auth()
        if (!authFile.exists()) return a
        authFile.readLines().forEach { line ->
            val i = line.indexOf('=')
            if (i <= 0) return@forEach
            when (line.substring(0, i)) {
                "idToken" -> a.idToken = line.substring(i + 1)
                "refreshToken" -> a.refreshToken = line.substring(i + 1)
                "expiresAt" -> a.expiresAt = line.substring(i + 1).toLongOrNull() ?: 0
            }
        }
        return a
    }

    private fun saveAuth(a: Auth) {
        authFile.writeText("idToken=${a.idToken}\nrefreshToken=${a.refreshToken}\nexpiresAt=${a.expiresAt}\n")
    }

    // 이 함수는 항상 네이티브 글루 스레드(안드로이드 메인/UI 스레드가 아님)에서 호출된다.
    // 블로킹 네트워크 호출이 메인 스레드에서만 금지되는 NetworkOnMainThreadException
    // 대상이 아니므로 별도 코루틴/스레드 없이 동기 호출로 충분하다.
    private fun httpRequest(urlStr: String, method: String, body: String?, contentType: String): Pair<Int, String> {
        val conn = URL(urlStr).openConnection() as HttpURLConnection
        conn.requestMethod = method
        conn.setRequestProperty("Content-Type", contentType)
        conn.connectTimeout = 10_000
        conn.readTimeout = 10_000
        if (body != null) {
            conn.doOutput = true
            OutputStreamWriter(conn.outputStream).use { it.write(body) }
        }
        val code = conn.responseCode
        val stream = if (code in 200..299) conn.inputStream else conn.errorStream
        val text = stream?.bufferedReader()?.use(BufferedReader::readText) ?: ""
        return Pair(code, text)
    }

    private fun signInAnonymously(): Auth? {
        val (status, body) = httpRequest(
            "https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=$API_KEY",
            "POST", "{\"returnSecureToken\":true}", "application/json"
        )
        if (status !in 200..299) return null
        val json = JSONObject(body)
        val a = Auth(idToken = json.optString("idToken"), refreshToken = json.optString("refreshToken"))
        val expiresIn = json.optString("expiresIn", "3600").toLongOrNull() ?: 3600
        a.expiresAt = System.currentTimeMillis() / 1000 + expiresIn - 60
        return if (a.idToken.isNotEmpty()) a else null
    }

    private fun refreshIdToken(a: Auth): Auth? {
        val (status, body) = httpRequest(
            "https://securetoken.googleapis.com/v1/token?key=$API_KEY",
            "POST", "grant_type=refresh_token&refresh_token=${a.refreshToken}",
            "application/x-www-form-urlencoded"
        )
        if (status !in 200..299) return null
        val json = JSONObject(body)
        val newA = Auth(idToken = json.optString("id_token"), refreshToken = json.optString("refresh_token"))
        val expiresIn = json.optString("expires_in", "3600").toLongOrNull() ?: 3600
        newA.expiresAt = System.currentTimeMillis() / 1000 + expiresIn - 60
        return if (newA.idToken.isNotEmpty()) newA else null
    }

    private fun ensureAuth(): Auth? {
        val a = loadAuth()
        val now = System.currentTimeMillis() / 1000
        if (a.idToken.isNotEmpty() && now < a.expiresAt) return a
        if (a.refreshToken.isNotEmpty()) {
            refreshIdToken(a)?.let { saveAuth(it); return it }
        }
        return signInAnonymously()?.also { saveAuth(it) }
    }

    // 반환: "OK\n<메시지>" 또는 "ERR\n<메시지>" — 네이티브 쪽이 JNI 호출 한 번으로
    // 성공여부+메시지를 같이 받게 하기 위해 한 문자열로 합침.
    fun upload(code: String, saveText: String): String {
        val auth = ensureAuth() ?: return "ERR\n로그인 실패 (네트워크 확인)"
        val body = JSONObject().apply {
            put("state", saveText)
            put("updatedAt", System.currentTimeMillis() / 1000)
            put("updatedBy", "android")
        }.toString()
        val (status, _) = httpRequest(
            "https://$DB_HOST/syncs/$code.json?auth=${auth.idToken}",
            "PUT", body, "application/json"
        )
        return if (status in 200..299) "OK\n업로드 완료" else "ERR\n업로드 실패 ($status)"
    }

    // 반환: "OK\n<세이브 텍스트>" 또는 "ERR\n<메시지>"
    fun download(code: String): String {
        val auth = ensureAuth() ?: return "ERR\n로그인 실패 (네트워크 확인)"
        val (status, body) = httpRequest(
            "https://$DB_HOST/syncs/$code.json?auth=${auth.idToken}",
            "GET", null, "application/json"
        )
        if (status !in 200..299) return "ERR\n다운로드 실패 ($status)"
        if (body == "null" || body.isBlank()) return "ERR\n클라우드에 저장된 데이터가 없습니다"
        return try {
            "OK\n" + JSONObject(body).optString("state")
        } catch (e: Exception) {
            "ERR\n응답 파싱 실패"
        }
    }
}
