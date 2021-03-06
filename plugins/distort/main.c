/*
xlivebg - live wallpapers for the X window system
Copyright (C) 2019-2020  John Tsiombikas <nuclear@member.fsf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <math.h>
#include <GL/gl.h>
#include "xlivebg.h"

static int init(void *cls);
static int start(long tmsec, void *cls);
static void draw(long tmsec, void *cls);
static void prop(const char *prop, void *cls);

#define PROPLIST	\
	"proplist {\n" \
	"    prop {\n" \
	"        id = \"amplitude\"\n" \
	"        desc = \"amplitude of the distortion\"\n" \
	"        type = \"number\"\n" \
	"        range = [0, 0.1]\n" \
	"    }\n" \
	"    prop {\n" \
	"        id = \"frequency\"\n" \
	"        desc = \"frequency of the distortion\"\n" \
	"        type = \"number\"\n" \
	"        range = [0, 50]\n" \
	"    }\n" \
	"}\n"

static struct xlivebg_plugin plugin = {
	"distort",
	"Image distortion effect on the background image",
	PROPLIST,
	XLIVEBG_25FPS,
	init, 0,
	start, 0,
	draw,
	prop,
	0, 0
};

static float ampl, freq;

int register_plugin(void)
{
	return xlivebg_register_plugin(&plugin);
}

static int init(void *cls)
{
	xlivebg_defcfg_num("xlivebg.distort.amplitude", 0.025f);
	xlivebg_defcfg_num("xlivebg.distort.frequency", 8.0f);
	return 0;
}

static int start(long tmsec, void *cls)
{
	prop("amplitude", 0);
	prop("frequency", 0);
	return 0;
}

static void prop(const char *prop, void *cls)
{
	switch(prop[0]) {
	case 'a':	/* amplitude */
		ampl = xlivebg_getcfg_num("xlivebg.distort.amplitude", 0.025);
		break;
	case 'f':	/* frequency */
		freq = xlivebg_getcfg_num("xlivebg.distort.frequency", 8.0);
		break;
	}
}

#define USUB	45
#define VSUB	20

static float wave(float x, float frq, float amp, float t)
{
	t *= 0.5;
	return (cos(x * frq + t)/* + cos(x * frq * 2.0f + t) * 0.5f*/) * amp;
}

static float dmask(float x)
{
	float s = sin(x * M_PI) * 20.0f;
	return s > 1.0f ? 1.0f : s;
}

#define COLOR(u, v)	\
	do { \
		float m = dmask(u) * dmask(v); \
		glColor3f(m, m, m); \
	} while(0)

static void emit_vertex(float x, float y, float u, float v, float au, float av,
		struct xlivebg_image *amask)
{
	if(amask) {
		float s;
		uint32_t mpix;
		int tx = (int)(u * amask->width);
		int ty = amask->height - (int)(v * amask->height);

		if(tx < 0) tx = 0;
		if(ty < 0) ty = 0;
		if(tx >= amask->width) tx = amask->width - 1;
		if(ty >= amask->height) ty = amask->height - 1;

		mpix = amask->pixels[ty * amask->width + tx] & 0xff;
		s = (float)mpix / 255.0f;
		au *= s;
		av *= s;
	}

	glTexCoord2f(u + au, 1.0f - (v + av));
	glVertex2f(x, y);
}

static void distquad(float t, struct xlivebg_image *amask)
{
	int i, j;
	float du = 1.0f / (float)USUB;
	float dv = 1.0f / (float)VSUB;
	float dx = du * 2.0f;
	float dy = dv * 2.0f;

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();

	glBegin(GL_QUADS);
	for(i=0; i<VSUB; i++) {
		float av0, av1;
		float v0 = (float)i * dv;
		float v1 = v0 + dv;
		float y = v0 * 2.0f - 1.0f;

		av0 = wave(v0, freq * 2.0f, dmask(v0) * ampl * 0.75, t);
		av1 = wave(v1, freq * 2.0f, dmask(v1) * ampl * 0.75, t);

		for(j=0; j<USUB; j++) {
			float au0, au1;
			float u0 = (float)j * du;
			float u1 = u0 + du;
			float x = u0 * 2.0f - 1.0f;

			au0 = wave(u0, freq, dmask(u0) * ampl, t);
			au1 = wave(u1, freq, dmask(u1) * ampl, t);

			emit_vertex(x, y, u0, v0, au0, av0, amask);
			emit_vertex(x + dx, y, u1, v0, au1, av0, amask);
			emit_vertex(x + dx, y + dy, u1, v1, au1, av1, amask);
			emit_vertex(x, y + dy, u0, v1, au0, av1, amask);
		}
	}
	glEnd();
}

static void draw(long tmsec, void *cls)
{
	int i, num_scr;
	struct xlivebg_image *img;
	float xform[16];
	float t = (float)tmsec / 1000.0f;

	xlivebg_clear(GL_COLOR_BUFFER_BIT);

	num_scr = xlivebg_screen_count();
	for(i=0; i<num_scr; i++) {
		xlivebg_gl_viewport(i);

		if((img = xlivebg_bg_image(i)) && img->tex) {
			struct xlivebg_image *amask = xlivebg_anim_mask(i);

			xlivebg_calc_image_proj(i, (float)img->width / img->height, xform);
			glMatrixMode(GL_PROJECTION);
			glLoadMatrixf(xform);

			glBindTexture(GL_TEXTURE_2D, img->tex);
			glEnable(GL_TEXTURE_2D);

			distquad(t, amask);
		}
	}
}
