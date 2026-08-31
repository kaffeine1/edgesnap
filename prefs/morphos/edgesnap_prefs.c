/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * EdgeSnap preferences for MorphOS - a MUI window.
 *
 * The twin of prefs/os4/edgesnap_prefs.c, and deliberately so: both
 * walk es_settings(), the table in core/config.c that describes the
 * vocabulary, and turn each entry into the widget its kind calls for -
 * here a slider for a number, a checkmark for a flag, a cycle for a
 * choice, a row of checkmarks for the zone mask. Adding a setting to
 * the table puts it in both windows without either being touched.
 *
 * Save writes ENVARC: and ENV:, Use writes ENV: only, Cancel writes
 * nothing - the three buttons every Amiga preferences program has. The
 * running EdgeSnap watches ENV: and reconfigures itself, so there is
 * no protocol between this program and the commodity at all.
 */

#include <exec/types.h>
#include <libraries/mui.h>
#include <dos/dos.h>

#include <clib/alib_protos.h>   /* DoMethod                             */
#include <libraries/iffparse.h> /* MAKE_ID, for the window's saved id   */

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include "config.h"
#include "prefs_io.h"

static const char es_version_cookie[] __attribute__((used)) =
    "$VER: EdgeSnapPrefs 0.1 (29.8.2026) Michele Dipace";

#define ES_MAX_SETTINGS 24
#define ES_ZONE_COUNT   7
/* Six columns is three zones per row, a zone being a checkmark and its
 * label. The labels are short ("Top left", "Bottom right"), so three
 * sit comfortably and the block is two rows deep instead of one very
 * wide one. */
#define ES_ZONE_COLS    6

/* The zones in the order a person reads them, not the order the bits
 * happen to sit in. Kept identical to the AmigaOS 4 window. */
static const int es_zone_order[ES_ZONE_COUNT] = {
    ES_ZONE_LEFT, ES_ZONE_RIGHT,
    ES_ZONE_TOP_LEFT, ES_ZONE_TOP_RIGHT,
    ES_ZONE_BOTTOM_LEFT, ES_ZONE_BOTTOM_RIGHT,
    ES_ZONE_MAX
};

static const char *const es_zone_label[ES_ZONE_COUNT] = {
    "Left", "Right", "Top left", "Top right",
    "Bottom left", "Bottom right", "Maximize"
};

enum {
    ES_ID_SAVE = 1,
    ES_ID_USE,
    ES_ID_CANCEL
};

struct ESPrefsGui {
    ESConfig cfg;
    Object *app;
    Object *win;
    Object *field[ES_MAX_SETTINGS];
    Object *zone[ES_ZONE_COUNT];
    /* MUI keeps the pointer we hand it, it does not copy the text, so
     * the labels have to outlive the call that builds them. The table's
     * own labels have no colon: they also feed the AmigaGuide manual,
     * where "Edge distance: - Distance from..." would read badly. The
     * style guide wants one here, so the window adds it. */
    char label[ES_MAX_SETTINGS][40];
};

struct Library *MUIMasterBase;

/* ------------------------------------------------------ the widgets */

/*
 * A cycle gadget wants a plain char ** it can keep; the table hands
 * out const char *const *. Copying the pointers into a small array of
 * the right type is honest and costs nothing - the strings themselves
 * are literals that outlive the window.
 */
static char *es_cycle_entries[8];

static char **es_choices(const char *const *choices)
{
    int n = 0;

    while (choices[n] != 0 && n < 7) {
        es_cycle_entries[n] = (char *)choices[n];
        n++;
    }
    es_cycle_entries[n] = 0;
    return es_cycle_entries;
}

static Object *es_widget(struct ESPrefsGui *gui, const ESSetting *s,
                         int index)
{
    int value = es_setting_value(&gui->cfg, index);

    switch (s->kind) {
    case ES_SET_BOOL:
        {
            Object *o = MUI_MakeObject(MUIO_Checkmark, NULL);

            if (o != NULL) {
                set(o, MUIA_Selected, value ? TRUE : FALSE);
                set(o, MUIA_CycleChain, 1);
            }
            return o;
        }
    case ES_SET_CHOICE:
        {
            Object *o = MUI_MakeObject(MUIO_Cycle, NULL,
                                       es_choices(s->choices));

            if (o != NULL) {
                set(o, MUIA_Cycle_Active, value);
                set(o, MUIA_CycleChain, 1);
            }
            return o;
        }
    case ES_SET_ZONES:
        return NULL;    /* built by es_zone_group() */
    default:
        return SliderObject,
            MUIA_Numeric_Min, s->min,
            MUIA_Numeric_Max, s->max,
            MUIA_Numeric_Value, value,
            MUIA_CycleChain, 1,
        End;
    }
}

