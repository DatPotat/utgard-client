/* update.c: the release tag from GitHub's redirect and version comparison. */
#include <string.h>
#include "check.h"
#include "update.h"

int main(void)
{
    char tag[32];
    CHECK(update_tag_from_location("https://github.com/DatPotat/utgard-client/releases/tag/2.2.0", tag, sizeof tag) &&
          !strcmp(tag, "2.2.0"));
    CHECK(update_tag_from_location("https://github.com/x/y/releases/tag/v2.2.0-rc1+b7", tag, sizeof tag) &&
          !strcmp(tag, "v2.2.0-rc1+b7"));
    CHECK(!update_tag_from_location("https://github.com/x/y/releases", tag, sizeof tag));        /* no tag */
    CHECK(!update_tag_from_location("https://github.com/x/y/releases/tag/", tag, sizeof tag));   /* empty */
    CHECK(!update_tag_from_location("https://github.com/x/y/releases/tag/2.2.0\"><x", tag, sizeof tag));
    CHECK(!update_tag_from_location("https://github.com/x/y/releases/tag/2.2.0", tag, 3));       /* too long */

    CHECK(update_is_newer("2.2.0", "2.1.0") && update_is_newer("v2.2.0", "2.1.9"));
    CHECK(update_is_newer("2.10.0", "2.9.9"));             /* numeric, not text */
    CHECK(update_is_newer("2.1.0.1", "2.1.0"));
    CHECK(!update_is_newer("2.1.0", "2.1.0") && !update_is_newer("2.0.9", "2.1.0"));
    CHECK(!update_is_newer("2.1.0-rc1", "2.1.0"));         /* suffix ignored: equal */
    CHECK(!update_is_newer("latest", "2.1.0") && !update_is_newer("", "2.1.0"));
    CHECK(!update_is_newer("99999999999999999999.0", "2.1.0"));   /* overflow never "newer" */
    return DONE("update");
}
