#include <libdragon.h>
#include <stdio.h>

#include "mission_select.h"

// Background colour matches the lobby — keep the two screens visually
// consistent.
#define BG_R 8
#define BG_G 12
#define BG_B 24

// Card layout: vertical stack of mission cards centered horizontally.
// Heights/gaps are tuned so 7 cards fit between the title (~y=44) and the
// blurb line at y=200 without overlap. If MISSION_COUNT changes, retune
// CARD_H / CARD_GAP — see CLAUDE.md "UI text spacing".
#define CARD_X        20
#define CARD_Y        50
#define CARD_W        280
#define CARD_H        18
#define CARD_GAP      2

// Stick Y deadzone for navigation. Edge-detected against prev_y so a
// held stick is one step.
#define NAV_THRESHOLD 60

static const joypad_port_t MS_PORTS[4] = {
    JOYPAD_PORT_1, JOYPAD_PORT_2, JOYPAD_PORT_3, JOYPAD_PORT_4
};

static bool pressed(bool cur, bool prev) { return cur && !prev; }

MissionId mission_select_run(int font_id)
{
    int selected = MIS_INSTANT_2F;   // default to the 2-fighter encounter
    bool prev_a = false;
    bool prev_y_up = false;
    bool prev_y_down = false;

    for (;;) {
        joypad_poll();

        // OR inputs across all connected controllers so any player who
        // readied in the lobby can drive this menu — a P3-only setup
        // (single controller in port 3) was previously locked out.
        bool a_now = false, y_up_now = false, y_down_now = false;
        for (int i = 0; i < 4; i++) {
            if (!joypad_is_connected(MS_PORTS[i])) continue;
            joypad_inputs_t in = joypad_get_inputs(MS_PORTS[i]);
            if (in.btn.a) a_now = true;
            int sy = in.stick_y;
            if (sy >  NAV_THRESHOLD || in.btn.d_up)   y_up_now   = true;
            if (sy < -NAV_THRESHOLD || in.btn.d_down) y_down_now = true;
        }

        bool a_press      = pressed(a_now,      prev_a);
        bool y_up_press   = pressed(y_up_now,   prev_y_up);
        bool y_down_press = pressed(y_down_now, prev_y_down);
        prev_a      = a_now;
        prev_y_up   = y_up_now;
        prev_y_down = y_down_now;

        if (y_up_press)   selected = (selected + MISSION_COUNT - 1) % MISSION_COUNT;
        if (y_down_press) selected = (selected + 1) % MISSION_COUNT;

        if (a_press) {
            return (MissionId)selected;
        }

        // ---- Draw ----
        rdpq_attach(display_get(), display_get_zbuf());
        rdpq_set_mode_fill(RGBA32(BG_R, BG_G, BG_B, 0xFF));
        rdpq_fill_rectangle(0, 0, 320, 240);

        rdpq_set_mode_standard();
        rdpq_text_print(NULL, font_id, 90, 24, "STAR CREW 64");
        rdpq_text_print(NULL, font_id, 84, 38, "MISSION SELECT");

        const MissionDef *table = missions_table();
        for (int i = 0; i < MISSION_COUNT; i++) {
            int y = CARD_Y + i * (CARD_H + CARD_GAP);
            uint8_t r, g, b;
            if (i == selected) { r = 60; g = 90;  b = 140; }
            else                { r = 28; g = 32;  b = 50;  }
            rdpq_set_mode_fill(RGBA32(r, g, b, 0xFF));
            rdpq_fill_rectangle(CARD_X, y, CARD_X + CARD_W, y + CARD_H);

            // 1-px highlight outline on the selected card.
            if (i == selected) {
                rdpq_set_mode_fill(RGBA32(180, 220, 255, 0xFF));
                rdpq_fill_rectangle(CARD_X,           y,            CARD_X + CARD_W, y + 1);
                rdpq_fill_rectangle(CARD_X,           y + CARD_H-1, CARD_X + CARD_W, y + CARD_H);
                rdpq_fill_rectangle(CARD_X,           y,            CARD_X + 1,      y + CARD_H);
                rdpq_fill_rectangle(CARD_X + CARD_W-1,y,            CARD_X + CARD_W, y + CARD_H);
            }
            rdpq_set_mode_standard();
            rdpq_text_print(NULL, font_id, CARD_X + 8,  y + 8,  table[i].title);
        }

        // Footer: blurb of the selected mission + "[A] LAUNCH" prompt.
        const MissionDef *cur = &table[selected];
        rdpq_text_print(NULL, font_id, 14, 200, cur->blurb);
        rdpq_text_print(NULL, font_id, 14, 224, "[A] LAUNCH       UP/DOWN: SELECT");

        rdpq_detach_show();
    }
}
