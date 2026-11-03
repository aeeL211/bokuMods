#include "targets.h"

#include <cstring>

#include "il2cpp_api.h"

namespace {

Target targets[T_COUNT] = {
    {"Iap", "BuyProductID", 1, nullptr, nullptr, nullptr},
    {"UI__Iap", "Purchase__Complete", 1, nullptr, nullptr, nullptr},
    {"_Unlock", "Check", 4, nullptr, nullptr, nullptr},
    {"Net__Multi__Client", "Permission__Start_Game", 3, nullptr, nullptr, nullptr},
    {"Net__Multi__Client", "Permission__Save_World__Receive", 1, nullptr, nullptr, nullptr},
    {"UI__Menu__Net__Multi__Join__Room_List__Detail__Password", "Input__Ok", 0, nullptr, nullptr,
     nullptr},
    {"UI__Menu__Net__Multi__Join__Room_List__Detail__Password", "Input_Field__Input", 1, nullptr,
     nullptr, nullptr},
    {"UIInput", "Validate", 1, nullptr, nullptr, nullptr},
    {"UIInput", "Insert", 1, nullptr, nullptr, nullptr},
    {"UI__Manager", "Keyboard__Open", 4, nullptr, nullptr, nullptr},
    {"Net__Account__Manager", "Name__Check", 2, nullptr, nullptr, nullptr},
    {"UI__Menu__Option__Profile", "Birthday__Refresh_", 4, nullptr, nullptr, nullptr},
    {"UI__Menu__Option__Profile", "Input_Field__Input__Birthday__Length", 1, nullptr, nullptr,
     nullptr},
    {"UI__Menu__Option__Profile", "Input_Field__End__Birthday", 1, nullptr, nullptr, nullptr},
    {"MyBehaviour", "Date_Check", 3, nullptr, nullptr, nullptr},
    {"UI__Menu__Model__Main__Pos", "Refresh__Step", 0, nullptr, nullptr, nullptr},
    {"Net__Account__Portrait__Main__Pos", "Refresh__Step", 0, nullptr, nullptr, nullptr},
    {"UI__Menu__Option__About", "Refresh__Version__Version", 0, nullptr, nullptr, nullptr},
    {"Title__Cmn", "Version__Text", 0, nullptr, nullptr, nullptr},
    {"UI__Menu__Net__Multi__Help", "Refresh__Version__Text", 0, nullptr, nullptr, nullptr},
    {"UI__Menu__Net__Multi__Join__Room_List__Detail__Password", "Term__Bottom_Button", 0, nullptr,
     nullptr, nullptr},
    {"_Tile", "Key__To__Id", 1, nullptr, nullptr, nullptr},
    {"Converter__Tile__Type", "Term__Eat__Tile__Change", 1, nullptr, nullptr, nullptr},
    {"Converter__Tile__Type", "Eat__Tile__Change__Add", 1, nullptr, nullptr, nullptr},
    {"Player__Manager", "Hh__Change", 1, nullptr, nullptr, nullptr},
    {"Player__State__Sp__Tile__Kneel", "Gingerbread_Man__Eat__End", 0, nullptr, nullptr, nullptr},
    {"Player__State__Sp__Tile__Sit__F", "Gingerbread_Man__Eat__End", 0, nullptr, nullptr, nullptr},
    {"Player__State__Sp__Tile__Sit__S", "Gingerbread_Man__Eat__End", 0, nullptr, nullptr, nullptr},
    {"Player__State__Std__Action__Hh__Food", "Gingerbread_Man__Eat__End", 0, nullptr, nullptr,
     nullptr},
    {"UI__Menu__Net__Multi__Join__Room_List", "Term__Room__Data__Get__World_Size_", 2, nullptr,
     nullptr, nullptr},
    {"UI__Menu__World__New__Setting__Size", "_Start", 0, nullptr, nullptr, nullptr},
    {"Guest__Manager", "Add", 18, nullptr, nullptr, nullptr},
    {"Data__Model", "File__Write__Data", 2, nullptr, nullptr, nullptr},
    {"Data__Model", "File__Path__Data", 1, nullptr, nullptr, nullptr},
    {"Net__Account__Manager", "Term__Privacy__Visible", 3, nullptr, nullptr, nullptr},
    {"Net__Account__Blacklist", "Term__Privacy__Visible", 1, nullptr, nullptr, nullptr},
    {"Net__Account__Add_Friend", "Term__Privacy__Visible", 1, nullptr, nullptr, nullptr},
    {"Net__Account__Friend_Profile", "Term__Privacy__Visible", 1, nullptr, nullptr, nullptr},
    {"Net__Account__Verify", "Term__Privacy__Visible", 1, nullptr, nullptr, nullptr},
    {"Net__Account__Verify", "Term__Privacy__Visible__Email", 1, nullptr, nullptr, nullptr},
    {"_User", "Privacy__Visible__Int__To__Key", 1, nullptr, nullptr, nullptr},
    {"Guest__Manager", "Account__Common", 7, nullptr, nullptr, nullptr},
    {"Guest__Manager", "Account__Privacy__Email", 3, nullptr, nullptr, nullptr},
    {"Player__State__Cmn", "Term__Gun_Shoot__End__Update__End", 0, nullptr, nullptr, nullptr},
    {"Player__State__Std__Action__Hh__Weapon", "Rockman__Rock_Buster__End__Update", 0, nullptr,
     nullptr, nullptr},
    {"Player__State__Cmn", "Rockman__Rock_Buster__Id", 0, nullptr, nullptr, nullptr},
    {"Camera__Manager", "Shake", 0, nullptr, nullptr, nullptr},
    {"Camera__Manager", "Shake", 2, nullptr, nullptr, nullptr},
    {"Eff__Type__Tile", "Debris", 1, nullptr, nullptr, nullptr},
    {"Eff__Type__Tile", "Debris__Tile", 3, nullptr, nullptr, nullptr},
    {"Net__Account__My_Account__Detail", "Introduce__Set", 1, nullptr, nullptr, nullptr},
    {"Net__Multi__Guest__Chat", "Msg__Receive__Text", 3, nullptr, nullptr, nullptr},
    {"Net__Multi__Chat", "Msg__Receive__Text", 2, nullptr, nullptr, nullptr},
    {"Player__Manager", "Key", 0, nullptr, nullptr, nullptr},
    {"UI__Note", "Term__Sensitive_Word", 3, nullptr, nullptr, nullptr},
    {"Player__State__Std__Action__Hh__Weapon", "Gun_Shoot__Action__Update", 0, nullptr, nullptr,
     nullptr},
    {"Player__State__Std__Action__Hh__Weapon", "Rockman__Rock_Buster__Action__Update", 0, nullptr,
     nullptr, nullptr},
    {"Global", "Data__Load__Data_Slot_Num_", 3, nullptr, nullptr, nullptr},
    {"Player__State", "_Update", 0, nullptr, nullptr, nullptr},
    {"Player__State__Cmn", "Term__Gun_Shoot__Action__Update__End", 1, nullptr, nullptr, nullptr},
    {"Player__State__Cmn", "Term__Rockman__Rock_Buster__Action__Update__End", 0, nullptr, nullptr,
     nullptr},
    {"Player__State__Cmn", "Term__Rockman__Rock_Buster__End__To__Action", 0, nullptr, nullptr,
     nullptr},
    {"Player__State__Std__Action__Hh__Weapon", "Rockman__Rock_Buster__Action", 1, nullptr, nullptr,
     nullptr},
    {"Player__State__Cmn", "Rockman__Rock_Buster__End__Dir", 0, nullptr, nullptr, nullptr},
    {"Player__State__Std__Air", "Tumble__Explode", 3, nullptr, nullptr, nullptr},
    {"Player__State__Std__Action__Hh__Nature", "Feces__Throw__Action", 1, nullptr, nullptr,
     nullptr},
    {"Player__State__Std__Action__Hh__Nature", "Feces__Throw__Action__Update", 0, nullptr, nullptr,
     nullptr},
    {"Net__Account__Manager", "User__Set", 4, nullptr, nullptr, nullptr},
    {"Net__Account__Manager", "User__Set__From_Local", 2, nullptr, nullptr, nullptr, "String"},
    {"Net__Account__Manager", "User__Set__From_Local", 2, nullptr, nullptr, nullptr, "String[]"},
    {"Net__Account__Manager", "User__Set__From_Local", 2, nullptr, nullptr, nullptr, "List`1"},
    {"Guest__Manager", "Account__User_Name", 2, nullptr, nullptr, nullptr},
    {"Data__Model", "File__Write__Name", 2, nullptr, nullptr, nullptr},
};

// Overloads can share a name and an arity, so a target may also name the class
// of its last parameter. An array class is "String[]" and a generic one keeps
// its "List`1" name, so an exact compare is enough to pick one overload.
bool paramMatches(const Target& t, void* m) {
    if (!t.paramType)
        return true;
    Api& api = il2cppApi();
    void* type = api.param(m, static_cast<uint32_t>(t.argc - 1));
    void* cls = type ? api.classFromType(type) : nullptr;
    const char* name = cls ? api.className(cls) : nullptr;
    return name && !strcmp(t.paramType, name);
}

} // namespace

