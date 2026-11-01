# Third-party notices

`libfebrian.so` links the following third-party code. Nothing is vendored in this repository:
every component is fetched from its upstream release at build time.

## ShadowHook

- Upstream: https://github.com/bytedance/android-inline-hook
- Version: v2.0.1, pinned in `.github/workflows/build.yml` (`SHADOWHOOK_TAG`) and overridable
  with the `shadowhook_tag` workflow input.
- License: MIT
- How it is used: the upstream source archive is extracted during the CI build and compiled
  into `libfebrian.so` as a static archive. `sh_jni.c` is excluded, so the binary contains no
  ShadowHook `JNI_OnLoad`. Public ShadowHook symbols are hidden and are not exported.

## xdl

- Upstream: https://github.com/hexhacking/xdl, bundled inside ShadowHook under
  `shadowhook/src/main/cpp/third_party/xdl`
- License: Apache License 2.0
- How it is used: compiled from the same ShadowHook source archive and linked statically.

## linux-syscall-support

- Upstream: https://chromium.googlesource.com/linux-syscall-support, bundled inside ShadowHook
  under `shadowhook/src/main/cpp/third_party/lss`
- License: Apache License 2.0 (BSD-style option)
- How it is used: headers only, compiled into the ShadowHook objects.

The full license texts are collected at build time into `THIRD_PARTY_LICENSES`, which is
published next to `libfebrian.so` in the `libfebrian-all` artifact.
