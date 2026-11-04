#include "hooks.h"

#include <cstdint>

#include "hook.h"
#include "targets.h"

namespace {

// Ledakan memanggil ini untuk membuat pemain lokal terpental menjauh dari titik
// ledakan. Membuang panggilannya membuat pemain tetap berdiri sementara tile dan
// efeknya tetap pecah.
void hookTerpental(void*, int32_t, float, float, int32_t, void*) {}

} // namespace

void pasangHookTerpental() {
    pasangHook(target(T_TUMBLE_EXPLODE), reinterpret_cast<void*>(hookTerpental));
}
