#include <string.h>
#include <sys/stddef.h> /* ptrdiff_t */

//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "mrkcommon/util.h"
#include "mrkcommon/array.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/xmatch.h"

#include "diag.h"

static int
str_cmp(const char **s1, const char **s2)
{
    //TRACE("%s - %s", *s1, *s2);
    if (*s1 == NULL && *s2 == NULL) {
        return 0;
    }
    if (*s2 == NULL) {
        return 1;
    } else if (*s1 == NULL) {
        return -1;
    } else {
        return strcmp(*s1, *s2);
    }
}

int
xmatch_init(xmatch_t *xmatch, const char *pattern)
{
    const char *s1, *s2;

    xmatch->pattern = pattern;

    init_string_array(&xmatch->compiled_pattern);

    for (s1 = pattern, s2 = pattern; s2 != NULL; s2 = strchr(s1, '|')) {
        char **entry;
        ptrdiff_t d;

        d = s2 - s1;
        if (d > 0) {
            if ((entry = array_incr(&xmatch->compiled_pattern)) == NULL) {
                FAIL("array_incr");
            }

            if ((*entry = malloc(s2 - s1 + 1)) == NULL) {
                FAIL("malloc");
            }
            memcpy(*entry, s1, d);
            *(*entry + d) = '\0';
        }
        s1 = s2 + 1;
    }

    /* XXX make them weak refs */
    init_string_array(&xmatch->input_stack);

    return 0;
}

int
xmatch_fini(xmatch_t *xmatch)
{
    xmatch->pattern = NULL;

    if (array_fini(&xmatch->compiled_pattern) != 0) {
        FAIL("array_fini");
    }
    if (array_fini(&xmatch->input_stack) != 0) {
        FAIL("array_fini");
    }
    return 0;
}

void
xmatch_push(xmatch_t *xmatch, const char *entry)
{
    char **e;

    if ((e = array_incr(&xmatch->input_stack)) == NULL) {
        FAIL("array_incr");
    }
    *e = strdup(entry);
}

void
xmatch_pop(xmatch_t *xmatch)
{
    if (xmatch->input_stack.elnum > 0) {
        if (array_decr(&xmatch->input_stack) != 0) {
            FAIL("array_decr");
        }
    }
}

const char *
xmatch_top(xmatch_t *xmatch, int idx)
{
    const char **e;

    if ((e = array_get(&xmatch->input_stack,
                       xmatch->input_stack.elnum - 1 - idx)) == NULL) {
        return NULL;
    }
    return *e;
}

int
xmatch_matches(xmatch_t *xmatch)
{

    return array_cmp(&xmatch->compiled_pattern,
                     &xmatch->input_stack,
                     (array_compar_t)str_cmp, 0);
}

