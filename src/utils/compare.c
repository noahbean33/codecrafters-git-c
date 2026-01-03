#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../storage/object.h"

int compareEntries(const void *a, const void *b) {
    return strcmp(((Entry *)a)->name, ((Entry *)b)->name);
}