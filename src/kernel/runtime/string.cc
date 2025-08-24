#include <os.h>

extern "C" {
    int strlen(char *s) {
        int i = 0;
        while (*s++) i++;
        return i;
    }

    char *strncpy(char *destString, const char *sourceString, int maxLength) {
        unsigned count;

        if ((destString == (char *) NULL) || (sourceString == (char *) NULL)) {
            return (destString = NULL);
        }

        if (maxLength > 255)
            maxLength = 255;

        for (count = 0; (int)count < (int)maxLength; count++) {
            destString[count] = sourceString[count];
            if (sourceString[count] == '\0') 
                break;
        }

        if (count >= 255) {
            return (destString = NULL);
        }

        return (destString);
    }

    int strcmp(const char *dst, const char *src) {
        int i = 0;

        while ((dst[i] == src[i])) {
            if (src[i++] == 0)
                return 0;
        }

        return 1;
    }

    int strcpy(char *dst, const char *src) {
        int i = 0;
        while ((dst[i] = src[i]) != 0) { i++; }

        return i;
    }

    void *memcpy(void *dest, const void *src, int n) {
        char *d = (char*)dest;
        const char *s = (const char*)src;
        for (int i = 0; i < n; i++) {
            d[i] = s[i];
        }
        return dest;
    }

    void *memset(void *s, int c, int n) {
        char *ptr = (char*)s;
        for (int i = 0; i < n; i++) {
            ptr[i] = c;
        }
        return s;
    }


    void strcat(void *dest, const void *src) {
        memcpy((char*) ((int)dest + (int)strlen((char*)dest)), (char*)src, strlen((char*)src));
    }

    int strncmp(const char* s1, const char* s2, int c) {
        int result = 0;

        while (c) {
            result = *s1 - *s2++;

            if ((result != 0) || (*s1++ == 0)) {
                break;
            }

            c--;
        }

        return result;
    }
}
