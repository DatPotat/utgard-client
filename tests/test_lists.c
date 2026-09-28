/* lists.c: the site list becomes a sing-box rule-set source. */
#include <string.h>
#include "check.h"
#include "lists.h"
#include "parson.h"

static char out[1 << 16];

static int has(JSON_Array *a, const char *v)
{
    size_t i;
    for (i = 0; a && i < json_array_get_count(a); i++)
        if (!strcmp(json_array_get_string(a, i), v)) return 1;
    return 0;
}

int main(void)
{
    lists_stats st;
    JSON_Value *v;
    JSON_Object *rule;
    int removed = 0;

    CHECK(lists_build_text("# comment\n\nExample.com\nwww.example.com\nexample.com\nru\n"
                           "198.51.100.0/24\n203.0.113.7\nhttps://spotify.com/path\n",
                           out, sizeof out, &st));
    v = json_parse_string(out);
    CHECK(v != NULL);
    rule = json_array_get_object(json_object_get_array(json_value_get_object(v), "rules"), 0);
    CHECK(rule && has(json_object_get_array(rule, "domain_suffix"), "example.com"));
    CHECK(!has(json_object_get_array(rule, "domain_suffix"), "www.example.com"));   /* collapsed */
    CHECK(!has(json_object_get_array(rule, "domain_suffix"), "ru"));                /* single label */
    CHECK(has(json_object_get_array(rule, "domain_suffix"), "spotify.com"));        /* URL reduced to host */
    CHECK(has(json_object_get_array(rule, "ip_cidr"), "198.51.100.0/24"));
    CHECK(st.invalid == 1 && st.collapsed == 1 && st.duplicates == 1 && !st.empty);
    json_value_free(v);

    /* nothing usable: still a valid file, with a rule that matches nothing */
    CHECK(lists_build_text("# only notes\n\n", out, sizeof out, &st) && st.empty);
    v = json_parse_string(out);
    CHECK(v != NULL && strstr(out, "invalid.placeholder.local"));
    json_value_free(v);

    /* tidy keeps the user's notes and order, drops repeats and covered ones */
    CHECK(lists_tidy_text("# mine\nexample.com\nwww.example.com\nEXAMPLE.com\nother.org\n",
                          out, sizeof out, &removed) && removed == 2);
    CHECK(!strcmp(out, "# mine\r\nexample.com\r\nother.org\r\n"));   /* written back with Windows line ends */

    /* zapret lists: a single label is a legitimate zone there */
    CHECK(lists_tidy_zapret("ru\nexample.ru\n", out, sizeof out, &removed) && removed == 1 &&
          !strcmp(out, "ru\r\n"));

    CHECK(!lists_build_text("example.com\n", out, 8, &st));    /* output does not fit */
    return DONE("lists");
}
