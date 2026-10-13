/* Dictionary lookup for the "type a word and the letters line up" feature. */
#include "bs.h"
#include "words_bin.h"

static char *buf;
static const char **list;
static int nwords;

static int cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

void words_init(void)
{
    const u32 n = words_bin_size;
    buf = (char *)malloc(n + 1);
    if (!buf) return;
    memcpy(buf, words_bin, n);
    buf[n] = 0;
    int cap = 1;
    for (u32 i = 0; i < n; i++) if (buf[i] == '\n') cap++;
    list = (const char **)malloc(sizeof(char *) * cap);
    if (!list) return;
    char *p = buf;
    while (*p) {
        char *e = p;
        while (*e && *e != '\n') e++;
        const bool more = *e != 0;
        *e = 0;
        if (e > p) list[nwords++] = p;
        p = more ? e + 1 : e;
    }
    qsort(list, nwords, sizeof(char *), cmp);
}

bool word_exists(const char *s, int len)
{
    char key[20];
    if (!nwords || len < 2 || len > 15) return false;
    memcpy(key, s, len);
    key[len] = 0;
    const char *k = key;
    return bsearch(&k, list, nwords, sizeof(char *), cmp) != NULL;
}
