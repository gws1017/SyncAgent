#include "equipment.h"
#include "lang.h"
#include <random>
#include <sstream>
#include <algorithm>
#include <cmath>

static std::mt19937 g_rng{ std::random_device{}() };

// ---- 등급/스탯 이름 ---------------------------------------------------------
const char* GradeName(Grade g) {
    switch (g) {
    case Grade::Common:    return T("일반", "Common");
    case Grade::Rare:      return T("희귀", "Rare");
    case Grade::Epic:      return T("영웅", "Epic");
    case Grade::Legendary: return T("전설", "Legendary");
    }
    return "?";
}

const char* StatName(StatType s) {
    switch (s) {
    case StatType::Attack:    return T("공격력", "Attack");
    case StatType::Xp:        return T("XP", "XP");
    case StatType::Gold:      return T("골드", "Gold");
    case StatType::Drop:      return T("드랍률", "Drop Rate");
    case StatType::Defense:   return T("방어력", "Defense");
    case StatType::Lifesteal: return T("체력흡수", "Lifesteal");
    case StatType::AtkSpeed:  return T("공격속도", "Atk Speed");
    default: break;
    }
    return "?";
}

const wchar_t* GradeNameW(Grade g) {
    switch (g) {
    case Grade::Common:    return TW(L"일반", L"Common");
    case Grade::Rare:      return TW(L"희귀", L"Rare");
    case Grade::Epic:      return TW(L"영웅", L"Epic");
    case Grade::Legendary: return TW(L"전설", L"Legendary");
    }
    return L"?";
}

const wchar_t* StatNameW(StatType s) {
    switch (s) {
    case StatType::Attack:    return TW(L"공격력", L"Attack");
    case StatType::Xp:        return TW(L"XP", L"XP");
    case StatType::Gold:      return TW(L"골드", L"Gold");
    case StatType::Drop:      return TW(L"드랍률", L"Drop Rate");
    case StatType::Defense:   return TW(L"방어력", L"Defense");
    case StatType::Lifesteal: return TW(L"체력흡수", L"Lifesteal");
    case StatType::AtkSpeed:  return TW(L"공격속도", L"Atk Speed");
    default: break;
    }
    return L"?";
}

// 등급별 스탯 보너스
static float BonusForGrade(Grade g) {
    switch (g) {
    case Grade::Common:    return 0.05f;
    case Grade::Rare:      return 0.15f;
    case Grade::Epic:      return 0.30f;
    case Grade::Legendary: return 0.60f;
    }
    return 0.0f;
}

// 합성 성공률
static int SuccessRate(Grade g) {
    switch (g) {
    case Grade::Common:    return 100;
    case Grade::Rare:      return 70;
    case Grade::Epic:      return 40;
    default:               return 0;  // 전설은 합성 불가
    }
}

// ---- 아이템 생성 ------------------------------------------------------------
Item MakeItem(Grade g) {
    std::uniform_int_distribution<int> statRoll(0, (int)StatType::STAT_COUNT - 1);
    return Item{ g, (StatType)statRoll(g_rng), BonusForGrade(g) };
}

Item CraftItem(Grade grade) {
    if (grade == Grade::Legendary) return MakeItem(Grade::Legendary); // 호출 안 되어야 함

    std::uniform_int_distribution<int> roll(1, 100);
    bool success = (roll(g_rng) <= SuccessRate(grade));

    if (success) {
        return MakeItem((Grade)((int)grade + 1));
    } else {
        // 실패: 재료와 같은 등급 1개 반환 (나머지 2개는 소멸)
        return MakeItem(grade);
    }
}

// ---- 장착 / 해제 ------------------------------------------------------------
bool TryEquip(Inventory& inv, int itemIdx) {
    if (itemIdx < 0 || itemIdx >= (int)inv.items.size()) return false;
    if ((int)inv.equipped.size() >= Inventory::MAX_EQUIP) return false;
    inv.equipped.push_back(inv.items[itemIdx]);
    inv.items.erase(inv.items.begin() + itemIdx);
    return true;
}

void Unequip(Inventory& inv, int equipIdx) {
    if (equipIdx < 0 || equipIdx >= (int)inv.equipped.size()) return;
    // 인벤토리 꽉 찼으면 장착 해제 불가 (자리 먼저 확보)
    if ((int)inv.items.size() >= MaxItems(inv)) return;
    inv.items.push_back(inv.equipped[equipIdx]);
    inv.equipped.erase(inv.equipped.begin() + equipIdx);
}

