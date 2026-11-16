#include <locale.h>
#include <limits.h>
#include <stddef.h>

static char c_locale[] = "C";
static char latin1_locale[] = "en_US.ISO-8859-1";
static char *current_locale = c_locale;
static struct lconv current_conventions = {
    .decimal_point = ".",
    .thousands_sep = "",
    .grouping = "",
    .int_curr_symbol = "",
    .currency_symbol = "",
    .mon_decimal_point = "",
    .mon_thousands_sep = "",
    .mon_grouping = "",
    .positive_sign = "",
    .negative_sign = "",
    .int_frac_digits = CHAR_MAX,
    .frac_digits = CHAR_MAX,
    .p_cs_precedes = CHAR_MAX,
    .p_sep_by_space = CHAR_MAX,
    .n_cs_precedes = CHAR_MAX,
    .n_sep_by_space = CHAR_MAX,
    .p_sign_posn = CHAR_MAX,
    .n_sign_posn = CHAR_MAX
};

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
    if (category != LC_CTYPE && category != LC_NUMERIC && category != LC_TIME &&
        category != LC_COLLATE && category != LC_MONETARY &&
        category != LC_MESSAGES && category != LC_ALL) {
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

struct lconv *localeconv(void)
{
    return &current_conventions;
}

int __iris_ctype_latin1_enabled(void)
{
    return current_locale == latin1_locale;
}