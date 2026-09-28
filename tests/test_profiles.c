/* profiles.c: the packed format, version 4, and a version 3 file written by
   the code before AmneziaWG. */
#include <string.h>
#include "check.h"
#include "profiles.h"

int main(void)
{
    static profile_store s, back;
    static unsigned char blob[4 * 1024 * 1024];
    size_t n, i;
    FILE *f;

    s.count = 2; s.active = 1;
    s.items[0].link.proto = LINK_WG; strcpy(s.items[0].link.awg, "jc = 4\npersistentkeepalive = 20-30\n");
    s.items[1].link.proto = LINK_WG; memset(s.items[1].link.awg, 'q', LINK_AWG_MAX - 1);
    n = profiles_pack(&s, blob, sizeof blob);
    CHECK(n > 0 && profiles_unpack(blob, n, &back) == 1);
    CHECK(!strcmp(back.items[0].link.awg, s.items[0].link.awg) && strlen(back.items[1].link.awg) == LINK_AWG_MAX - 1);
    for (i = 0; i < n; i += 7) profiles_unpack(blob, i, &back);          /* truncated: never crashes */
    f = fopen("fixtures/profiles_v3.bin", "rb");
    CHECK(f != NULL);
    if (f) {
        n = fread(blob, 1, sizeof blob, f); fclose(f);
        CHECK(profiles_unpack(blob, n, &back) == 1 && back.count == 1 &&
              !strcmp(back.items[0].link.server, "old.example") && !back.items[0].link.awg[0]);
    }
    return DONE("profiles");
}
