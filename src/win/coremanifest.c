#include "coremanifest.h"

/* Hashes were taken from the official releases themselves: the sing-box
   archives for v1.14.1 and the AmneziaWG MSI packages of
   amneziawg-windows-client 3.1.0 (which publishes no checksum file), with
   every file inside hashed after extraction. Changing a version means
   changing every hash of that core in the same edit. */

#if defined(__aarch64__) || defined(_M_ARM64)
#define ARCH L"arm64"
#define SB_ZIP_SHA  L"58852d744056b76d1da08ad57e0f93e86fff11df49ab26a026ea4af3da840980"
#define SB_EXE_SHA  L"1f75caa353904c7d0f17b9c18765f5379f4a43056573c2ed8ec7e6edf4026d71"
#define SB_DLL_SHA  L"a75a1b99a7e31802cf67ee14763e5cb9db6a24aaf7062f86c49f2fc1c030fcf0"
#define AWG_MSI_SHA L"3aec023884944890fac151f9cd4be3e92d39f9987c49da248837529c006ea2aa"
#define AWG_EXE_SHA L"25fa81103152699bf416f394059b40d447001c3d0dea5ef889a9426f315e4c0a"
#define AWG_TUN_SHA L"f7ba89005544be9d85231a9e0d5f23b2d15b3311667e2dad0debd344918a3f80"
#elif defined(__x86_64__) || defined(_M_X64)
#define ARCH L"amd64"
#define SB_ZIP_SHA  L"5197f16d492d93202dc623622149a6ed040f8eca263128f91d603f2b901baa89"
#define SB_EXE_SHA  L"b838de45bd0b2e6ddbed1977e4745622f7dffab3b293807ff4c6b1b640fed909"
#define SB_DLL_SHA  L"3217c6260fbca5f16072e0b79735742f40109a63bb0ff88fd6b96dd6b54a2928"
#define AWG_MSI_SHA L"a1b48ea8699cd347832a3691d832004574ef8ad65bcf887611ac8acb99b7de8b"
#define AWG_EXE_SHA L"ba446f6e1a4093e43a65d6ff45f4b8c7b6485dc419327eedaa1a218549740e3a"
#define AWG_TUN_SHA L"e5da8447dc2c320edc0fc52fa01885c103de8c118481f683643cacc3220dafce"
#else
#error "Utgard is built for Windows amd64 and arm64 only"
#endif

#define SB_VERSION  L"1.14.1"
#define SB_ARCHIVE  L"sing-box-" SB_VERSION L"-windows-" ARCH L".zip"
#define AWG_VERSION L"3.1.0"
#define AWG_ARCHIVE L"amneziawg-" ARCH L"-" AWG_VERSION L".msi"

/* The LICENSE file is the same in both sing-box archives. */
#define SB_LICENSE_SHA L"bb3805862b583aee73ad6f7805ec634747a37257a637a3069857843f05ea589c"

const core_desc CORE_SINGBOX = {
    L"sing-box", SB_VERSION, L"sing-box", SB_ARCHIVE,
    L"https://github.com/SagerNet/sing-box/releases/download/v" SB_VERSION L"/" SB_ARCHIVE,
    SB_ZIP_SHA,
    { { L"sing-box.exe", SB_EXE_SHA, 1 },
      { L"libcronet.dll", SB_DLL_SHA, 1 },
      { L"LICENSE", SB_LICENSE_SHA, 0 } },
    3,
    0,     /* keeps working from FAT32 as before, with hashes and locking only */
    L"https://github.com/SagerNet/sing-box/releases/tag/v" SB_VERSION
};

const core_desc CORE_AWG = {
    L"AmneziaWG", AWG_VERSION, L"amneziawg", AWG_ARCHIVE,
    L"https://github.com/amnezia-vpn/amneziawg-windows-client/releases/download/"
        AWG_VERSION L"/" AWG_ARCHIVE,
    AWG_MSI_SHA,
    { { L"amneziawg.exe", AWG_EXE_SHA, 1 },
      { L"wintun.dll", AWG_TUN_SHA, 1 } },
    2,
    1,     /* runs as SYSTEM: never from a folder that cannot be protected */
    L"https://github.com/amnezia-vpn/amneziawg-windows-client/releases/tag/" AWG_VERSION
};
