#include "hooks.h"

#include "hook.h"
#include "targets.h"

namespace {

// A food leaves the hand only from its own ...__Eat__End, which asks
// Player__Manager.Hh__Change for the empty hand id. Every food and posture has
// its own copy of that method, so each keeps the hand change back and the piece
// stays in hand for the next bite. Ice cream stays out on purpose: its treats
// end the item by design.
//
template <TargetId id> void eatEndHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(id).orig);
    if (!orig)
        return;
    setKeepHand(true);
    orig(self, method);
    setKeepHand(false);
}

template <TargetId id> void installEatEnd(const char* label) {
    installHook(target(id), reinterpret_cast<void*>(eatEndHook<id>), label);
}

} // namespace

void installFoodHooks() {
    installEatEnd<T_EAT_END_KNEEL>("Player__State__Sp__Tile__Kneel.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_SIT_F>("Player__State__Sp__Tile__Sit__F.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_SIT_S>("Player__State__Sp__Tile__Sit__S.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_FOOD>("Player__State__Std__Action__Hh__Food.Gingerbread_Man__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_KNEEL>("Player__State__Sp__Tile__Kneel.Sushi__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_SIT_F>("Player__State__Sp__Tile__Sit__F.Sushi__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_SIT_S>("Player__State__Sp__Tile__Sit__S.Sushi__Eat__End");
    installEatEnd<T_EAT_END_SUSHI_FOOD>("Player__State__Std__Action__Hh__Food.Sushi__Eat__End");
    installEatEnd<T_EAT_END_BUN_KNEEL>("Player__State__Sp__Tile__Kneel.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_SIT_F>("Player__State__Sp__Tile__Sit__F.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_SIT_S>("Player__State__Sp__Tile__Sit__S.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_FOOD>(
        "Player__State__Std__Action__Hh__Food.Hot_Cross_Bun__Eat__End");
}
