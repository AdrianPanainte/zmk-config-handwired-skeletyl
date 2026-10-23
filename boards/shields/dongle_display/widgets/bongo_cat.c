/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Bongo Cat that reacts to every key: a key on the left half slaps the left paw,
 * the right half the right paw, and the paw lifts on release. After a short pause
 * the cat rests its paws on the keyboard and blinks. Frames: tools/bongo_cat.py.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

#include "bongo_cat.h"

#define SRC(array) (const void **)array, sizeof(array) / sizeof(lv_img_dsc_t *)

// Rest the paws after this long without a key held
#define IDLE_DELAY_MS 1500
// Idle loop: each frame shows for IDLE_FRAME_MS, the last one is a blink
#define IDLE_FRAME_MS 300

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

LV_IMG_DECLARE(bongo_cat_rest);
LV_IMG_DECLARE(bongo_cat_rest_blink);
LV_IMG_DECLARE(bongo_cat_up);
LV_IMG_DECLARE(bongo_cat_left);
LV_IMG_DECLARE(bongo_cat_right);
LV_IMG_DECLARE(bongo_cat_both);

static const lv_img_dsc_t *idle_imgs[] = {
    &bongo_cat_rest, &bongo_cat_rest, &bongo_cat_rest, &bongo_cat_rest, &bongo_cat_rest,
    &bongo_cat_rest, &bongo_cat_rest, &bongo_cat_rest, &bongo_cat_rest, &bongo_cat_rest_blink,
};

// Skeletyl key positions: three 10-key rows (columns 0-4 are the left half), then
// six thumb keys (30-32 left, 33-35 right).
static bool position_is_left(uint32_t position) {
    return position < 30 ? (position % 10) < 5 : position < 33;
}

struct bongo_cat_state {
    uint8_t left_held;
    uint8_t right_held;
};

// Only touched from bongo_cat_get_state(), which ZMK calls under the listener's mutex.
// Counting here (not in the display callback) means coalesced updates can't lose a release.
static uint8_t left_held, right_held;

static struct bongo_cat_state bongo_cat_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev =
        eh != NULL ? as_zmk_position_state_changed(eh) : NULL;
    if (ev != NULL) {
        uint8_t *held = position_is_left(ev->position) ? &left_held : &right_held;
        if (ev->state) {
            (*held)++;
        } else if (*held > 0) {
            (*held)--;
        }
    }
    return (struct bongo_cat_state){.left_held = left_held, .right_held = right_held};
}

static lv_timer_t *idle_timer;

static void start_idle(lv_obj_t *obj) {
    lv_animimg_set_src(obj, SRC(idle_imgs));
    lv_animimg_set_duration(obj, IDLE_FRAME_MS * ARRAY_SIZE(idle_imgs));
    lv_animimg_set_repeat_count(obj, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(obj);
}

static void idle_timer_cb(lv_timer_t *timer) {
    struct zmk_widget_bongo_cat *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { start_idle(widget->obj); }
    lv_timer_pause(timer);
}

static void show_frame(lv_obj_t *obj, const lv_img_dsc_t *frame) {
    lv_anim_del(obj, NULL); // stop the idle loop, if running
    lv_img_set_src(obj, frame);
}

static void bongo_cat_update_cb(struct bongo_cat_state state) {
    const lv_img_dsc_t *frame;
    if (state.left_held && state.right_held) {
        frame = &bongo_cat_both;
    } else if (state.left_held) {
        frame = &bongo_cat_left;
    } else if (state.right_held) {
        frame = &bongo_cat_right;
    } else {
        frame = &bongo_cat_up;
    }

    struct zmk_widget_bongo_cat *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { show_frame(widget->obj, frame); }

    if (state.left_held || state.right_held) {
        lv_timer_pause(idle_timer);
    } else {
        lv_timer_reset(idle_timer);
        lv_timer_resume(idle_timer);
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_bongo_cat, struct bongo_cat_state, bongo_cat_update_cb,
                            bongo_cat_get_state)
ZMK_SUBSCRIPTION(widget_bongo_cat, zmk_position_state_changed);

int zmk_widget_bongo_cat_init(struct zmk_widget_bongo_cat *widget, lv_obj_t *parent) {
    widget->obj = lv_animimg_create(parent);
    lv_obj_center(widget->obj);

    if (idle_timer == NULL) {
        idle_timer = lv_timer_create(idle_timer_cb, IDLE_DELAY_MS, NULL);
    }

    sys_slist_append(&widgets, &widget->node);
    widget_bongo_cat_init();
    return 0;
}

lv_obj_t *zmk_widget_bongo_cat_obj(struct zmk_widget_bongo_cat *widget) { return widget->obj; }
