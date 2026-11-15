#include <ctype.h>

int __iris_ctype_latin1_enabled(void);

static int is_ascii_letter(int character)
{
    return (character >= 'A' && character <= 'Z') ||
        (character >= 'a' && character <= 'z');
}

static int is_latin1_letter(int character)
{
    return (character >= 0xc0 && character <= 0xd6) ||
        (character >= 0xd8 && character <= 0xf6) ||
        (character >= 0xf8 && character <= 0xff);
}

int isalnum(int character)
{
    return isalpha(character) || isdigit(character);
}

int isalpha(int character)
{
    return is_ascii_letter(character) ||
        (__iris_ctype_latin1_enabled() && is_latin1_letter(character));
}

int isblank(int character)
{
    return character == ' ' || character == '\t' ||
        (__iris_ctype_latin1_enabled() && character == 0xa0);
}

int iscntrl(int character)
{
    return (character >= 0 && character <= 0x1f) || character == 0x7f ||
        (__iris_ctype_latin1_enabled() &&
            character >= 0x80 && character <= 0x9f);
}

int isdigit(int character)
{
    return character >= '0' && character <= '9';
}

int isgraph(int character)
{
    return (character >= 0x21 && character <= 0x7e) ||
        (__iris_ctype_latin1_enabled() &&
            character >= 0xa1 && character <= 0xff);
}

int islower(int character)
{
    return (character >= 'a' && character <= 'z') ||
        (__iris_ctype_latin1_enabled() &&
            ((character >= 0xdf && character <= 0xf6) ||
                (character >= 0xf8 && character <= 0xff)));
}

int isprint(int character)
{
    return (character >= 0x20 && character <= 0x7e) ||
        (__iris_ctype_latin1_enabled() &&
            character >= 0xa0 && character <= 0xff);
}

int ispunct(int character)
{
    return isgraph(character) && !isalnum(character);
}

int isspace(int character)
{
    return character == ' ' || (character >= '\t' && character <= '\r') ||
        (__iris_ctype_latin1_enabled() && character == 0xa0);
}

int isupper(int character)
{
    return (character >= 'A' && character <= 'Z') ||
        (__iris_ctype_latin1_enabled() &&
            ((character >= 0xc0 && character <= 0xd6) ||
                (character >= 0xd8 && character <= 0xde)));
}

int isxdigit(int character)
{
    return isdigit(character) ||
        (character >= 'A' && character <= 'F') ||
        (character >= 'a' && character <= 'f');
}

int tolower(int character)
{
    if (character >= 'A' && character <= 'Z') {
        return character + ('a' - 'A');
    }
    if (__iris_ctype_latin1_enabled() && isupper(character)) {
        return character + 0x20;
    }
    return character;
}

int toupper(int character)
{
    if (character >= 'a' && character <= 'z') {
        return character - ('a' - 'A');
    }
    if (__iris_ctype_latin1_enabled() &&
        character >= 0xe0 && character <= 0xfe && character != 0xf7) {
        return character - 0x20;
    }
    return character;
}