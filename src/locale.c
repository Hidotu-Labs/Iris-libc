#include <locale.h>
#include <stddef.h>

static char c_locale[] = "C";
static char latin1_locale[] = "en_US.ISO-8859-1";
static char *current_locale = c_locale;

static int strings_equal(const char *left, const char *right)
{
    while (*left != '\0' && *left == *right) {
        left++;
        right++;
    }
    return *left == *right;
}

char *setlocale(int category, const char *locale)
{
    if (category != LC_CTYPE && category != LC_ALL) {
        return NULL;
    }
    if (locale == NULL) {
        return current_locale;
    }
    if (strings_equal(locale, "C") || strings_equal(locale, "POSIX")) {
        current_locale = c_locale;
    } else if (strings_equal(locale, "en_US.ISO-8859-1")) {
        current_locale = latin1_locale;
    } else {
        return NULL;
    }
    return current_locale;
}

int __iris_ctype_latin1_enabled(void)
{
    return current_locale == latin1_locale;
}