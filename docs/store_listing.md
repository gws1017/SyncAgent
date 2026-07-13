# Play Store 등록 정보 초안

## 앱 제목 (최대 30자)
Text RPG - 방치형 텍스트 RPG

## 짧은 설명 (최대 80자)
버튼 하나로 자라는 텍스트 방치형 RPG. 백그라운드에서도 계속 성장합니다.

## 긴 설명 (최대 4000자)

**Text RPG**는 화려한 그래픽 없이, 숫자와 텍스트만으로 즐기는 순수한 방치형(아이들) RPG입니다.

전사·마법사·도적 세 직업 중 하나를 골라 던전을 탐험하고, 업그레이드와 특성을 투자해서
캐릭터를 키워보세요. 앱을 닫아도, 화면을 보고 있지 않아도 캐릭터는 계속 성장합니다.

**주요 기능**
- 3개 직업(전사/마법사/도적), 직업마다 다른 전투 스타일과 특성 트리
- 프레스티지 시스템 — 캐릭터를 초기화하고 "계승 보너스"를 얻어 처음부터 더 강하게 시작
- 백그라운드 자동 성장 — 앱을 꺼두거나 최소화해도 진행 상황이 계속 쌓임
- PC 버전과 연동되는 클라우드 세이브 동기화 (선택 사항)
- 홈 화면 위젯으로 진행 상황 한눈에 확인
- 한국어/영어 지원

**프라이버시 모드**
설정에서 "프라이버시 모드"를 켜면 앱의 알림/제목 문구가 일반적인 배경 작업처럼
표시됩니다. 조용히 즐기고 싶을 때 사용해 보세요. (앱의 실제 기능이나 데이터 처리
방식은 바뀌지 않으며, 표시되는 문구만 달라집니다.)

가볍게 켜두고 짬날 때마다 들여다보는 게임을 찾으신다면, Text RPG를 플레이해 보세요.

---

## 카테고리
게임 > 롤플레잉

## 콘텐츠 등급
전체이용가 (폭력적 텍스트 묘사 최소 수준 — 등급 설문에서 확인 필요)

## 연락처 이메일
(본인 이메일 입력)

## 개인정보처리방침 URL
https://gws1017.github.io/SyncAgent/privacy.html

## 데이터 삭제 요청 URL
https://gws1017.github.io/SyncAgent/data-deletion.html

---

## GitHub Pages 활성화 방법

1. `docs/privacy.html` 을 커밋·푸시한다.
2. GitHub 저장소 → **Settings** → **Pages**
3. Source: **Deploy from a branch**
4. Branch: `master` / Folder: `/docs` → Save
5. 1~2분 후 아래 URL이 열리면 완료:
   - https://gws1017.github.io/SyncAgent/privacy.html

## 스토어 이미지 위치

`dist/` 는 gitignore 대상이므로 로컬에서만 보관합니다.
재생성: `powershell -File tools/make_store_assets.ps1`

| 파일 | 경로 | 용도 |
|------|------|------|
| 아이콘 512×512 | `dist/store/icon-512.png` | Play Console 고해상도 아이콘 |
| 피처 그래픽 1024×500 | `dist/store/feature-graphic-1024x500.png` | Play Console 추천 그래픽 |