/* The zone mask gets a row of checkmarks rather than a cryptic number. */
static Object *es_zone_group(struct ESPrefsGui *gui)
{
    struct TagItem tags[2 + ES_ZONE_COUNT * 2 + ES_ZONE_COLS + 1];
    int n = 0;
    int cells = 0;
    int i;

    /*
     * A grid, not one long row. As a single horizontal group the seven
     * zones set a minimum width the window could never go below, which
     * is what stopped it from being resized (reported by a MorphOS user
     * against 0.1). Each pair is checkmark then label, so the label
     * sits on the right where the style guide wants it.
     */
    tags[n].ti_Tag = MUIA_Group_Columns;
    tags[n++].ti_Data = ES_ZONE_COLS;
    for (i = 0; i < ES_ZONE_COUNT; i++) {
        int zone = es_zone_order[i];
        int on = (gui->cfg.engine.zones_mask & ES_ZONEBIT(zone)) != 0;
        Object *check = MUI_MakeObject(MUIO_Checkmark, NULL);
        Object *label = Label((char *)es_zone_label[i]);

        gui->zone[i] = check;
        if (check == NULL) {
            continue;
        }
        set(check, MUIA_Selected, on ? TRUE : FALSE);
        set(check, MUIA_CycleChain, 1);
        tags[n].ti_Tag = MUIA_Group_Child;
        tags[n++].ti_Data = (ULONG)check;
        cells++;
        if (label != NULL) {
            tags[n].ti_Tag = MUIA_Group_Child;
            tags[n++].ti_Data = (ULONG)label;
            cells++;
        }
    }
    /*
     * A column group wants whole rows: seven zones are fourteen cells,
     * which does not divide by six. Fill the remainder with empty space
     * rather than leaving MUI to make sense of half a row.
     */
    while ((cells % ES_ZONE_COLS) != 0) {
        Object *gap = MUI_NewObject(MUIC_Rectangle, TAG_DONE);

        if (gap == NULL) {
            break;
        }
        tags[n].ti_Tag = MUIA_Group_Child;
        tags[n++].ti_Data = (ULONG)gap;
        cells++;
    }
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;
    return MUI_NewObjectA(MUIC_Group, tags);
}

/*
 * The settings group. Two columns - label, widget - and the children
 * are handed to MUI_NewObjectA as a tag array because how many there
 * are is the table's business, not this file's.
 */
/* The table's label with a colon, in storage that lives as long as the
 * window does. */
static const char *es_label(struct ESPrefsGui *gui, int index,
                            const char *text)
{
    char *out = gui->label[index];
    int n = 0;

    while (text[n] != '\0' && n < 38) {
        out[n] = text[n];
        n++;
    }
    out[n++] = ':';
    out[n] = '\0';
    return out;
}

static Object *es_settings_group(struct ESPrefsGui *gui)
{
    struct TagItem tags[4 + ES_MAX_SETTINGS * 2 + 1];
    const ESSetting *t;
    int count = 0;
    int n = 0;
    int i;

    t = es_settings(&count);
    if (count > ES_MAX_SETTINGS) {
        count = ES_MAX_SETTINGS;
    }

    tags[n].ti_Tag = MUIA_Frame;
    tags[n++].ti_Data = MUIV_Frame_Group;
    tags[n].ti_Tag = MUIA_FrameTitle;
    tags[n++].ti_Data = (ULONG)"Snapping";
    tags[n].ti_Tag = MUIA_Group_Columns;
    tags[n++].ti_Data = 2;

    for (i = 0; i < count; i++) {
        Object *widget;
        Object *label;

        if (t[i].kind == ES_SET_ZONES) {
            widget = es_zone_group(gui);
        } else {
            widget = es_widget(gui, &t[i], i);
        }
        gui->field[i] = widget;
        if (widget == NULL) {
            continue;
        }
        /*
         * A checkmark's label belongs on its RIGHT: that is the style
         * guide, and it is what the zone row above already does. Every
         * other kind keeps the label in the left column, where it lines
         * up with the rest.
         */
        if (t[i].kind == ES_SET_BOOL) {
            label = Label((char *)es_label(gui, i, t[i].label));
            tags[n].ti_Tag = MUIA_Group_Child;
            tags[n++].ti_Data = (ULONG)widget;
            if (label != NULL) {
                tags[n].ti_Tag = MUIA_Group_Child;
                tags[n++].ti_Data = (ULONG)label;
            }
            continue;
        }
        label = Label2((char *)es_label(gui, i, t[i].label));
        if (label != NULL) {
            tags[n].ti_Tag = MUIA_Group_Child;
            tags[n++].ti_Data = (ULONG)label;
        }
        tags[n].ti_Tag = MUIA_Group_Child;
        tags[n++].ti_Data = (ULONG)widget;
    }
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;
    return MUI_NewObjectA(MUIC_Group, tags);
}

/* ------------------------------------------------ back to the file */

