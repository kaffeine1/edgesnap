/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * EdgeSnap preferences for AmigaOS 4.x - a ReAction window.
 *
 * The window is not written out gadget by gadget: it is built by
 * walking es_settings(), the table in core/config.c that describes the
 * vocabulary. An integer becomes an integer gadget with its own range,
 * a boolean a checkbox, a choice a chooser, the zone mask a row of
 * checkboxes. Adding a setting to the table puts it in this window and
 * in the MorphOS one without either being touched.
 *
 * Save writes ENVARC: and ENV:, Use writes ENV: only, Cancel writes
 * nothing - the three buttons every Amiga preferences program has. The
 * running EdgeSnap watches ENV: and reconfigures itself, so there is
 * no protocol between this program and the commodity at all.
 *
 * One row is not EdgeSnap's: "Windows can move off-screen" is the
 * system's own off-screen dragging, the GUI editor's setting, kept in
 * the GUI preferences (prefs/offscreen_sys.c). It is here because a
 * drag only snaps where the pointer can reach the edge, and with that
 * setting off a dragged window stops at the edge and holds the pointer
 * back. It sits in a section named after the system, and IPrefs takes
 * it up at once. From a Shell and from the installer:
 *   EdgeSnapPrefs OFFSCREEN QUERY|ON|OFF
 */

/*
 * Call the system by name - NewObject(), Open() - instead of through
 * an interface pointer at every site. This has to be defined before
 * ANY header: reaction_macros.h chooses between two sets of macros on
 * it, and getting the answer after those macros are defined expands
 * IIntuition->IIntuition->NewObject().
 */
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif

#include <exec/types.h>
#include <exec/ports.h>
#include <libraries/commodities.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/checkbox.h>
#include <gadgets/integer.h>
#include <gadgets/chooser.h>
#include <gadgets/string.h>
#include <gadgets/clicktab.h>
#include <images/label.h>
#include <reaction/reaction_macros.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/checkbox.h>
#include <proto/integer.h>
#include <proto/chooser.h>
#include <proto/string.h>
#include <proto/clicktab.h>
#include <proto/commodities.h>
#include <proto/label.h>

#include "prefs_io.h"
#include "offscreen_sys.h"
#include "zones.h"
#include "edgesnap_version.h"

/* ReAction's call chains are deep and the preferences file is read
 * into a buffer of its own: ask for room rather than find out. */
static const char es_stack_cookie[] __attribute__((used)) =
    "$STACK:65536";
static const char es_version_cookie[] __attribute__((used)) =
    "$VER: EdgeSnapPrefs " ES_VERSION " (" ES_VERSION_DATE ") Michele Dipace";

#define ES_MAX_SETTINGS 48
#define ES_ZONE_COUNT   7

#define GID_SAVE    1
#define GID_USE     2
#define GID_CANCEL  3
#define GID_PAGES   5
#define GID_OFFSCREEN 4          /* the system's setting, not ours    */
#define GID_SETTING 100          /* + index of the setting            */
#define GID_ZONE    200          /* + zone number                     */

struct ESPrefsGui {
    Object *win;
    struct Window *window;
    Object *offscreen;                   /* the system's setting      */
    int off_was;                         /* as found: 1, 0, or -1 for */
                                         /* a file we cannot read     */
    Object *field[ES_MAX_SETTINGS];      /* one gadget per setting    */
    Object *zone[ES_ZONE_COUNT + 1];     /* checkboxes for the mask   */
    ESConfig cfg;
    ESHotkeys keys;
    Object *pages;
    Object *tabs;
    struct List tab_labels;
    int hotkey_page;
    /* label.image keeps the pointer we give it rather than copying the
     * text, so these have to outlive the call that builds the window.
     * The table's labels carry no colon: they also feed the AmigaGuide
     * manual, where one would read badly. The window adds it. */
    char label[ES_MAX_SETTINGS][40];
};

/*
 * The proto/ headers declare these for us; here are the definitions.
 * They cannot be static: the inline stubs in those headers refer to
 * them by name. IExec and IDOS come from the startup code, but
 * Intuition is ours to open.
 */
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;

