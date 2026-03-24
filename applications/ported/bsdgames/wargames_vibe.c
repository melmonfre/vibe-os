#include "vibe_bsdgame_shim.h"

static const char *const g_wargames_known_games[] = {
    "adventure", "arithmetic", "atc", "backgammon", "banner", "battlestar",
    "bcd", "boggle", "bs", "caesar", "canfield", "cribbage", "factor",
    "fish", "fortune", "gomoku", "grdc", "hack", "hangman",
    "mille", "monop", "morse", "number", "phantasia", "pig", "pom", "ppt",
    "primes", "quiz", "rain", "random", "robots", "sail", "snake-bsd",
    "teachgammon", "tetris-bsd", "trek", "worm", "worms", "wump", 0
};

static const char *wargames_basename(const char *text) {
    const char *last = text;

    while (text && *text) {
        if (*text == '/') {
            last = text + 1;
        }
        ++text;
    }
    return last;
}

static int wargames_is_known_game(const char *name) {
    for (int i = 0; g_wargames_known_games[i]; ++i) {
        if (strcmp(name, g_wargames_known_games[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    char line[128];
    char name[64];
    const char *token;
    size_t offset = 0u;
    size_t name_len = 0u;

    printf("Would you like to play a game? ");
    if (!fgets(line, sizeof(line), stdin)) {
        return 0;
    }

    while (line[offset] == ' ' || line[offset] == '\t') {
        ++offset;
    }
    token = wargames_basename(line + offset);
    while (*token != '\0' && *token != ' ' && *token != '\t' && *token != '\n' &&
           name_len + 1u < sizeof(name)) {
        name[name_len++] = *token;
        ++token;
    }
    name[name_len] = '\0';

    if (wargames_is_known_game(name)) {
        printf("\nLaunch `%s` diretamente no terminal do VibeOS.\n", name);
    } else {
        puts("A strange game.");
        puts("The only winning move is not to play.");
    }
    return 0;
}
