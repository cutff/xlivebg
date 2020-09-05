#include <stdio.h>
#include <string.h>
#include "xmutil.h"

extern XtAppContext app;
extern Widget app_shell;

Widget xm_label(Widget par, const char *text)
{
	Widget w;
	Arg arg;
	XmString str = XmStringCreateSimple((char*)text);
	XtSetArg(arg, XmNlabelString, str);
	w = XmCreateLabel(par, "label", &arg, 1);
	XmStringFree(str);
	XtManageChild(w);
	return w;
}

Widget xm_frame(Widget par, const char *title)
{
	Widget w;
	Arg arg;

	w = XmCreateFrame(par, "frame", 0, 0);
	XtSetArg(arg, XmNframeChildType, XmFRAME_TITLE_CHILD);
	XtManageChild(XmCreateLabelGadget(w, (char*)title, &arg, 1));
	XtManageChild(w);
	return w;
}

Widget xm_rowcol(Widget par, int orient)
{
	Widget w;
	Arg arg;

	XtSetArg(arg, XmNorientation, orient);
	w = XmCreateRowColumn(par, "rowcolumn", &arg, 1);
	XtManageChild(w);
	return w;
}

Widget xm_button(Widget par, const char *text, XtCallbackProc cb, void *cls)
{
	Widget w = XmCreatePushButton(par, "...", 0, 0);
	XtManageChild(w);
	if(cb) {
		XtAddCallback(w, XmNactivateCallback, cb, cls);
	}
	return w;
}


static void filesel_handler(Widget dlg, void *cls, void *calldata);

const char *file_dialog(Widget shell, const char *start_dir, const char *filter, char *buf, int bufsz)
{
	Widget dlg;
	Arg argv[3];
	int argc = 0;

	if(bufsz < sizeof bufsz) {
		fprintf(stderr, "file_dialog: insufficient buffer size: %d\n", bufsz);
		return 0;
	}
	memcpy(buf, &bufsz, sizeof bufsz);

	if(start_dir) {
		XmString s = XmStringCreateSimple((char*)start_dir);
		XtSetArg(argv[argc++], XmNdirectory, s);
		XmStringFree(s);
	}
	if(filter) {
		XmString s = XmStringCreateSimple((char*)filter);
		XtSetArg(argv[argc++], XmNdirMask, s);
		XmStringFree(s);
	}
	XtSetArg(argv[argc++], XmNpathMode, XmPATH_MODE_RELATIVE);

	dlg = XmCreateFileSelectionDialog(app_shell, "filesb", argv, argc);
	XtAddCallback(dlg, XmNcancelCallback, filesel_handler, 0);
	XtAddCallback(dlg, XmNokCallback, filesel_handler, buf);
	XtManageChild(dlg);

	while(XtIsManaged(dlg)) {
		XtAppProcessEvent(app, XtIMAll);
	}

	return *buf ? buf : 0;
}

static void filesel_handler(Widget dlg, void *cls, void *calldata)
{
	char *fname;
	char *buf = cls;
	int bufsz;
	XmFileSelectionBoxCallbackStruct *cbs = calldata;

	if(buf) {
		memcpy(&bufsz, buf, sizeof bufsz);
		*buf = 0;

		if(!(fname = XmStringUnparse(cbs->value, XmFONTLIST_DEFAULT_TAG, XmCHARSET_TEXT,
						XmCHARSET_TEXT, 0, 0, XmOUTPUT_ALL))) {
			return;
		}

		strncpy(buf, fname, bufsz - 1);
		buf[bufsz - 1] = 0;
		XtFree(fname);
	}
	XtUnmanageChild(dlg);
}


static void pathfield_browse(Widget bn, void *cls, void *calldata);
static void pathfield_modify(Widget txf, void *cls, void *calldata);

Widget create_pathfield(Widget par, const char *filter, void (*handler)(const char*))
{
	Widget hbox, tx_path;
	Arg args[2];

	hbox = xm_rowcol(par, XmHORIZONTAL);

	XtSetArg(args[0], XmNcolumns, 40);
	XtSetArg(args[1], XmNeditable, 0);
	tx_path = XmCreateTextField(hbox, "textfield", args, 2);
	XtManageChild(tx_path);
	XtAddCallback(tx_path, XmNvalueChangedCallback, pathfield_modify, (void*)handler);

	xm_button(hbox, "...", pathfield_browse, tx_path);
	return tx_path;
}

static void pathfield_browse(Widget bn, void *cls, void *calldata)
{
	char buf[512];

	if(file_dialog(app_shell, 0, 0, buf, sizeof buf)) {
		XmTextFieldSetString(cls, buf);
	}
}

static void pathfield_modify(Widget txf, void *cls, void *calldata)
{
	void (*usercb)(const char*) = (void (*)(const char*))cls;

	char *text = XmTextFieldGetString(txf);
	if(usercb) usercb(text);
	XtFree(text);
}

