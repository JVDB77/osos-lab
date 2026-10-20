/* SPDX-License-Identifier: GPL-3.0-only */

#include "balance.h"
#include "balance_ui.h"
#include "osos.h"
#include "cfw.h"
#include "preferences.h"
#include "patch.h"

enum {
    BALANCE_RECORD_MAGIC = 0x4c414200, /* "\0BAL" + choice index in the low byte */
    UNITY = 1 << 15,                   /* Q15 gain of 1.0 */
    RAMP_STEP = UNITY / 256,           /* full sweep in 256 frames (~6 ms) */
};

static unsigned int selected;
static int initialized;
static const struct osos_path settings_path = {0, "iPod_Control\\Device\\CFWBal"};

/* Written from the UI, read by the audio path; 32-bit stores are atomic here. */
static volatile int32_t target_left = UNITY, target_right = UNITY;
static int32_t gain_left = UNITY, gain_right = UNITY;

static void publish(unsigned int index)
{
    int32_t percent = cfw_balance_values[index];
    /* Linear attenuation of the opposite channel, like Rockbox's balance. */
    target_left = percent > 0 ? UNITY * (100 - percent) / 100 : UNITY;
    target_right = percent < 0 ? UNITY * (100 + percent) / 100 : UNITY;
}

static void defaults(void)
{
    selected = cfw_balance_fields[CFW_BALANCE_LEVEL].default_value;
    publish(selected);
    initialized = 1;
}

void cfw_balance_load_preferences(void)
{
    uint32_t record;
    defaults();
    if (!cfw_preferences_read(&settings_path, &record, sizeof(record)) ||
        (record & ~0xffu) != BALANCE_RECORD_MAGIC ||
        (record & 0xffu) >= cfw_balance_fields[CFW_BALANCE_LEVEL].count)
        return;
    selected = record & 0xffu;
    publish(selected);
}

static void save(void)
{
    uint32_t record = BALANCE_RECORD_MAGIC | selected;
    cfw_preferences_write(&settings_path, &record, sizeof(record));
}

int cfw_balance_action(const char *action, uint32_t argument)
{
    unsigned int field, value;
    int open = cfw_string_equal(action, "CFW_Balance_Open");
    int chosen = cfw_ui_selection("CFW_Balance_Set_", action, cfw_balance_fields,
                                  CFW_BALANCE_FIELDS, &field, &value);
    if (!open && !chosen)
        return 0;
    if (argument == OSOS_ACTION_SUPPORT_QUERY)
        return 1;
    if (!initialized)
        defaults();
    if (chosen) {
        unsigned int previous = selected;
        selected = value;
        publish(selected);
        save();
        osos_menu_item_changed(cfw_balance_fields[field].items[previous]);
        osos_menu_item_changed(cfw_balance_fields[field].items[value]);
    }
    cfw_ui_refresh(&cfw_balance_fields[CFW_BALANCE_LEVEL], selected);
    return 1;
}

static inline int32_t approach(int32_t current, int32_t target)
{
    if (current < target)
        return current + RAMP_STEP < target ? current + RAMP_STEP : target;
    if (current > target)
        return current - RAMP_STEP > target ? current - RAMP_STEP : target;
    return current;
}

void cfw_balance_apply(int16_t *samples, uint32_t frames)
{
    int32_t left = target_left, right = target_right;
    if (gain_left == UNITY && gain_right == UNITY && left == UNITY && right == UNITY)
        return;
    for (uint32_t i = 0; i < frames; i++) {
        gain_left = approach(gain_left, left);
        gain_right = approach(gain_right, right);
        /* Gains never exceed unity, so the products cannot overflow int16. */
        samples[i * 2] = (int16_t)(((int32_t)samples[i * 2] * gain_left) >> 15);
        samples[i * 2 + 1] = (int16_t)(((int32_t)samples[i * 2 + 1] * gain_right) >> 15);
    }
}
