/* --- Prototypes -------------------------------------------------------- */

void    oops(char *msg);

Widget  add_pulldown_menu(Widget,char*);
Widget  add_menu_entry(Widget, const char*, XtCallbackProc, XtPointer);
Widget  add_menu_sep(Widget menu,char *name);

int popup_menu(Widget, const char*, struct STRTAB*);
void popdown_CB(Widget widget, XtPointer client_data, XtPointer calldata);
void destroy_CB(Widget widget, XtPointer client_data, XtPointer calldata);
void center_under_mouse(Widget shell,int,int);
void offscreen_scroll_AC(Widget widget,  XEvent *event,
			 String *params, Cardinal *num_params);

char* get_string_resource(Widget widget, char *name);

void kbd_scroll_viewport_AC(Widget widget,  XEvent *event,
			    String *params, Cardinal *num_params);
void report_viewport_CB(Widget, XtPointer, XtPointer);

void get_user_string(Widget, char*, char*, char*, XtCallbackProc, XtPointer);
void tell_user(Widget, char*, char*);
void xperror(Widget, char*);

void help_AC(Widget widget,  XEvent *event,
	     String *params, Cardinal *num_params);
void set_shadowWidth_AC(Widget widget,  XEvent *event,
			String *params, Cardinal *num_params);