struct Library *WindowBase, *LayoutBase, *ButtonBase, *CheckBoxBase;
struct Library *IntegerBase, *ChooserBase, *LabelBase;
struct Library *StringBase, *ClickTabBase, *CxBase;
struct CommoditiesIFace *ICommodities;
struct StringIFace *IString;
struct ClickTabIFace *IClickTab;
struct WindowIFace *IWindow;
struct LayoutIFace *ILayout;
struct ButtonIFace *IButton;
struct CheckBoxIFace *ICheckBox;
struct IntegerIFace *IInteger;
struct ChooserIFace *IChooser;
struct LabelIFace *ILabel;

static const int es_zone_order[ES_ZONE_COUNT] = {
    ES_ZONE_LEFT, ES_ZONE_RIGHT, ES_ZONE_TOP_LEFT, ES_ZONE_TOP_RIGHT,
    ES_ZONE_BOTTOM_LEFT, ES_ZONE_BOTTOM_RIGHT, ES_ZONE_MAX
};

static const char *const es_zone_label[ES_ZONE_COUNT] = {
    "Left", "Right", "Top left", "Top right",
    "Bottom left", "Bottom right", "Maximize"
};

/* ----------------------------------------------------------- classes */

static struct Library *es_open_class(const char *name, const char *iface,
                                     APTR *ifptr)
{
    struct Library *base = OpenLibrary((STRPTR)name, 51);

    if (base != NULL) {
        *ifptr = GetInterface(base, (STRPTR)iface, 1, NULL);
        if (*ifptr == NULL) {
            CloseLibrary(base);
            base = NULL;
        }
    }
    return base;
}

static void es_close_class(struct Library *base, APTR iface)
{
    if (iface != NULL) {
        DropInterface((struct Interface *)iface);
    }
    if (base != NULL) {
        CloseLibrary(base);
    }
}

static int es_open_classes(void)
{
    IntuitionBase = OpenLibrary("intuition.library", 51);
    if (IntuitionBase == NULL) {
        return 0;
    }
    IIntuition = (struct IntuitionIFace *)
        GetInterface(IntuitionBase, "main", 1, NULL);
    if (IIntuition == NULL) {
        return 0;
    }
    CxBase = OpenLibrary("commodities.library", 37);
    if (CxBase != NULL) {
        ICommodities = (struct CommoditiesIFace *)
            GetInterface(CxBase, "main", 1, NULL);
    }
    StringBase = es_open_class("gadgets/string.gadget", "main",
                               (APTR *)&IString);
    ClickTabBase = es_open_class("gadgets/clicktab.gadget", "main",
                                 (APTR *)&IClickTab);
    WindowBase = es_open_class("window.class", "main", (APTR *)&IWindow);
    LayoutBase = es_open_class("gadgets/layout.gadget", "main",
                               (APTR *)&ILayout);
    ButtonBase = es_open_class("gadgets/button.gadget", "main",
                               (APTR *)&IButton);
    CheckBoxBase = es_open_class("gadgets/checkbox.gadget", "main",
                                 (APTR *)&ICheckBox);
    IntegerBase = es_open_class("gadgets/integer.gadget", "main",
                                (APTR *)&IInteger);
    ChooserBase = es_open_class("gadgets/chooser.gadget", "main",
                                (APTR *)&IChooser);
    LabelBase = es_open_class("images/label.image", "main",
                              (APTR *)&ILabel);
    return WindowBase && LayoutBase && ButtonBase && CheckBoxBase &&
           IntegerBase && ChooserBase && LabelBase && StringBase &&
           ClickTabBase && ICommodities;
}

static void es_close_classes(void)
{
    es_close_class(ClickTabBase, IClickTab);
    es_close_class(StringBase, IString);
    es_close_class(CxBase, ICommodities);
    es_close_class(LabelBase, ILabel);
    es_close_class(ChooserBase, IChooser);
    es_close_class(IntegerBase, IInteger);
    es_close_class(CheckBoxBase, ICheckBox);
    es_close_class(ButtonBase, IButton);
    es_close_class(LayoutBase, ILayout);
    es_close_class(WindowBase, IWindow);
    if (IIntuition != NULL) {
        DropInterface((struct Interface *)IIntuition);
        IIntuition = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary(IntuitionBase);
        IntuitionBase = NULL;
    }
}