void DeleteItem(Inventory& inv, int itemIdx) {
    if (itemIdx < 0 || itemIdx >= (int)inv.items.size()) return;
    inv.items.erase(inv.items.begin() + itemIdx);
}

int MaxItems(const Inventory& inv) {
    return Inventory::MAX_ITEMS + inv.bagExpandCount * Inventory::BAG_EXPAND_SLOTS;
}

bool ExpandBag(Inventory& inv) {
    if (inv.bagExpandCount >= Inventory::MAX_BAG_EXPANSIONS) return false;
    inv.bagExpandCount++;
    return true;
}

bool AutoCraftTick(Inventory& inv) {
    bool craftedAny = false;
    const Grade order[] = { Grade::Common, Grade::Rare, Grade::Epic };
    for (Grade g : order) {
        int cnt = 0;
        for (const Item& it : inv.items) if (it.grade == g) cnt++;
        if (cnt < 3) continue;
        int removed = 0;
        for (int i = (int)inv.items.size() - 1; i >= 0 && removed < 3; i--) {
            if (inv.items[i].grade == g) { inv.items.erase(inv.items.begin() + i); removed++; }
        }
        // 재료 3개 제거 후 결과 1개 추가 = 순감소 2칸이라 항상 자리가 남음(공간 체크 불필요).
        inv.items.push_back(CraftItem(g));
        craftedAny = true;
    }
    return craftedAny;
}

// 등급이 높을수록 리롤 비용도 커짐 (희귀도에 비례)
long long RerollCost(Grade g) {
    return (long long)(100.0 * std::pow(4.0, (int)g));
}

void RerollItem(Item& item) {
    std::uniform_int_distribution<int> statRoll(0, (int)StatType::STAT_COUNT - 1);
    item.stat = (StatType)statRoll(g_rng); // 등급/보너스%는 유지, 스탯 종류만 재추첨
    item.rerolled = true; // 아이템당 평생 한 번만 — 다시는 리롤 못 함
}

// ---- 스탯 합산 --------------------------------------------------------------
float GetEquippedBonus(const Inventory& inv, StatType stat) {
    float total = 0.0f;
    for (const Item& item : inv.equipped)
        if (item.stat == stat) total += item.bonus;
    return total;
}

// ---- 저장 / 불러오기 ---------------------------------------------------------
// 포맷: "g s r | ..." (보관함) "/ g s r | ..." (장착) "/ 확장횟수"
std::string SerializeInventory(const Inventory& inv) {
    std::ostringstream oss;
    auto write = [&](const std::vector<Item>& list) {
        for (int i = 0; i < (int)list.size(); i++) {
            if (i) oss << '|';
            oss << (int)list[i].grade << ' ' << (int)list[i].stat << ' ' << (list[i].rerolled ? 1 : 0);
        }
    };
    write(inv.items);
    oss << '/';
    write(inv.equipped);
    oss << '/' << inv.bagExpandCount;
    return oss.str();
}

void DeserializeInventory(const std::string& data, Inventory& inv) {
    inv.items.clear();
    inv.equipped.clear();
    inv.bagExpandCount = 0;

    auto parse = [](const std::string& chunk, std::vector<Item>& out) {
        if (chunk.empty()) return;
        std::istringstream ss(chunk);
        std::string token;
        while (std::getline(ss, token, '|')) {
            int g = 0, s = 0, r = 0;
            int n = sscanf(token.c_str(), "%d %d %d", &g, &s, &r);
            if (n == 2 || n == 3) { // 구버전 세이브는 r 필드가 없었으니 기본값(0)으로 처리
                Grade  grade = (Grade)std::clamp(g, 0, 3);
                StatType stat = (StatType)std::clamp(s, 0, (int)StatType::STAT_COUNT - 1);
                out.push_back({ grade, stat, BonusForGrade(grade), r != 0 });
            }
        }
    };

    auto slash1 = data.find('/');
    if (slash1 == std::string::npos) return;
    parse(data.substr(0, slash1), inv.items);

    auto slash2 = data.find('/', slash1 + 1);
    std::string equippedChunk = (slash2 == std::string::npos) ? data.substr(slash1 + 1) : data.substr(slash1 + 1, slash2 - slash1 - 1);
    parse(equippedChunk, inv.equipped);

    // 구버전 세이브는 확장횟수 필드가 없었으니 그때는 그냥 0으로 둠(기본값).
    if (slash2 != std::string::npos) {
        int expand = 0;
        if (sscanf(data.c_str() + slash2 + 1, "%d", &expand) == 1)
            inv.bagExpandCount = std::clamp(expand, 0, Inventory::MAX_BAG_EXPANSIONS);
    }
}
