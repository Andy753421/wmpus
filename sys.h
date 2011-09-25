typedef struct win_sys win_sys_t;
typedef struct win_wm  win_wm_t;
typedef struct {
	int x, y, z;
	int w, h;
	win_sys_t *sys;
	win_wm_t  *wm;
} win_t;

typedef enum {
	// 'char' = unicode,
	key_alert       = '\a',    // Bell (alert)
	key_backspace   = '\b',    // Backspace
	key_formfeed    = '\f',    // Formfeed
	key_newline     = '\n',    // New line
	key_return      = '\r',    // Carriage return
	key_tab         = '\t',    // Horizontal tab
	key_vtab        = '\v',    // Vertical tab
	key_singlequote = '\'',    // Single quotation mark
	key_doublequote = '\"',    // Double quotation mark
	key_backslash   = '\\',    // Backslash
	key_question    = '\?',    // Literal question mark
	key_none        = 0xF0000, // unused unicode space
	key_mouse0, key_mouse1, key_mouse2, key_mouse3,
	key_mouse4, key_mouse5, key_mouse6, key_mouse7,
	key_left, key_right, key_up,     key_down,
	key_home, key_end,   key_pageup, key_pagedown,
	key_f1, key_f2,  key_f3,  key_f4,
	key_f5, key_f6,  key_f7,  key_f8,
	key_f9, key_f10, key_f11, key_f12,
	key_alt, key_ctrl, key_shift, key_win,
	key_enter, key_leave, key_focus, key_unfocus,
} Key_t;

typedef struct {
	unsigned char alt   : 1;
	unsigned char ctrl  : 1;
	unsigned char shift : 1;
	unsigned char win   : 1;
	unsigned char up    : 1;
	unsigned char spare : 3;
} mod_t;
#define MOD(...) ((mod_t){__VA_ARGS__})
#define mod2int(mod) (*((unsigned short*)&(mod)))

typedef struct {
	int  x,  y;
	int rx, ry;
} ptr_t;
#define PTR(...) ((ptr_t){__VA_ARGS__})

void sys_watch(win_t *win, Key_t key, mod_t mod);

void sys_unwatch(win_t *win, Key_t key, mod_t mod);

void sys_move(win_t *win, int x, int y, int w, int h);

void sys_raise(win_t *win);

void sys_focus(win_t *win);

void sys_foreach(win_t *win);

list_t *sys_info(win_t *win);

win_t *sys_init(void);

void sys_run(win_t *root);
