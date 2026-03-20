#include <string.h>

#include <userland/applications/games/craft/upstream/src/auth.h>

int get_access_token(char *result, int length, char *username, char *identity_token) {
    const char *prefix = "offline:";
    size_t pos = 0;

    if (!result || length <= 0) {
        return 0;
    }
    if (!username || !username[0] || !identity_token || !identity_token[0]) {
        result[0] = '\0';
        return 0;
    }

    while (prefix[pos] && pos + 1 < (size_t)length) {
        result[pos] = prefix[pos];
        pos += 1u;
    }
    for (size_t i = 0; username[i] && pos + 1 < (size_t)length; ++i) {
        result[pos++] = username[i];
    }
    if (pos + 1 < (size_t)length) {
        result[pos++] = ':';
    }
    for (size_t i = 0; identity_token[i] && pos + 1 < (size_t)length; ++i) {
        result[pos++] = identity_token[i];
    }
    result[pos] = '\0';
    return 1;
}