/* -------------------------------------------------------- the widgets */

static void es_add_child(Object *layout, Object *child, const char *label,
                         int fill);

/* Same group as the section already open? The table keeps a section's
 * settings together, so a change of string is a change of section. */
static int es_same_group(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

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

/*
 * The zone mask gets checkboxes rather than a cryptic number, laid out
 * three to a row. As one long row they set a minimum width the window
 * could never go below, which is what stopped the MorphOS window from
 * being resized; the same layout was here.
 */
#define ES_ZONES_PER_ROW 3

static Object *es_zone_group(struct ESPrefsGui *gui)
{
    Object *group;
    Object *row = NULL;
    int i;

    group = VLayoutObject,
        LAYOUT_SpaceOuter, FALSE,
    End;
    if (group == NULL) {
        return NULL;
    }
    for (i = 0; i < ES_ZONE_COUNT; i++) {
        int zone = es_zone_order[i];
        int on = (gui->cfg.engine.zones_mask & ES_ZONEBIT(zone)) != 0;

        if ((i % ES_ZONES_PER_ROW) == 0) {
            /*
             * EvenSize gives the row three cells of equal width. Without
             * it each row divides the space by its own labels, so "Left,
             * Right, Top left" and "Top right, Bottom left, Bottom
             * right" put their checkboxes at different places and the
             * columns visibly drift.
             */
            row = HLayoutObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_EvenSize, TRUE,
            End;
            if (row == NULL) {
                return group;
            }
            es_add_child(group, row, NULL, 0);
        }
        gui->zone[i] = CheckBoxObject,
            GA_ID, GID_ZONE + zone,
            GA_RelVerify, TRUE,
            GA_Text, es_zone_label[i],
            GA_Selected, on ? TRUE : FALSE,
        End;
        es_add_child(row, gui->zone[i], NULL, 1);
    }
    /*
     * Seven zones do not fill the last row of three, and a row of one
     * would stretch that one cell across the whole width, pulling its
     * checkbox out of column. Empty layouts hold the missing cells.
     */
    for (i = ES_ZONE_COUNT; (i % ES_ZONES_PER_ROW) != 0; i++) {
        Object *pad = HLayoutObject,
            LAYOUT_SpaceOuter, FALSE,
        End;

        if (pad == NULL) {
            break;
        }
        es_add_child(row, pad, NULL, 1);
    }
    return group;
}

/*
 * LM_ADDCHILD takes its tags as a TagItem ARRAY, not as loose varargs:
 * the method's message is {MethodID, window, object, tags}, so tags
 * written inline put CHILD_Label's tag id where the tag POINTER
 * belongs, and the layout class follows it. That is the DSI this
 * program died with on its first run.
 */
/* fill: 0 fixed height, 1 free, 2 fixed in both directions. */
static void es_add_child(Object *layout, Object *child, const char *label,
                         int fill)
{
    struct TagItem tags[4];
    Object *lab = NULL;
    int n = 0;

    if (layout == NULL || child == NULL) {
        return;
    }
    if (label != NULL) {
        lab = LabelObject,
            LABEL_Text, label,
        LabelEnd;
        if (lab != NULL) {
            tags[n].ti_Tag = CHILD_Label;
            tags[n++].ti_Data = (ULONG)lab;
        }
    }
    if (fill != 1) {
        tags[n].ti_Tag = CHILD_WeightedHeight;
        tags[n++].ti_Data = 0;
    }
    if (fill == 2) {
        /* a checkbox is its own size: do not stretch it across the
         * column, or it stops lining up with the sliders below */
        tags[n].ti_Tag = CHILD_WeightedWidth;
        tags[n++].ti_Data = 0;
    }
    tags[n].ti_Tag = TAG_END;
    tags[n].ti_Data = 0;
    IDoMethod(layout, LM_ADDCHILD, NULL, child, tags);
}

static Object *es_widget(struct ESPrefsGui *gui, const ESSetting *s,
                         int index)
{
    int value = es_setting_value(&gui->cfg, index);

    switch (s->kind) {
    case ES_SET_BOOL:
        return CheckBoxObject,
            GA_ID, GID_SETTING + index,
            GA_RelVerify, TRUE,
            GA_Text, "",
            GA_Selected, value ? TRUE : FALSE,
        End;
    case ES_SET_CHOICE:
        return ChooserObject,
            GA_ID, GID_SETTING + index,
            GA_RelVerify, TRUE,
            CHOOSER_LabelArray, s->choices,
            CHOOSER_Selected, value,
        End;
    case ES_SET_KEY:
        return StringObject,
            GA_ID, GID_SETTING + index,
            GA_RelVerify, TRUE,
            GA_TabCycle, TRUE,
            STRINGA_TextVal, es_setting_text(&gui->keys, index),
            STRINGA_MaxChars, ES_HOTKEY_SIZE,
        End;
    case ES_SET_ZONES:
        return es_zone_group(gui);
    default:
        return IntegerObject,
            GA_ID, GID_SETTING + index,
            GA_RelVerify, TRUE,
            GA_TabCycle, TRUE,
            INTEGER_Number, value,
            INTEGER_Minimum, s->min,
            INTEGER_Maximum, s->max,
            INTEGER_MaxChars, 6,
        End;
    }
}

/* A page contains its contiguous run of groups from the table. */
static Object *es_settings_page(struct ESPrefsGui *gui, const ESSetting *t,
                                 int first, int last)
{
    Object *rows = VLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
    End;
    Object *sect = NULL;
    const char *open = NULL;
    int i;

    if (rows == NULL) {
        return NULL;
    }
    for (i = first; i <= last; i++) {
        if (open == NULL || !es_same_group(t[i].group, open)) {
            sect = VLayoutObject,
                LAYOUT_SpaceOuter, TRUE,
                LAYOUT_BevelStyle, BVS_GROUP,
                LAYOUT_Label, t[i].group,
            End;
            if (sect == NULL) {
                DisposeObject(rows);
                return NULL;
            }
            es_add_child(rows, sect, NULL, 0);
            open = t[i].group;
        }
        gui->field[i] = es_widget(gui, &t[i], i);
        if (gui->field[i] == NULL) {
            DisposeObject(rows);
            return NULL;
        }
        es_add_child(sect, gui->field[i], es_label(gui, i, t[i].label),
                     t[i].kind == ES_SET_BOOL ? 2 : 0);
    }
    /* The system's setting follows the first page's last group. */
    if (first == 0) {
        sect = VLayoutObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_BevelStyle, BVS_GROUP,
            LAYOUT_Label, eso_system(),
        End;
        gui->offscreen = CheckBoxObject,
            GA_ID, GID_OFFSCREEN,
            GA_RelVerify, TRUE,
            GA_Text, "",
            GA_Selected, gui->off_was == 1 ? TRUE : FALSE,
            GA_Disabled, gui->off_was < 0 ? TRUE : FALSE,
        End;
        if (sect == NULL || gui->offscreen == NULL) {
            if (sect != NULL) { DisposeObject(sect); }
            if (gui->offscreen != NULL) { DisposeObject(gui->offscreen); }
            DisposeObject(rows);
            return NULL;
        }
        es_add_child(rows, sect, NULL, 0);
        es_add_child(sect, gui->offscreen, "Windows can move off-screen:", 2);
    }
    /*
     * Empty space last, taking what is left over: the pages share the
     * height of the tallest, and on a shorter one the layout spread its
     * sections apart instead (AmigaOS 4 VM, 2026-10-20).
     */
    {
        Object *slack = VLayoutObject,
            LAYOUT_SpaceOuter, FALSE,
        End;

        es_add_child(rows, slack, NULL, 1);
    }
    return rows;
}

static Object *es_settings_pages(struct ESPrefsGui *gui)
{
    struct TagItem tags[ES_MAX_SETTINGS + 2];
    const ESSetting *t;
    int count, first = 0, pages = 0, n = 0, i;

    t = es_settings(&count);
    if (count > ES_MAX_SETTINGS) {
        return NULL;
    }
    tags[n].ti_Tag = PAGE_Current;
    tags[n++].ti_Data = 0;
    /*
     * Layout and rendering on a change of page are deferred to our
     * task, as in the SDK's Examples/GUI/ClickTab/PageExample.c: without
     * it the clicktab took the click and showed the new tab selected,
     * but the old page stayed drawn (AmigaOS 4 VM, 2026-10-20).
     */
    tags[n].ti_Tag = LAYOUT_DeferLayout;
    tags[n++].ti_Data = TRUE;
    for (i = 1; i <= count; i++) {
        Object *body;
        struct Node *label;

        if (i < count && es_same_group(t[i].page, t[first].page)) {
            continue;
        }
        body = es_settings_page(gui, t, first, i - 1);
        if (body == NULL) {
            break;
        }
        /* Each tab needs its own number: without one they were all 0,
         * and every click showed the first page (clicktab_gc.doc). */
        label = AllocClickTabNode(TNA_Text, (ULONG)t[first].page,
                                  TNA_Number, (ULONG)pages, TAG_DONE);
        if (label == NULL) {
            DisposeObject(body);
            break;
        }
        AddTail(&gui->tab_labels, label);
        if (es_same_group(t[first].page, "Hotkeys")) {
            gui->hotkey_page = pages;
        }
        pages++;
        tags[n].ti_Tag = PAGE_Add;
        tags[n++].ti_Data = (ULONG)body;
        first = i;
    }
    if (i <= count) {
        while (--n > 0) {
            DisposeObject((Object *)tags[n].ti_Data);
        }
        return NULL;
    }
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;
    gui->pages = NewObjectA(PAGE_GetClass(), NULL, tags);
    if (gui->pages == NULL) {
        while (--n > 0) {
            DisposeObject((Object *)tags[n].ti_Data);
        }
        return NULL;
    }
    gui->tabs = ClickTabObject,
        GA_ID, GID_PAGES,
        GA_RelVerify, TRUE,
        CLICKTAB_Labels, &gui->tab_labels,
        CLICKTAB_Current, 0,
        CLICKTAB_PageGroup, gui->pages,
    End;
    if (gui->tabs == NULL) {
        DisposeObject(gui->pages);
        return NULL;
    }
    /*
     * The page group is the clicktab's own child: CLICKTAB_PageGroup
     * embeds it, and the clicktab draws the pages inside its frame
     * (the SDK's Examples/GUI/ClickTab/PageExample.c). Only the
     * clicktab goes into the window's layout; added there as well, the
     * pages would have two parents.
     */
    return gui->tabs;
}

static Object *es_build_window(struct ESPrefsGui *gui)
{
    Object *rows = es_settings_pages(gui);
    Object *buttons;

    if (rows == NULL) {
        return NULL;
    }

    buttons = HLayoutObject,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_EvenSize, TRUE,
        LAYOUT_AddChild, ButtonObject,
            GA_ID, GID_SAVE,
            GA_RelVerify, TRUE,
            GA_Text, "_Save",
        End,
        LAYOUT_AddChild, ButtonObject,
            GA_ID, GID_USE,
            GA_RelVerify, TRUE,
            GA_Text, "_Use",
        End,
        LAYOUT_AddChild, ButtonObject,
            GA_ID, GID_CANCEL,
            GA_RelVerify, TRUE,
            GA_Text, "_Cancel",
        End,
    End;

    return WindowObject,
        /*
         * window.class works out most of the IDCMP for itself, but the
         * evidence said otherwise here: the close gadget and window
         * activation arrived while nothing a button did ever did. Ask
         * for the gadget classes by name.
         */
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN |
                  IDCMP_REFRESHWINDOW | IDCMP_NEWSIZE | IDCMP_VANILLAKEY |
                  IDCMP_IDCMPUPDATE,
        WA_Title, "EdgeSnap Preferences",
        WA_ScreenTitle, "EdgeSnap - by Michele Dipace",
        WA_Activate, TRUE,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_Layout, VLayoutObject,
            LAYOUT_DeferLayout, TRUE,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_AddChild, rows,
            LAYOUT_AddChild, buttons,
            CHILD_WeightedHeight, 0,
        End,
    End;
}

/* ------------------------------------------------------ reading it back */

static void es_key_error(struct ESPrefsGui *gui, int bad, int duplicate,
                          const char *text)
{
    const ESSetting *t = es_settings(NULL);
    struct EasyStruct es;
    ULONG args[3];

    SetGadgetAttrs((struct Gadget *)gui->tabs, gui->window, NULL,
                   CLICKTAB_Current, gui->hotkey_page, TAG_DONE);
    SetGadgetAttrs((struct Gadget *)gui->pages, gui->window, NULL,
                   PAGE_Current, gui->hotkey_page, TAG_DONE);
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (STRPTR)"EdgeSnap Preferences";
    es.es_GadgetFormat = (STRPTR)"OK";
    args[0] = (ULONG)t[bad].label;
    args[1] = (ULONG)text;
    if (duplicate >= 0) {
        args[2] = (ULONG)t[duplicate].label;
        es.es_TextFormat = (STRPTR)"The hotkey for %s:\n%s\n\nMatches %s. "
                                  "Choose another key or leave it empty.";
    } else {
        es.es_TextFormat = (STRPTR)"Invalid hotkey for %s:\n%s\n\nUse an input "
                                  "description as Exchange writes it, or "
                                  "leave it empty.";
    }
    EasyRequestArgs(gui->window, &es, NULL, args);
    ActivateGadget((struct Gadget *)gui->field[bad], gui->window, NULL);
}

static int es_collect(struct ESPrefsGui *gui)
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
            GetAttr(GA_Selected, gui->field[i], &value);
            es_setting_apply(&gui->cfg, i, value ? 1 : 0);
            break;
        case ES_SET_CHOICE:
            GetAttr(CHOOSER_Selected, gui->field[i], &value);
            es_setting_apply(&gui->cfg, i, (int)value);
            break;
        case ES_SET_KEY:
            {
                ULONG text = 0;

                GetAttr(STRINGA_TextVal, gui->field[i], &text);
                if (text == 0 ||
                    es_setting_apply_text(&gui->keys, i, (const char *)text)
                        != ES_OK) {
                    es_key_error(gui, i, -1, text != 0 ?
                                  (const char *)text : "");
                    return 0;
                }
            }
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
                    GetAttr(GA_Selected, gui->zone[z], &on);
                    if (on) {
                        mask |= ES_ZONEBIT(es_zone_order[z]);
                    }
                }
                es_setting_apply(&gui->cfg, i, (int)mask);
            }
            break;
        default:
            GetAttr(INTEGER_Number, gui->field[i], &value);
            /* Out of range is simply refused, and the gadget keeps
             * whatever it had: the table's range and the parser's are
             * the same one. */
            es_setting_apply(&gui->cfg, i, (int)value);
            break;
        }
    }
    {
        int bad, duplicate;

        if (!esp_validate_hotkeys(&gui->keys, &bad, &duplicate)) {
            es_key_error(gui, bad, duplicate, es_setting_text(&gui->keys, bad));
            return 0;
        }
    }
    return 1;
}

