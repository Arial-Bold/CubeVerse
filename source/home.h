/* home.h - pieces of the home screen the opening animation also draws, so
 * it can hand over to the home cubes without a seam. */
#pragma once
#include "vcube.h"

float home_center_y(void);
/* Where a cube sits on the ring at angle theta (0 = front, 90 = right,
 * 180 = back, 270 = left): offset from the middle, scale and brightness. */
void home_slot(float theta_deg, float *dx, float *dy, float *scale, float *alpha);
/* Cube i (0..3) at ring angle theta, at time t (its drift and icon). */
void home_cube_setup(VCube *v, int i, float theta, float time);
void home_draw_cubes(float ring_pos, float time, float alpha, int skip_mask);
void home_draw_header(float alpha);
void home_draw_labels(float ring_pos, float alpha);
void home_draw_hints(float alpha);
