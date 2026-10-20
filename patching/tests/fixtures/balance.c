/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <string.h>
/* This fixture mocks native calls, not their 32-bit object layouts. */
#define _Static_assert(...)
#include "../../../payload/osos.h"
#include "../../../payload/balance.h"
#include "balance_ui.h"
#undef _Static_assert
#define CFW_PATCH_H
#define PATCH_ARM

static uint32_t disk_record;
static int missing = 1, writes, refreshed = -1, changes, eq_notified;

void cfw_eq_balance_changed(void) { eq_notified++; }

static int file_open(const struct osos_path *path, int mode, void **handle)
{
    assert(!strcmp(path->text, "iPod_Control\\Device\\CFWBal"));
    if (mode == 1 && missing)
        return -1;
    *handle = &disk_record;
    return 0;
}

static int file_read(void *handle, void *out, uint32_t size, uint32_t *count)
{
    assert(handle == &disk_record && size == 4);
    memcpy(out, &disk_record, 4);
    *count = 4;
    return 0;
}

static int file_write(void *handle, const void *record, uint32_t size, uint32_t *count)
{
    assert(handle == &disk_record && size == 4);
    memcpy(&disk_record, record, 4);
    missing = 0;
    writes++;
    *count = size;
    return 0;
}

static void file_close(void *handle) { assert(handle == &disk_record); }
static void item_changed(uint32_t item) { (void)item; changes++; }

#define osos_file_open file_open
#define osos_file_read file_read
#define osos_file_write file_write
#define osos_file_close file_close
#define osos_menu_item_changed item_changed

/* Generated UI tables, reduced to what the feature reads. */
const int32_t cfw_balance_values[21] = {-100, -90, -80, -70, -60, -50, -40, -30, -20, -10, 0,
                                        10,   20,  30,  40,  50,  60,  70,  80,  90,  100};
static const uint32_t items[21];
const struct cfw_ui_field cfw_balance_fields[1] = {
    {.count = 21, .default_value = 10, .items = items},
};

void cfw_ui_refresh(const struct cfw_ui_field *field, unsigned int selected)
{
    assert(field == &cfw_balance_fields[0]);
    refreshed = (int)selected;
}

void cfw_ui_status(const struct cfw_ui_action *action, unsigned int status)
{
    (void)action;
    (void)status;
}

/* Use the real selection parser; label rewriting is replaced by the stubs above. */
#define cfw_ui_refresh native_ui_refresh
#define cfw_ui_status native_ui_status
#include "../../../payload/ui.c"
#undef cfw_ui_refresh
#undef cfw_ui_status
#include "../../../payload/balance.c"

static void fill(int16_t *buffer, unsigned int frames, int16_t value)
{
    for (unsigned int i = 0; i < frames * 2; i++)
        buffer[i] = value;
}

int main(void)
{
    int16_t buffer[2048];

    /* Missing file: centered, audio untouched. */
    cfw_balance_load_preferences();
    fill(buffer, 512, 12345);
    cfw_balance_apply(buffer, 512);
    for (unsigned int i = 0; i < 1024; i++)
        assert(buffer[i] == 12345);

    /* Selecting "R 100%" mutes left after the ramp and keeps right intact. */
    assert(cfw_balance_action("CFW_Balance_Set_00_20", 0));
    assert(writes == 1 && refreshed == 20 && changes == 2 && eq_notified == 1);
    assert(cfw_balance_active());
    assert(disk_record == (0x4c414200u | 20));
    fill(buffer, 1024, -32768);
    cfw_balance_apply(buffer, 1024);
    assert(buffer[0] > -32768 && buffer[0] < 0); /* ramp, no hard step */
    assert(buffer[1] == -32768);
    assert(buffer[2046] == 0 && buffer[2047] == -32768);

    /* "L 50%" halves right; left ramps back to unity. */
    assert(cfw_balance_action("CFW_Balance_Set_00_05", 0));
    fill(buffer, 1024, 32767);
    cfw_balance_apply(buffer, 1024);
    assert(buffer[2046] == 32767 && buffer[2047] == 16383);

    /* Reload restores the saved choice; corrupt records fall back to center. */
    cfw_balance_load_preferences();
    assert(selected == 5 && target_right == (1 << 15) / 2);
    disk_record = 0x4c414200u | 99;
    cfw_balance_load_preferences();
    assert(selected == 10 && target_left == (1 << 15) && target_right == (1 << 15));
    assert(!cfw_balance_active());

    /* Unrelated actions pass through; support queries are claimed. */
    assert(!cfw_balance_action("CFW_EQ_Open", 0));
    assert(!cfw_balance_action("CFW_Balance_Set_00_21", 0));
    assert(cfw_balance_action("CFW_Balance_Open", OSOS_ACTION_SUPPORT_QUERY));
    assert(cfw_balance_action("CFW_Balance_Open", 0) && refreshed == 10);
    return 0;
}
