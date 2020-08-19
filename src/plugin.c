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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <alloca.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <GL/gl.h>
#include "xlivebg.h"
#include "app.h"
#include "imageman.h"
#include "util.h"
#include "cfg.h"
#include "treestore.h"

static int load_plugins(const char *dirpath);

static struct xlivebg_plugin *act;
static struct xlivebg_plugin **plugins;
static int num_plugins, max_plugins;

/* searches for, and loads, all available plugins.
 * search paths:
 *  - PREFIX/lib/xlivebg/
 *  - $HOME/.local/lib/xlivebg/
 *  - $HOME/.xlivebg/plugins/
 */
void init_plugins(void)
{
	char *home = get_home_dir();
	char *dirpath;

	load_plugins(PREFIX "/lib/xlivebg");

	dirpath = alloca(strlen(home) + 32);

	sprintf(dirpath, "%s/.local/lib/xlivebg", home);
	load_plugins(dirpath);

	sprintf(dirpath, "%s/.xlivebg/plugins", home);
	load_plugins(dirpath);
}

static int load_plugins(const char *dirpath)
{
	int num = 0;
	DIR *dir;
	struct dirent *dent;
	struct stat st;
	char fname[1024];
	void *so;
	void (*reg)(void);

	if(!(dir = opendir(dirpath))) {
		return -1;
	}

	printf("xlivebg: searching for plugins in %s\n", dirpath);

	while((dent = readdir(dir))) {
		snprintf(fname, sizeof fname, "%s/%s", dirpath, dent->d_name);
		fname[sizeof fname - 1] = 0;

		if(stat(fname, &st) == -1 || !S_ISREG(st.st_mode)) {
			continue;
		}

		if((so = dlopen(fname, RTLD_LAZY))) {
			if((reg = dlsym(so, "register_plugin"))) {
				reg();
				plugins[num_plugins - 1]->so = so;
				num++;
			} else {
				dlclose(so);
			}
		} else {
			fprintf(stderr, "failed to open plugin: %s: %s\n", fname, dlerror());
		}
	}

	closedir(dir);
	return num;
}

struct xlivebg_plugin *get_plugin(int idx)
{
	return plugins[idx];
}

int get_plugin_count(void)
{
	return num_plugins;
}

struct xlivebg_plugin *find_plugin(const char *name)
{
	int i;

	for(i=0; i<num_plugins; i++) {
		if(strcasecmp(name, plugins[i]->name) == 0) {
			return plugins[i];
		}
	}
	return 0;
}

void activate_plugin(struct xlivebg_plugin *plugin)
{
	printf("xlivebg: activating plugin: %s\n", plugin->name);
	if(act) {
		if(act->stop) {
			act->stop(act->data);
		}
		xlivebg_destroy_gl();
	}

	if(xlivebg_init_gl() == -1) {
		fprintf(stderr, "xlivebg: failed to initialize OpenGL\n");
		return;
	}

	act = plugin;
	if(plugin->start) {
		plugin->start(msec, plugin->data);
	}

	upd_interval_usec = act->upd_interval;
}

struct xlivebg_plugin *get_active_plugin(void)
{
	return act;
}

int remove_plugin(int idx)
{
	if(idx < 0 || idx >= num_plugins) {
		return -1;
	}

	dlclose(plugins[idx]->so);

	if(idx == num_plugins - 1) {
		num_plugins--;
		return 0;
	}

	memmove(plugins + idx, plugins + idx + 1, (num_plugins - idx - 1) * sizeof *plugins);
	num_plugins--;
	return 0;
}


/* ---- plugin API ---- */

int xlivebg_register_plugin(struct xlivebg_plugin *plugin)
{
	if(num_plugins >= max_plugins) {
		int nmax = max_plugins ? max_plugins * 2 : 16;
		struct xlivebg_plugin **tmp = realloc(plugins, nmax * sizeof *plugins);
		if(!tmp) {
			perror("xlivebg_register_plugin");
			return -1;
		}
		plugins = tmp;
		max_plugins = nmax;
	}
	plugins[num_plugins++] = plugin;
	printf("xlivebg: registered plugin: %s\n", plugin->name);
	return 0;
}

int xlivebg_screen_count(void)
{
	return num_screens;
}

