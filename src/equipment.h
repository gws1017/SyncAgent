#pragma once
#include <vector>
#include <string>

enum class Grade { Common = 0, Rare, Epic, Legendary };
enum class StatType { Attack = 0, Xp, Gold, Drop, Defense, Lifesteal, AtkSpeed, STAT_COUNT };

struct Item {
    Grade    grade;
    StatType stat;
    float    bonus; // ex) 0.05 = +5%
};

struct Inventory {
    static constexpr int MAX_ITEMS   = 20;
    static constexpr int MAX_EQUIP   = 5;

    std::vector<Item> items;   // 보관함
    std::vector<Item> equipped; // 장착 중 (최대 MAX_EQUIP)
};

// 합성: grade 등급 아이템 3개 소모 → 결과 아이템 반환
// 실패 시 한 단계 낮은 등급 반환 (일반은 항상 성공)
Item     MakeItem(Grade grade);    // 랜덤 스탯으로 아이템 생성
Item     CraftItem(Grade grade);   // 재료 소모는 호출자가 처리
bool     TryEquip(Inventory& inv, int itemIdx);
void     Unequip(Inventory& inv, int equipIdx);

// 장착된 아이템에서 스탯 합산
float    GetEquippedBonus(const Inventory& inv, StatType stat);

const char*    GradeName(Grade g);
const char*    StatName(StatType s);
const wchar_t* GradeNameW(Grade g);
const wchar_t* StatNameW(StatType s);

// 저장/불러오기용
std::string  SerializeInventory(const Inventory& inv);
void         DeserializeInventory(const std::string& data, Inventory& inv);
