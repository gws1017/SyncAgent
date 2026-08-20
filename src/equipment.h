#pragma once
#include <vector>
#include <string>

enum class Grade { Common = 0, Rare, Epic, Legendary };
enum class StatType { Attack = 0, Xp, Gold, Drop, Defense, Lifesteal, AtkSpeed, STAT_COUNT };

struct Item {
    Grade    grade;
    StatType stat;
    float    bonus;     // ex) 0.05 = +5%
    bool     rerolled = false; // 리롤은 아이템당 평생 한 번만 (영웅 등급 이상만 가능)
};

struct Inventory {
    static constexpr int MAX_ITEMS          = 20;
    static constexpr int MAX_EQUIP          = 5;
    static constexpr int BAG_EXPAND_SLOTS   = 5; // 확장 1회당 추가되는 칸 수
    static constexpr int MAX_BAG_EXPANSIONS = 3; // 확장 가능한 최대 횟수 (모바일 광고 전용)
    static constexpr int GOOGLE_LINK_BONUS_SLOTS = 5; // 계정 연동 최초 1회 보상 칸 수

    std::vector<Item> items;   // 보관함
    std::vector<Item> equipped; // 장착 중 (최대 MAX_EQUIP)
    int bagExpandCount = 0;     // 지금까지 확장한 횟수(광고)
    int bonusSlots     = 0;     // 계정 연동 등 1회성 보상 칸 수(광고 확장 한도와 무관)
};

// 합성: grade 등급 아이템 3개 소모 → 결과 아이템 반환
// 실패 시 한 단계 낮은 등급 반환 (일반은 항상 성공)
Item     MakeItem(Grade grade);    // 랜덤 스탯으로 아이템 생성
Item     CraftItem(Grade grade);   // 재료 소모는 호출자가 처리
bool     TryEquip(Inventory& inv, int itemIdx);
void     Unequip(Inventory& inv, int equipIdx);

// 보관함 아이템 삭제(버리기) — 쓸모없는 등급으로 칸이 막히는 문제 대응.
// 장착 중인 아이템은 먼저 해제해야 지울 수 있음(실수로 착용템 삭제 방지).
void     DeleteItem(Inventory& inv, int itemIdx);

// 지금 이 보관함이 실제로 담을 수 있는 최대 칸 수(MAX_ITEMS + 확장분).
int      MaxItems(const Inventory& inv);
// 보관함 칸을 BAG_EXPAND_SLOTS만큼 영구 확장(모바일 광고 보상 전용).
// 이미 MAX_BAG_EXPANSIONS만큼 확장했으면 false.
bool     ExpandBag(Inventory& inv);
// 자동합성(광고 버프) — 등급 낮은 순(일반→희귀→영웅)으로 재료 3개 이상이면
// 등급당 최대 1회씩 자동 합성. 매 틱마다 호출되는 걸 전제로 함(GrantAutoCraftBuff 참고).
bool     AutoCraftTick(Inventory& inv);

// 아이템의 스탯 종류를 랜덤으로 다시 굴림 (등급/보너스% 고정, 골드 소모).
long long RerollCost(Grade g);
void      RerollItem(Item& item);

// 장착된 아이템에서 스탯 합산
float    GetEquippedBonus(const Inventory& inv, StatType stat);

const char*    GradeName(Grade g);
const char*    StatName(StatType s);
const wchar_t* GradeNameW(Grade g);
const wchar_t* StatNameW(StatType s);

// 저장/불러오기용
std::string  SerializeInventory(const Inventory& inv);
void         DeserializeInventory(const std::string& data, Inventory& inv);