struct xlivebg_screen *xlivebg_screen(int idx)
{
	return screen + idx;
}

struct xlivebg_image *xlivebg_bg_image(int scr)
{
	return get_bg_image(scr);
}

struct xlivebg_image *xlivebg_anim_mask(int scr)
{
	return get_anim_mask(scr);
}

int xlivebg_fit_mode(int scr)
{
	/* TODO per-screen */
	return cfg.fit;
}

float xlivebg_crop_zoom(int scr)
{
	/* TODO per-screen */
	return cfg.zoom;
}

void xlivebg_crop_dir(int scr, float *dirvec)
{
	/* TODO per-screen */
	dirvec[0] = cfg.crop_dir[0];
	dirvec[1] = cfg.crop_dir[1];
}

/* plugin configuration interface */
int xlivebg_havecfg(const char *cfgpath)
{
	if(!cfg.ts) return 0;
	return ts_lookup(cfg.ts, cfgpath) ? 1 : 0;
}

const char *xlivebg_getcfg_str(const char *cfgpath, const char *def_val)
{
	if(!cfg.ts) return def_val;
	return ts_lookup_str(cfg.ts, cfgpath, def_val);
}

float xlivebg_getcfg_num(const char *cfgpath, float def_val)
{
	if(!cfg.ts) return def_val;
	return ts_lookup_num(cfg.ts, cfgpath, def_val);
}

int xlivebg_getcfg_int(const char *cfgpath, int def_val)
{
	if(!cfg.ts) return def_val;
	return ts_lookup_int(cfg.ts, cfgpath, def_val);
}

float *xlivebg_getcfg_vec(const char *cfgpath, float *def_val)
{
	if(!cfg.ts) return def_val;
	return ts_lookup_vec(cfg.ts, cfgpath, def_val);
}

/* returns pointer to the value of a specific attribute. If it doesn't exist,
 * creates it and any intermedate path components first.
 */
static struct ts_value *touch_node(const char *cfgpath)
{
	char *pathbuf, *ptr, *aname;
	struct ts_node *node, *child;
	struct ts_attr *attr;

	if(!cfg.ts) {
		if(!(cfg.ts = ts_alloc_node()) || !(cfg.ts->name = strdup("xlivebg"))) {
			ts_free_node(cfg.ts);
		}
	}

	pathbuf = alloca(strlen(cfgpath) + 1);
	strcpy(pathbuf, cfgpath);

	/* separate the attribute name from the rest of the node chain*/
	if(!(aname = strrchr(pathbuf, '.')) || !*++aname) {
		return 0;
	}
	aname[-1] = 0;

	/* grab the first element of the path, and make sure it's xlivebg */
	if(!(ptr = strtok(pathbuf, ".")) || strcmp(ptr, "xlivebg") != 0) {
		return 0;
	}

	node = cfg.ts;
	while((ptr = strtok(0, "."))) {
		if(!(child = ts_get_child(node, ptr))) {
			/* no such path component, create it and move on */
			if(!(child = ts_alloc_node()) || !(child->name = strdup(ptr))) {
				ts_free_node(child);
				return 0;
			}
			ts_add_child(node, child);
		}
		node = child;
	}

	/* reached the end of the chain, see if we have the attribute */
	if(!(attr = ts_get_attr(node, aname))) {
		if(!(attr = ts_alloc_attr()) || ts_set_attr_name(attr, aname) == -1) {
			return 0;
		}
		ts_add_attr(node, attr);
	}

	return &attr->val;
}

static char *skip_space(char *s)
{
	while(*s && isspace(*s)) s++;
	return s;
}

