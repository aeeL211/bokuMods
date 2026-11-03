#include "hooks.h"

#include "hook.h"
#include "targets.h"

namespace {

// A food leaves the hand only from its own ...__Eat__End, which asks
// Player__Manager.Hh__Change for the empty hand id. Every food and posture has
// its own copy of that method, so each keeps the hand change back and the piece
// stays in hand. Sushi, ice cream and popsicle stay out on purpose: one bite
// ends those items, and the game then parks the player in a finished pose that
// keeps the hand layer off, so the kept piece stays invisible until the next
// move. Gingerbread and hot cross bun end their last bite in a pose that still
// draws the hand.
//
// The eat animation hides the held sprite right after that hand change, so the
// hand is rebuilt at the end of that player's update, when the state machine has
// moved past the eat animation. Without it the food is still in hand but
// invisible until the player moves and the state refresh rebuilds it.
template <TargetId id> void eatEndHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(id).orig);
    if (!orig)
        return;
    setKeepHand(true);
    orig(self, method);
    setKeepHand(false);
    refreshHand();
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
    installEatEnd<T_EAT_END_BUN_KNEEL>("Player__State__Sp__Tile__Kneel.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_SIT_F>("Player__State__Sp__Tile__Sit__F.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_SIT_S>("Player__State__Sp__Tile__Sit__S.Hot_Cross_Bun__Eat__End");
    installEatEnd<T_EAT_END_BUN_FOOD>(
        "Player__State__Std__Action__Hh__Food.Hot_Cross_Bun__Eat__End");
}
