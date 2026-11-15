#ifndef IRIS_LOCALE_H
#define IRIS_LOCALE_H

#define LC_CTYPE 0
#define LC_ALL 1

char *setlocale(int category, const char *locale);

#endif