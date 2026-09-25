#ifndef UTGARD_VERSION_H
#define UTGARD_VERSION_H

/* The one place the version lives. Must match the tag of the GitHub
   release this build is published under: the update check compares the
   two, and the settings page links to that release. Tags are plain
   numbers (2.1.0). build.sh reads this line to stamp the manifest
   (X.Y.Z.0), so keep it three numbers. */
#define UTGARD_VERSION "2.1.0"

#define UTGARD_WIDEN_(x) L##x
#define UTGARD_WIDEN(x)  UTGARD_WIDEN_(x)
#define UTGARD_VERSION_W UTGARD_WIDEN(UTGARD_VERSION)

#define UTGARD_RELEASES_URL L"https://github.com/DatPotat/utgard-client/releases/latest"
#define UTGARD_RELEASE_URL  L"https://github.com/DatPotat/utgard-client/releases/tag/" UTGARD_VERSION_W

#endif