/*
 * The system's setting as the box shows it: Use for this session, Save
 * for the next start too, even when only an earlier Use had changed it.
 * IPrefs takes it up at once, and a file that already says so is not
 * written at all.
 */
static void es_store_offscreen(struct ESPrefsGui *gui, int permanent)
{
    struct EasyStruct es;
    ULONG on = 0;

    if (gui->offscreen == NULL || gui->off_was < 0) {
        return;
    }
    GetAttr(GA_Selected, gui->offscreen, &on);
    if (!eso_set(on ? 1 : 0, 1, permanent)) {
        es.es_StructSize = sizeof(es);
        es.es_Flags = 0;
        es.es_Title = (STRPTR)"EdgeSnap Preferences";
        es.es_TextFormat = (STRPTR)"\"Windows can move off-screen\" could not "
                           "be written:\nthe system's GUI preferences file "
                           "refused it.";
        es.es_GadgetFormat = (STRPTR)"OK";
        EasyRequestArgs(gui->window, &es, NULL, NULL);
    }
}

/* A public port makes finding and signalling an existing instance one
 * indivisible operation. It is published before window construction, so
 * a second launch during startup is remembered by the port's signal. */
static struct MsgPort *es_instance_open(int *already)
{
    struct MsgPort *port = CreateMsgPort();
    struct MsgPort *other;

