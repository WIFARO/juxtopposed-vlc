/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef VLC_LIBASS_PLAINTEXT_H
#define VLC_LIBASS_PLAINTEXT_H

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ASS stores vector drawing commands in the same field as readable text.
 * Keep track of drawing mode while removing author formatting, so coordinates
 * never become visible captions when embedded formatting is disabled. */
static inline char *AssPlainText(const char *text)
{
    char *plain = (char *)malloc(strlen(text) + 1);
    if (plain == NULL)
        return NULL;
    char *out = plain;
    bool drawing = false;
    for (const char *p = text; *p;) {
        if (*p == '{') {
            const char *end = strchr(p + 1, '}');
            if (end != NULL) {
                for (const char *tag = p + 1; tag < end; ++tag) {
                    if (*tag != '\\' || tag + 1 == end)
                        continue;
                    ++tag;
                    if (*tag == 'r')
                        drawing = false;
                    else if (*tag == 'p') {
                        const char *value = tag + 1;
                        while (value < end && (*value == ' ' || *value == '\t'))
                            ++value;
                        if (value < end && (*value == '+' || *value == '-'))
                            ++value;
                        if (value < end && *value >= '0' && *value <= '9')
                            drawing = strtol(tag + 1, NULL, 10) > 0;
                    }
                }
                p = end + 1;
                continue;
            }
        }
        if (*p == '\\' && p[1] != '\0') {
            if (p[1] == 'N' || p[1] == 'n') {
                if (!drawing) *out++ = '\n';
                p += 2;
                continue;
            }
            if (p[1] == 'h') {
                if (!drawing) { *out++ = '\xc2'; *out++ = '\xa0'; }
                p += 2;
                continue;
            }
        }
        if (!drawing) *out++ = *p;
        ++p;
    }
    *out = '\0';
    return plain;
}
#endif
