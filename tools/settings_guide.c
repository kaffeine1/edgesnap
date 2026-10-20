/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * settings_guide - print the AmigaGuide node that documents every
 * setting, straight from the table in core/config.c.
 *
 * Documentation that is typed out by hand goes stale the first time a
 * range changes and nobody notices. This cannot: the labels, the help
 * lines, the ranges and the defaults printed here are the ones the
 * parser and the preferences window use.
 */

#include <stdio.h>
#include "config.h"

static void print_zone_values(void)
{
    printf("    Values: @{b}all@{ub}, @{b}none@{ub}, "
           "@{b}halves@{ub}, @{b}corners@{ub}, or a list of\n"
           "    @{b}left right topleft topright bottomleft "
           "bottomright maximize@{ub}\n"
           "    separated by commas. A word that is not one of these "
           "makes the\n    whole line be ignored - a typo must not "
           "silently disable half\n    the zones.\n");
}

static int es_same_group(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int main(void)
{
    const ESSetting *t;
    ESConfig def;
    ESHotkeys keys;
    int count = 0;
    int i;

    t = es_settings(&count);
    es_config_defaults(&def);
    es_hotkeys_defaults(&keys);

    printf("@node SETTINGS \"Settings\"\n");
    printf("@{b}Settings@{ub}\n\n");
    printf("The same words work in three places: the preferences file\n"
           "@{b}ENVARC:EdgeSnap.prefs@{ub}, the Shell arguments, and "
           "the tooltypes of\nthe commodity's icon. What you ask for "
           "on the command line wins\nover the file.\n\n");
    printf("  EdgeSnap ZONES=halves EDGEPX=24 BYPASSQUAL=alt\n\n");

    for (i = 0; i < count; i++) {
        int d = es_setting_value(&def, i);

        if (i == 0 || !es_same_group(t[i].page, t[i - 1].page)) {
            printf("@{b}%s page@{ub}\n\n", t[i].page);
        }
        /* The same sections the preferences windows show: the grouping
         * lives in the settings table, so all three agree. */
        if (i == 0 || !es_same_group(t[i].page, t[i - 1].page) ||
            !es_same_group(t[i].group, t[i - 1].group)) {
            printf("@{b}%s@{ub}\n\n", t[i].group);
        }
        printf("@{b}%s@{ub}: %s\n", t[i].key, t[i].label);
        printf("    %s\n", t[i].help);
        switch (t[i].kind) {
        case ES_SET_KEY:
            {
                const char *text = es_setting_text(&keys, i);

                printf("    A key as Exchange writes it, for example "
                       "@{b}ctrl alt x@{ub},\n"
                       "    or @{b}none@{ub} to leave it unset. "
                       "Default @{b}%s@{ub}.\n",
                       *text != '\0' ? text : "none");
                printf("    Up to %d printable ASCII characters. "
                       "@{b}off@{ub} also means none.\n",
                       ES_HOTKEY_SIZE - 1);
            }
            break;
        case ES_SET_INT:
            printf("    Whole number from %d to %d. Default %d.\n",
                   t[i].min, t[i].max, d);
            break;
        case ES_SET_BOOL:
            printf("    @{b}yes@{ub} or @{b}no@{ub}. Default %s.\n",
                   d ? "yes" : "no");
            break;
        case ES_SET_CHOICE:
            {
                int j;

                printf("    One of:");
                for (j = 0; t[i].choices[j] != 0; j++) {
                    printf(" @{b}%s@{ub}", t[i].choices[j]);
                }
                printf(". Default @{b}%s@{ub}.\n", t[i].choices[d]);
            }
            break;
        default:
            print_zone_values();
            break;
        }
        printf("\n");
    }
    printf("@endnode\n");
    return 0;
}
