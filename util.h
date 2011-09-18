/* Misc macros */
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define new0(type) (calloc(1, sizeof(type)))

#define countof(x) (sizeof(x)/sizeof((x)[0]))

/* Constant lenght map functitons */
#define map_getg(map, test) ({ \
	int i; \
	for (i = 0; i < countof(map) && !(test); i++); \
	i < countof(map) ? &map[i] : NULL ; \
})

#define map_get(m,k)    map_getg(m,k==*((typeof(k)*)&m[i]))
#define map_getr(m,k)   map_getg(m,k==*(((typeof(k)*)&m[i+1])-1))
#define map_getk(m,k,a) map_getg(m,k==m[i].a)

/* Linked lists */
typedef struct list {
	struct list *prev;
	struct list *next;
	void   *data;
} list_t;

list_t *list_insert(list_t *after, void *data);

list_t *list_append(list_t *before, void *data);

list_t *list_remove(list_t *head, list_t *item);

int list_length(list_t *item);

/* Misc */
int error(char *fmt, ...);