    *already = 0;
    if (port == NULL) {
        return NULL;
    }
    Forbid();
    other = FindPort((STRPTR)"EdgeSnap.Preferences");
    if (other != NULL) {
        Signal(other->mp_SigTask, 1UL << other->mp_SigBit);
        *already = 1;
    } else {
        port->mp_Node.ln_Name = (STRPTR)"EdgeSnap.Preferences";
        AddPort(port);
    }
    Permit();
    if (*already) {
        DeleteMsgPort(port);
        return NULL;
    }
    return port;
}

static void es_instance_close(struct MsgPort *port)
{
    Forbid();
    RemPort(port);
    Permit();
    DeleteMsgPort(port);
}

static void es_free_tabs(struct ESPrefsGui *gui)
{
    struct Node *node;

    while ((node = RemHead(&gui->tab_labels)) != NULL) {
        FreeClickTabNode(node);
    }
}

/* ---------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    struct ESPrefsGui gui;
    ULONG signals, result;
    struct MsgPort *instance;
    ULONG instance_mask;
    int already;
    UWORD code;
    int running = 1;
    int rc = RETURN_FAIL;
    int i;

    (void)es_version_cookie;
    (void)es_stack_cookie;
    if (eso_is_command(argc, argv)) {
        return eso_command(argc, argv);
    }
    instance = es_instance_open(&already);
    if (instance == NULL) {
        return already ? RETURN_OK : RETURN_FAIL;
    }
    instance_mask = 1UL << instance->mp_SigBit;
    NewList(&gui.tab_labels);
    gui.offscreen = NULL;
    gui.off_was = eso_query(0);
    if (gui.off_was >= 0 && eso_query(1) < 0) {
        gui.off_was = -1;      /* one of the two files is not ours to read */
    }
    for (i = 0; i < ES_MAX_SETTINGS; i++) {
        gui.field[i] = NULL;
    }
    for (i = 0; i <= ES_ZONE_COUNT; i++) {
        gui.zone[i] = NULL;
    }
    esp_load(&gui.cfg, &gui.keys);

    if (!es_open_classes()) {
        es_instance_close(instance);
        es_close_classes();
        return RETURN_FAIL;
    }
    gui.win = es_build_window(&gui);
    if (gui.win == NULL) {
        es_free_tabs(&gui);
        es_instance_close(instance);
        es_close_classes();
        return RETURN_FAIL;
    }
    gui.window = (struct Window *)IDoMethod(gui.win, WM_OPEN, NULL);
    if (gui.window == NULL) {
        DisposeObject(gui.win);
        es_free_tabs(&gui);
        es_instance_close(instance);
        es_close_classes();
        return RETURN_FAIL;
    }

    while (running) {
        ULONG wake;

        GetAttr(WINDOW_SigMask, gui.win, &signals);
        wake = Wait(signals | instance_mask | SIGBREAKF_CTRL_C);
        if (wake & instance_mask) {
            ScreenToFront(gui.window->WScreen);
            WindowToFront(gui.window);
            ActivateWindow(gui.window);
        }
        if (wake & SIGBREAKF_CTRL_C) {
            running = 0;
            rc = RETURN_OK;
            break;
        }

        while ((result = IDoMethod(gui.win, WM_HANDLEINPUT,
                                               &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                running = 0;
                rc = RETURN_OK;
                break;
            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                case GID_SAVE:
                case GID_USE:
                    if (es_collect(&gui)) {
                        int permanent = (result & WMHI_GADGETMASK) == GID_SAVE;

                        if (esp_store(&gui.cfg, &gui.keys, permanent)) {
                            es_store_offscreen(&gui, permanent);
                            running = 0;
                            rc = RETURN_OK;
                        } else {
                            struct EasyStruct es;

                            es.es_StructSize = sizeof(es);
                            es.es_Flags = 0;
                            es.es_Title = (STRPTR)"EdgeSnap Preferences";
                            es.es_TextFormat = (STRPTR)
                                "The preferences could not be written.";
                            es.es_GadgetFormat = (STRPTR)"OK";
                            EasyRequestArgs(gui.window, &es, NULL, NULL);
                        }
                    }
                    break;
                case GID_CANCEL:
                    running = 0;
                    rc = RETURN_OK;
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
        }
    }

    IDoMethod(gui.win, WM_CLOSE, NULL);
    es_instance_close(instance);
    DisposeObject(gui.win);
    es_free_tabs(&gui);
    es_close_classes();
    return rc;
}