static void es_collect(struct ESPrefsGui *gui)
{
    const ESSetting *t;
    int count = 0;
    int i;

    t = es_settings(&count);
    if (count > ES_MAX_SETTINGS) {
        count = ES_MAX_SETTINGS;
    }
    for (i = 0; i < count; i++) {
        ULONG value = 0;

        if (gui->field[i] == NULL) {
            continue;
        }
        switch (t[i].kind) {
        case ES_SET_BOOL:
            get(gui->field[i], MUIA_Selected, &value);
            es_setting_apply(&gui->cfg, i, value ? 1 : 0);
            break;
        case ES_SET_CHOICE:
            get(gui->field[i], MUIA_Cycle_Active, &value);
            es_setting_apply(&gui->cfg, i, (int)value);
            break;
        case ES_SET_ZONES:
            {
                unsigned mask = 0;
                int z;

                for (z = 0; z < ES_ZONE_COUNT; z++) {
                    ULONG on = 0;

                    if (gui->zone[z] == NULL) {
                        continue;
                    }
                    get(gui->zone[z], MUIA_Selected, &on);
                    if (on) {
                        mask |= ES_ZONEBIT(es_zone_order[z]);
                    }
                }
                es_setting_apply(&gui->cfg, i, (int)mask);
            }
            break;
        default:
            get(gui->field[i], MUIA_Numeric_Value, &value);
            /* Out of range is simply refused and the setting keeps
             * what it had: the slider's range and the parser's are the
             * same one, so this cannot normally happen. */
            es_setting_apply(&gui->cfg, i, (int)value);
            break;
        }
    }
}

/* ------------------------------------------------------------ main */

int main(void)
{
    struct ESPrefsGui gui;
    Object *save;
    Object *use;
    Object *cancel;
    ULONG signals = 0;
    ULONG id;
    int rc = RETURN_FAIL;
    int i;

    (void)es_version_cookie;
    for (i = 0; i < ES_MAX_SETTINGS; i++) {
        gui.field[i] = NULL;
    }
    for (i = 0; i < ES_ZONE_COUNT; i++) {
        gui.zone[i] = NULL;
    }
    esp_load(&gui.cfg);

    MUIMasterBase = OpenLibrary(MUIMASTER_NAME, MUIMASTER_VMIN);
    if (MUIMasterBase == NULL) {
        Printf("EdgeSnapPrefs: muimaster.library %ld is needed\n",
               (LONG)MUIMASTER_VMIN);
        return RETURN_FAIL;
    }

    save = SimpleButton("_Save");
    use = SimpleButton("_Use");
    cancel = SimpleButton("_Cancel");

    gui.app = ApplicationObject,
        MUIA_Application_Title,       "EdgeSnap",
        MUIA_Application_Version,     (ULONG)es_version_cookie,
        MUIA_Application_Copyright,   "(c) 2026 Michele Dipace",
        MUIA_Application_Author,      "Michele Dipace",
        MUIA_Application_Description, "Window snapping preferences",
        MUIA_Application_Base,        "EDGESNAPPREFS",
        SubWindow, gui.win = WindowObject,
            MUIA_Window_Title, "EdgeSnap Preferences",
            MUIA_Window_ID, MAKE_ID('E', 'S', 'P', 'R'),
            WindowContents, VGroup,
                Child, es_settings_group(&gui),
                Child, HGroup,
                    MUIA_Group_SameWidth, TRUE,
                    Child, save,
                    Child, use,
                    Child, cancel,
                End,
            End,
        End,
    End;

    if (gui.app == NULL) {
        Printf("EdgeSnapPrefs: the window would not build\n");
        CloseLibrary(MUIMasterBase);
        return RETURN_FAIL;
    }

    DoMethod(gui.win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)gui.app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    DoMethod(save, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)gui.app, 2, MUIM_Application_ReturnID, ES_ID_SAVE);
    DoMethod(use, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)gui.app, 2, MUIM_Application_ReturnID, ES_ID_USE);
    DoMethod(cancel, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)gui.app, 2, MUIM_Application_ReturnID, ES_ID_CANCEL);

    set(gui.win, MUIA_Window_Open, TRUE);

    while ((LONG)(id = DoMethod(gui.app, MUIM_Application_NewInput,
                               &signals)) !=
               (LONG)MUIV_Application_ReturnID_Quit) {
        if (id == ES_ID_SAVE) {
            es_collect(&gui);
            esp_store(&gui.cfg, 1);
            rc = RETURN_OK;
            break;
        }
        if (id == ES_ID_USE) {
            es_collect(&gui);
            esp_store(&gui.cfg, 0);
            rc = RETURN_OK;
            break;
        }
        if (id == ES_ID_CANCEL) {
            rc = RETURN_OK;
            break;
        }
        if (signals != 0) {
            signals = Wait(signals | SIGBREAKF_CTRL_C);
            if ((signals & SIGBREAKF_CTRL_C) != 0) {
                rc = RETURN_OK;
                break;
            }
        }
    }
    if ((LONG)id == (LONG)MUIV_Application_ReturnID_Quit) {
        rc = RETURN_OK;
    }

    set(gui.win, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(gui.app);
    CloseLibrary(MUIMasterBase);
    return rc;
}