Target& target(TargetId id) { return targets[id]; }

int scan() {
    Api& api = il2cppApi();
    void* dom = api.domain();
    if (!dom)
        return 0;

    size_t assemblyCount = 0;
    void** assemblies = api.assemblies(dom, &assemblyCount);
    if (!assemblies)
        return 0;

    for (size_t i = 0; i < assemblyCount; i++) {
        void* img = api.image(assemblies[i]);
        if (!img)
            continue;

        size_t count = api.classCount(img);
        for (size_t j = 0; j < count; j++) {
            void* cls = api.classAt(img, j);
            if (!cls)
                continue;

            const char* cn = api.className(cls);
            if (!cn)
                continue;

            bool wanted = false;
            for (const Target& t : targets) {
                if (!strcmp(t.cls, cn)) {
                    wanted = true;
                    break;
                }
            }
            if (!wanted)
                continue;

            void* it = nullptr;
            while (void* m = api.methods(cls, &it)) {
                const char* mn = api.methodName(m);
                if (!mn)
                    continue;

                int argc = api.paramCount(m);
                for (Target& t : targets) {
                    if (t.addr)
                        continue;
                    if (strcmp(t.cls, cn) || strcmp(t.name, mn) || t.argc != argc)
                        continue;
                    if (!paramMatches(t, m))
                        continue;

                    void* a = methodAddr(m);
                    if (!hasBody(a))
                        continue;
                    t.addr = a;
                    t.method = m;
                    bindClass(cn, cls);
                }
            }
        }
    }

    int found = 0;
    for (const Target& t : targets) {
        if (t.addr)
            found++;
    }
    return found;
}
