/* vcube.h - the glass cubes of CubeVerse's home screen: Flashcube's cube
 * (tinted glass, rounded silver edges lit from the top left, sweeping
 * reflections and a block icon floating inside), drawable at any place,
 * size and angle so four of them can turn around the ring. */
#pragma once
#include <stdbool.h>
#include "gfx.h"
#include "m3d.h"

#define VC_PERSP 700.0f
/* Flashcube's front view of the cube */
#define VC_BASE_X (-8.0f)
#define VC_BASE_Y (-12.0f)
#define VC_BASE_Z (-4.0f)

enum { VC_ICON_ACTIVITY, VC_ICON_COMMUNITIES, VC_ICON_NOTIFICATIONS, VC_ICON_MYPAGE, VC_ICON_COUNT };

typedef struct {
    float cx, cy;        /* the cube's middle on screen */
    float scale;         /* 1 = Flashcube's 180-unit cube */
    float alpha;
    float time;          /* drives the idle sway */
    float sway;          /* 0..1 */
    float ax, ay, az;    /* the angle it's seen at (degrees) */
    Color tint;          /* the front glass's colour */
    int   icon;          /* VC_ICON_*, or -1 for none */
    float icon_alpha;
    float icon_time;     /* drives the icon's change of shape */
    float wob_time;      /* drives the icon's rocking */
} VCube;

void vcube_init(VCube *c);
void vcube_draw(const VCube *c);
/* Where the cube shows on the screen (with its glow). */
void vcube_bounds(const VCube *c, float *x0, float *y0, float *x1, float *y1);
/* The rotation (and scale) the cube is drawn with, about its middle. */
M34  vcube_matrix(const VCube *c);
/* Glass for small planes: kind 0 = tinted front glass, 1 = clear side glass. */
void cube_glass(int kind, float size, float radius, float alpha, bool rim);
