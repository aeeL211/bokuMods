#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "il2cpp_api.h"
#include "log.h"
#include "targets.h"

namespace {

Field posM = {};
Field mainPos = {};
Field posStepsMax = {};
Field posStepsHalf = {};
Field posStepsW = {};
Field posW = {};
Field posValues = {};
Field valueMin = {};
Field valueMax = {};
Field valuePerStep = {};
Field portraitM = {};
Field portraitPos = {};

constexpr int kMaxAxes = 8;
constexpr int kRangeSlots = 2;
int32_t skinBase[kRangeSlots][kMaxAxes] = {};
int32_t skinWidened[kRangeSlots][kMaxAxes] = {};

// Refresh__Step resets the X/Y sliders to -5..+5 and derives steps_Half,
// steps_W and per_Step from steps_Max. Widen only the axes that are still at
// -5..+5 (the rotate axis is left alone) and rebuild the derived values so
// Value__To__Step sees the new -25..+25 range in the same Refresh() pass.
void patchRange(void* self, const Field& ownerField, const Field& posField, int slot) {
    if (!ownerField.field || !posField.field || !posStepsMax.field || !posStepsHalf.field ||
        !posStepsW.field || !posW.field || !posValues.field || !valueMin.field || !valueMax.field ||
        !valuePerStep.field) {
        return;
    }

    void* owner = readPtr(self, ownerField);
    void* pos = readPtr(owner, posField);
    if (!pos)
        return;

    void* values = readPtr(pos, posValues);
    void* stepsMax = readPtr(pos, posStepsMax);
    void* stepsHalf = readPtr(pos, posStepsHalf);
    void* stepsW = readPtr(pos, posStepsW);
    if (!values || !stepsMax || !stepsHalf || !stepsW)
        return;

    int32_t valueCount = arrayLength(values);
    int32_t maxCount = arrayLength(stepsMax);
    int32_t halfCount = arrayLength(stepsHalf);
    int32_t widthCount = arrayLength(stepsW);
    int32_t count = valueCount < maxCount ? valueCount : maxCount;
    if (count > kMaxAxes)
        count = kMaxAxes;
    if (count <= 0)
        return;

    float width = readFloat(pos, posW);

    for (int32_t i = 0; i < count; i++) {
        void* value = *reinterpret_cast<void**>(arrayData(values) + i * sizeof(void*));
        if (!value)
            continue;

        float min = readFloat(value, valueMin);
        float max = readFloat(value, valueMax);
        if (!nearValue(min, -5.0f) || !nearValue(max, 5.0f))
            continue;

        int32_t* maxPtr = reinterpret_cast<int32_t*>(arrayData(stepsMax) + i * sizeof(int32_t));
        int32_t current = *maxPtr;
        if (current <= 0) {
            LOGE("Skin range: axis %d has steps_Max=%d, skipped", i, current);
            continue;
        }
        if (current > INT32_MAX / 5) {
            LOGE("Skin range: axis %d has steps_Max=%d, skipped", i, current);
            continue;
        }

        int32_t base;
        int32_t widened;
        if (skinBase[slot][i] != 0 && current == skinBase[slot][i]) {
            base = skinBase[slot][i];
            widened = skinWidened[slot][i];
        } else if (skinWidened[slot][i] != 0 && current == skinWidened[slot][i]) {
            base = skinBase[slot][i];
            widened = skinWidened[slot][i];
        } else {
            base = current;
            widened = current * 5;
            skinBase[slot][i] = base;
            skinWidened[slot][i] = widened;
            LOGI("Skin range: axis %d widened %d -> %d steps", i, base, widened);
        }

        *maxPtr = widened;
        if (i < halfCount) {
            *reinterpret_cast<int32_t*>(arrayData(stepsHalf) + i * sizeof(int32_t)) = widened / 2;
        }
        if (i < widthCount && width > 0.0f) {
            *reinterpret_cast<float*>(arrayData(stepsW) + i * sizeof(float)) =
                width / static_cast<float>(widened);
        }

        writeFloat(value, valueMin, -25.0f);
        writeFloat(value, valueMax, 25.0f);
        writeFloat(value, valuePerStep, 10.0f / static_cast<float>(base));
    }
}

void posStepHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_POS_REFRESH_STEP).orig);
    if (!orig)
        return;
    orig(self, method);
    patchRange(self, posM, mainPos, 0);
}

// The portrait editor has its own _Pos instance and step arrays, so it gets a
// second memo slot instead of overwriting the model editor's.
void portraitStepHook(void* self, void* method) {
    auto orig = reinterpret_cast<void (*)(void*, void*)>(target(T_PORTRAIT_REFRESH_STEP).orig);
    if (!orig)
        return;
    orig(self, method);
    patchRange(self, portraitM, portraitPos, 1);
}

bool resolvePos(void* posClass) {
    return resolveField(posClass, "steps_Max", posStepsMax) &&
           resolveField(posClass, "steps_Half", posStepsHalf) &&
           resolveField(posClass, "steps_W", posStepsW) && resolveField(posClass, "w", posW) &&
           resolveField(posClass, "values", posValues) &&
           resolveField(fieldElementClass(posValues), "min", valueMin) &&
           resolveField(fieldElementClass(posValues), "max", valueMax) &&
           resolveField(fieldElementClass(posValues), "per_Step", valuePerStep);
}

} // namespace

void installSkinRangeHooks() {
    void* posComponent = classOf("UI__Menu__Model__Main__Pos");
    bool modelRange =
        resolveField(posComponent, "m", posM) && resolveField(fieldClass(posM), "_pos", mainPos);

    void* portraitComponent = classOf("Net__Account__Portrait__Main__Pos");
    bool portraitRange = resolveField(portraitComponent, "m", portraitM) &&
                         resolveField(fieldClass(portraitM), "_pos", portraitPos);

    bool posFields = resolvePos(modelRange ? fieldClass(mainPos) : fieldClass(portraitPos));
    if (modelRange && posFields) {
        LOGI("Skin range fields OK");
    } else {
        LOGE("Skin range fields not found; X/Y stays -5..5");
    }
    if (!portraitRange || !posFields) {
        LOGE("Portrait range fields not found; portrait X/Y stays -5..5");
    }

    installHook(target(T_POS_REFRESH_STEP), reinterpret_cast<void*>(posStepHook),
                "Pos.Refresh__Step");
    installHook(target(T_PORTRAIT_REFRESH_STEP), reinterpret_cast<void*>(portraitStepHook),
                "Portrait.Pos.Refresh__Step");
}
