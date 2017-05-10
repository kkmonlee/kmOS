//
// Created by aa on 10/05/17.
//

#include "string.hpp"
#include <os.h>

extern "C" {
int strlen(char* s) {
    int i = 0;
    while (*s++) {
        i++;
    }
    return i;
}

char *Strncpy(char* destString, const char* sourceString, int maxLength) {
    unsigned count;

    if ((destString == (char*) NULL) || (sourceString == (char*) NULL)) {
        return (destString = NULL);
    }

    if (maxLength > 255) {
        maxLength = 255;
    }

    for (count = 0; (int) count < (int) maxLength; count++) {
        destString[count] = sourceString[count];
        if (sourceString[count] == '\0') break;
    }

    if (count > 255) {
        return (destString = NULL);
    }

    return destString;
}


}