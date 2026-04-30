#include <stdio.h>

#include "lobby.h"

// Simple 2D-only lobby screen. We don't bother spinning up the t3d frame
// because nothing 3D needs to render here.

static const joypad_port_t LOBBY_PORTS[LOBBY_MAX_PLAYERS] = {
    JOYPAD_PORT_1, JOYPAD_PORT_2, JOYPAD_PORT_3, JOYPAD_PORT_4
};

// Background color of the lobby screen. Slightly darker than the game's
// space-blue so the title text + slots pop.
#define BG_R 8
#define BG_G 12
#define BG_B 24

// Slot card layout: 4 cards stacked vertically in the centre of the screen.
#define SLOT_X      40
#define SLOT_Y      60
#define SLOT_W      240
#define SLOT_H      28
#define SLOT_GAP    6

static int lobby_count_ready(const LobbyState *st)
{
    int n = 0;
    for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) if (st->ready[i]) n++;
    return n;
}

static int lobby_count_connected(const LobbyState *st)
{
    int n = 0;
    for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) if (st->connected[i]) n++;
    return n;
}

// Edge-detected input: returns true if `cur` is pressed and `prev` wasn't.
static bool pressed(bool cur, bool prev) { return cur && !prev; }

// Filled rectangle helper. rdpq's standard mode supports a primitive color
// fill via rdpq_set_mode_fill but that disables blending; for the few
// rectangles we draw the simpler `rdpq_set_mode_standard + fill_rectangle`
// is enough.
static void fill_rect(int x0, int y0, int x1, int y1,
                      uint8_t r, uint8_t g, uint8_t b)
{
    rdpq_set_mode_fill(RGBA32(r, g, b, 0xFF));
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

static void draw_slot(int font_id, int slot, const LobbyState *st, int hold_frames)
{
    int y = SLOT_Y + slot * (SLOT_H + SLOT_GAP);
    bool conn  = st->connected[slot];
    bool ready = st->ready[slot];

    uint8_t r, g, b;
    if (!conn)        { r = 32;  g = 32;  b = 40;  }
    else if (ready)   { r = 30;  g = 110; b = 60;  }
    else              { r = 60;  g = 60;  b = 80;  }
    fill_rect(SLOT_X, y, SLOT_X + SLOT_W, y + SLOT_H, r, g, b);
    // Inner border so the cards read as buttons.
    fill_rect(SLOT_X, y, SLOT_X + SLOT_W, y + 1, 200, 200, 220);
    fill_rect(SLOT_X, y + SLOT_H - 1, SLOT_X + SLOT_W, y + SLOT_H, 200, 200, 220);

    char label[64];
    if (!conn) {
        snprintf(label, sizeof label, "P%d  --  CONNECT CONTROLLER", slot + 1);
    } else if (ready) {
        snprintf(label, sizeof label, "P%d  READY", slot + 1);
    } else {
        snprintf(label, sizeof label, "P%d  PRESS A TO READY UP", slot + 1);
    }
    rdpq_text_print(NULL, font_id, SLOT_X + 8, y + 18, label);

    // P1 is the launcher — small in-card progress bar mirrors the big
    // centered indicator drawn by the lobby loop.
    if (slot == 0 && hold_frames > 0) {
        int bar_w = (SLOT_W - 16) * hold_frames / LOBBY_LAUNCH_HOLD;
        if (bar_w > SLOT_W - 16) bar_w = SLOT_W - 16;
        fill_rect(SLOT_X + 8, y + SLOT_H - 6,
                  SLOT_X + 8 + bar_w, y + SLOT_H - 3,
                  240, 200, 80);
    }
}

int lobby_run(int font_id)
{
    LobbyState st = {0};
    bool prev_a[LOBBY_MAX_PLAYERS]     = {0};
    bool prev_b[LOBBY_MAX_PLAYERS]     = {0};
    bool start_held = false;
    int  hold_frames = 0;

    for (;;) {
        joypad_poll();
        joypad_inputs_t in[LOBBY_MAX_PLAYERS] = {0};
        for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) {
            st.connected[i] = joypad_is_connected(LOBBY_PORTS[i]);
            if (!st.connected[i]) {
                // If a controller drops mid-lobby, un-ready them.
                st.ready[i] = false;
                continue;
            }
            in[i] = joypad_get_inputs(LOBBY_PORTS[i]);
            bool a_now = in[i].btn.a != 0;
            bool b_now = in[i].btn.b != 0;
            if (pressed(a_now, prev_a[i]))     st.ready[i] = true;
            if (pressed(b_now, prev_b[i]))     st.ready[i] = false;
            prev_a[i] = a_now;
            prev_b[i] = b_now;
        }

        // P1 is the launcher and is auto-ready as soon as they're connected,
        // so solo play just needs a connected pad + holding START. P2-P4 still
        // opt in via A; connected-but-not-ready non-P1 pads are observers and
        // are dropped from the launch mask. Any other-pad readies still need
        // P1 to actually pull the trigger by holding START.
        if (st.connected[0]) st.ready[0] = true;

        start_held = st.connected[0] && in[0].btn.start != 0;
        if (start_held) {
            hold_frames++;
            if (hold_frames >= LOBBY_LAUNCH_HOLD) {
                int mask = 0;
                for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) {
                    if (st.connected[i] && st.ready[i]) mask |= (1 << i);
                }
                if (mask == 0) mask = 1;  // safety: P1 always plays
                return mask;
            }
        } else {
            hold_frames = 0;
        }

        // ---- Draw ---------------------------------------------------
        rdpq_attach(display_get(), display_get_zbuf());
        rdpq_set_mode_fill(RGBA32(BG_R, BG_G, BG_B, 0xFF));
        rdpq_fill_rectangle(0, 0, 320, 240);

        rdpq_set_mode_standard();
        rdpq_text_print(NULL, font_id, 90, 28, "STAR CREW 64");
        rdpq_text_print(NULL, font_id, 96, 42, "READY ROOM");

        for (int i = 0; i < LOBBY_MAX_PLAYERS; i++) {
            draw_slot(font_id, i, &st, hold_frames);
        }

        int ready  = lobby_count_ready(&st);
        int active = lobby_count_connected(&st);
        char footer[80];
        if (hold_frames > 0) {
            snprintf(footer, sizeof footer,
                     "LAUNCHING IN %ds...",
                     (LOBBY_LAUNCH_HOLD - hold_frames + 59) / 60);
        } else if (active == 0) {
            snprintf(footer, sizeof footer,
                     "CONNECT P1 CONTROLLER TO LAUNCH");
        } else {
            snprintf(footer, sizeof footer,
                     "READY %d/%d  -  P1 HOLD START 3s TO LAUNCH",
                     ready, active);
        }
        rdpq_text_print(NULL, font_id, 14, 224, footer);

        // Big centered hold-START indicator. Drawn whenever P1 is pressing
        // START so the user always sees feedback that their input is being
        // received, even before the bar reaches full.
        if (hold_frames > 0) {
            const int bw = 200;
            const int bh = 16;
            const int bx = (320 - bw) / 2;
            const int by = 130;
            // Frame
            fill_rect(bx - 3, by - 3, bx + bw + 3, by + bh + 3, 200, 200, 220);
            // Track
            fill_rect(bx, by, bx + bw, by + bh, 30, 40, 60);
            // Fill
            int fw = bw * hold_frames / LOBBY_LAUNCH_HOLD;
            if (fw > bw) fw = bw;
            uint8_t fr = 240, fg = 200, fb = 80;
            if (hold_frames >= LOBBY_LAUNCH_HOLD) { fr = 120; fg = 240; fb = 140; }
            fill_rect(bx, by, bx + fw, by + bh, fr, fg, fb);
            rdpq_set_mode_standard();
            rdpq_text_print(NULL, font_id, bx + 32, by - 6, "HOLDING START...");
        }

        rdpq_detach_show();
    }
}
