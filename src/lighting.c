#include "lighting.h"

// tiny3d supports up to 8 light slots (slot 0 used by the directional sun).
#define LIGHT_SLOT_MAX 8

void lighting_setup_main(void)
{
    uint8_t ambientColor[4]   = {0x30, 0x32, 0x3C, 0xFF};   // dim cool ambient
    uint8_t mainLightColor[4] = {0x80, 0x88, 0x98, 0xFF};   // softer key light so point lights are visible
    T3DVec3 mainLightDir      = {{-0.5f, -1.0f, -0.3f}};
    t3d_vec3_norm(&mainLightDir);

    t3d_light_set_ambient(ambientColor);
    t3d_light_set_directional(0, mainLightColor, &mainLightDir);
}

int lighting_apply_points(const PointLight *lights, int count)
{
    int slot = 1;  // slot 0 is the directional key light
    for (int i = 0; i < count && slot < LIGHT_SLOT_MAX; i++) {
        // t3d_light_set_point(slot, color, position, size, isExponent)
        //   size     : falloff distance (world units)
        //   isExponent: true = exponential falloff, false = linear-ish
        t3d_light_set_point(
            slot,
            (uint8_t *)lights[i].color,
            (T3DVec3 *)&lights[i].position,
            lights[i].size,
            false
        );
        slot++;
    }
    return slot;
}

void lighting_finalize(int lightCount)
{
    t3d_light_set_count(lightCount);
}

// ---- legacy laser lights (unused by new main.c, kept for compat) ---------
int lighting_add_laser_lights(Laser *lasers, int maxLasers)
{
    int lightIdx = 1;
    for (int i = 0; i < maxLasers && lightIdx < 4; i++) {
        if (lasers[i].active) {
            T3DVec3 laserLightDir = {{
                lasers[i].pos.v[0],
                lasers[i].pos.v[1],
                lasers[i].pos.v[2]
            }};
            t3d_vec3_norm(&laserLightDir);
            float dist = t3d_vec3_len(&lasers[i].pos);
            float intensity = 50.0f / (dist + 10.0f);
            if (intensity > 1.0f) intensity = 1.0f;
            if (intensity < 0.1f) intensity = 0.1f;
            uint8_t bright = (uint8_t)(0xFF * intensity);
            uint8_t laserLightColor[4] = {bright, (uint8_t)(bright * 0.85f), (uint8_t)(bright * 0.2f), 0xFF};
            t3d_light_set_directional(lightIdx, laserLightColor, &laserLightDir);
            lightIdx++;
        }
    }
    return lightIdx;
}
