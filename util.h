#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

#define new0(type) (calloc(1, sizeof(type)))

#define countof(x) (sizeof(x)/sizeof((x)[0]))

#define map_get(map, key) ({ \
	int i; \
	for (i = 0; i < countof(map) && \
		*((typeof(key)*)&map[i]) != key; i++); \
	i < countof(map) ? &map[i] : NULL ; \
})
