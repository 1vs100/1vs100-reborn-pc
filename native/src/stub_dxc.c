// Links in place of libdxcompiler on platforms without a DXC release.
// DxcCreateInstance always fails, so runtime shader translation fails
// cleanly and the game falls back to prebuilt shader packs. Kept out of the
// game target itself: with a real DXC beside the executable, the real
// DxcCreateInstance must win.
#include <stddef.h>

int DxcCreateInstance(const void *rclsid, const void *riid, void **ppv)
{
    (void)rclsid;
    (void)riid;
    if (ppv != NULL)
        *ppv = NULL;
    return (int)0x80004005;
}
