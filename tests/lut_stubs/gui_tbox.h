/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Host stub of CHDK's text box module: records the request so tests can
 * answer it with any text (or 0 for cancel). */
#ifndef TEXTBOX_H
#define TEXTBOX_H
typedef struct {int (*textbox_init)(int title,int msg,const char *defaultstr,unsigned int maxsize,void (*on_select)(const char *newstr),char *input_buffer);} libtextbox_sym;
static void (*stub_tbox_select)(const char *newstr);
static unsigned stub_tbox_max;
static char stub_tbox_default[128];
static int stub_textbox_init(int title,int msg,const char *defaultstr,unsigned int maxsize,void (*on_select)(const char *),char *input_buffer) {
    (void)title;(void)msg;(void)input_buffer;stub_tbox_select=on_select;stub_tbox_max=maxsize;
    snprintf(stub_tbox_default,sizeof(stub_tbox_default),"%s",defaultstr?defaultstr:"");return 1;
}
static libtextbox_sym stub_libtextbox={stub_textbox_init},*libtextbox=&stub_libtextbox;
#endif
