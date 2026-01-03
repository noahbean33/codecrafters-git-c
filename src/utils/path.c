#include <stdio.h>

char *buildPath(const char *hash) {
    static char path[256];
    snprintf(path, sizeof(path), ".git/objects/%c%c/%s", hash[0], hash[1], hash + 2);
    return path;
}