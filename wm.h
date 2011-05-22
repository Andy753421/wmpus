void wm_init(win_t *root);

int wm_handle_key(win_t *win, Key_t key, mod_t mod, ptr_t ptr);

int wm_handle_ptr(win_t *win, ptr_t ptr);

void wm_insert(win_t *win);

void wm_remove(win_t *win);
