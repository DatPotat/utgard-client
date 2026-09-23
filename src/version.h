#ifndef UTGARD_VERSION_H
#define UTGARD_VERSION_H

/* Must match the tag of the GitHub release this build is published under:
   the update check compares the two. Tags are plain numbers (2.0.0). */
#define UTGARD_VERSION "2.0.1"

#define UTGARD_WIDEN_(x) L##x
#define UTGARD_WIDEN(x)  UTGARD_WIDEN_(x)
#define UTGARD_VERSION_W UTGARD_WIDEN(UTGARD_VERSION)

#define UTGARD_RELEASES_URL L"https://github.com/DatPotat/utgard-client/releases/latest"

#endif