static void notify_plugin(const char *cfgpath)
{
	struct xlivebg_plugin *p;
	char *aname, *ptr, *end;
	char *buf;
	int len, aname_len;

	if(!(p = get_active_plugin())) return;

	buf = alloca(strlen(p->name) + 16);
	sprintf(buf, "xlivebg.%s.", p->name);
	if(!strstr(cfgpath, buf)) {
		/* this property has nothing to do with the current plugin */
		return;
	}

	/* if the plugin didn't specify a property list or a prop callback, we'll have to restart it */
	if(!p->props || !p->prop) {
		printf("notify_plugin: restarting live wallpaper\n");
		if(p->stop) p->stop(p->data);
		activate_plugin(p);
		return;
	}

	if((aname = strrchr(cfgpath, '.'))) {
		aname++;
	} else {
		aname = (char*)cfgpath;
	}
	aname_len = strlen(aname);

	/* check to see if the attribute name matches any attributes declared by the plugin */
	ptr = p->props;
	while((ptr = strstr(ptr, "id"))) {
		ptr = skip_space(ptr + 3);
		if(*ptr != '=') continue;
		ptr = skip_space(ptr + 1);
		if(*ptr != '"') continue;
		end = ++ptr;
		while(*end && *end != '\n' && *end != '"') end++;
		if(*end != '"') continue;

		len = end - ptr;
		if(len == aname_len && memcmp(aname, ptr, len) == 0) {
			/* found at least one match, notify the plugin and return */
			p->prop(aname, p->data);
			break;
		}
	}
}

int xlivebg_setcfg_str(const char *cfgpath, const char *str)
{
	struct ts_value *aval;
	if(!(aval = touch_node(cfgpath))) {
		return -1;
	}
	if(ts_set_value_str(aval, str) == -1) {
		return -1;
	}
	notify_plugin(cfgpath);
	return 0;
}

int xlivebg_setcfg_num(const char *cfgpath, float val)
{
	struct ts_value *aval;
	if(!(aval = touch_node(cfgpath))) {
		return -1;
	}
	if(ts_set_valuef(aval, val) == -1) {
		return -1;
	}
	notify_plugin(cfgpath);
	return 0;
}

int xlivebg_setcfg_int(const char *cfgpath, int val)
{
	struct ts_value *aval;
	if(!(aval = touch_node(cfgpath))) {
		return -1;
	}
	if(ts_set_valuei(aval, val) == -1) {
		return -1;
	}
	notify_plugin(cfgpath);
	return 0;
}

int xlivebg_setcfg_vec(const char *cfgpath, float *vec)
{
	struct ts_value *aval;
	if(!(aval = touch_node(cfgpath))) {
		return -1;
	}
	if(ts_set_valuef_arr(aval, 4, vec) == -1) {
		return -1;
	}
	notify_plugin(cfgpath);
	return 0;
}


/* helpers */
void xlivebg_gl_viewport(int sidx)
{
	struct xlivebg_screen *scr = xlivebg_screen(sidx);

	glViewport(scr->vport[0], scr->vport[1], scr->vport[2], scr->vport[3]);
}

void xlivebg_calc_image_proj(int sidx, float img_aspect, float *xform)
{
	static const float ident[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	float vpscale;
	struct xlivebg_screen *scr = xlivebg_screen(sidx);
	float aspect = (float)scr->width / (float)scr->height;
	int fit_mode = xlivebg_fit_mode(sidx);

	memcpy(xform, ident, sizeof ident);

	if(fit_mode != XLIVEBG_FIT_STRETCH) {
		if(aspect > img_aspect) {
			vpscale = xform[0] = img_aspect / aspect;
			xform[5] = 1.0f;
		} else {
			vpscale = xform[5] = aspect / img_aspect;
			xform[0] = 1.0f;
		}

		if(fit_mode == XLIVEBG_FIT_CROP) {
			float cropscale, maxpan, tx, ty;
			float cdir[2];

			cropscale = 1.0f / vpscale;
			cropscale = 1.0f + (cropscale - 1.0f) * xlivebg_crop_zoom(sidx);
			maxpan = cropscale - 1.0f;

			xform[0] *= cropscale;
			xform[5] *= cropscale;

			xlivebg_crop_dir(sidx, cdir);
			if(aspect > img_aspect) {
				tx = 0.0f;
				ty = -cdir[1] * maxpan;
			} else {
				tx = -cdir[0] * maxpan;
				ty = 0.0f;
			}

			xform[12] = tx;
			xform[13] = ty;
		} else {
			xform[12] = xform[13] = 0.0f;
		}
	}
}

void xlivebg_gl_image_proj(int scr, float img_aspect)
{
	float mat[16];
	xlivebg_calc_image_proj(scr, img_aspect, mat);

	glPushAttrib(GL_TRANSFORM_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(mat);
	glPopAttrib();
}

void xlivebg_mouse_pos(int *mx, int *my)
{
	app_getmouse(mx, my);
}
