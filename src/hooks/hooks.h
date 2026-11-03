#pragma once

void installIapHooks();
void installMultiplayerHooks();
void installChatHooks();
void installSensitiveHooks();
void installTextInputHooks();
void installProfileHooks();
void installPasswordHooks();
void installSkinRangeHooks();
void installAboutHooks();
void installFoodHooks();
void installSkinHooks();
void installPrivacyHooks();
void installAccountHooks();
void installWeaponHooks();
void installEffectHooks();
void installKnockbackHooks();
void installFecesHooks();
void installHandHooks();
void installSlotHooks();
void installJumpHooks();

// True while the interaction button is held for this player.
bool actionHeld(void* manager);
void resolveActionInput();

// Raised by a feature that keeps its item in hand while the stock call would
// clear the hand slot.
void setKeepHand(bool keep);

// Puts the last held id back into the hand after an eat animation hid the
// sprite, so the kept food stays visible.
void refreshHand();
