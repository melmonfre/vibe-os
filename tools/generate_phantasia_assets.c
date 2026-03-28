#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef LOGIN_NAME_MAX
#define LOGIN_NAME_MAX 32
#endif

#include "compat/games/phantasia/phantdefs.h"
#include "compat/games/phantasia/phantstruct.h"

static void trim_trailing_spaces(char *text) {
    size_t len = strlen(text);

    while (len > 0u && text[len - 1u] == ' ') {
        text[--len] = '\0';
    }
}

static int write_i386_monster(FILE *out, const struct monster *monster) {
    static const unsigned char zero_pad[2] = {0u, 0u};

    return fwrite(&monster->m_strength, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_brains, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_speed, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_energy, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_experience, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_flock, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_o_strength, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_o_speed, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_maxspeed, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_o_energy, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_melee, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_skirmish, sizeof(double), 1u, out) == 1u &&
           fwrite(&monster->m_treasuretype, sizeof(int), 1u, out) == 1u &&
           fwrite(&monster->m_type, sizeof(int), 1u, out) == 1u &&
           fwrite(monster->m_name, sizeof(monster->m_name), 1u, out) == 1u &&
           fwrite(zero_pad, sizeof(zero_pad), 1u, out) == 1u;
}

static int write_i386_energyvoid(FILE *out, double x, double y, unsigned char active) {
    static const unsigned char zero_pad[3] = {0u, 0u, 0u};

    return fwrite(&x, sizeof(double), 1u, out) == 1u &&
           fwrite(&y, sizeof(double), 1u, out) == 1u &&
           fwrite(&active, sizeof(active), 1u, out) == 1u &&
           fwrite(zero_pad, sizeof(zero_pad), 1u, out) == 1u;
}

int main(int argc, char **argv) {
    FILE *input = NULL;
    FILE *monsters = NULL;
    FILE *energy_void = NULL;
    char line[256];

    if (argc != 4) {
        fprintf(stderr, "usage: %s <monsters.asc> <monsters.bin> <void.bin>\n", argv[0]);
        return 1;
    }

    input = fopen(argv[1], "r");
    monsters = fopen(argv[2], "wb");
    energy_void = fopen(argv[3], "wb");
    if (!input || !monsters || !energy_void) {
        perror("generate_phantasia_assets");
        if (input) {
            fclose(input);
        }
        if (monsters) {
            fclose(monsters);
        }
        if (energy_void) {
            fclose(energy_void);
        }
        return 1;
    }

    while (fgets(line, sizeof(line), input) != NULL) {
        struct monster monster;
        char name_buf[25];

        memset(&monster, 0, sizeof(monster));
        memcpy(name_buf, line, 24u);
        name_buf[24] = '\0';
        trim_trailing_spaces(name_buf);
        strncpy(monster.m_name, name_buf, sizeof(monster.m_name) - 1u);
        if (sscanf(&line[24], "%lf%lf%lf%lf%lf%d%d%lf",
                   &monster.m_strength, &monster.m_brains,
                   &monster.m_speed, &monster.m_energy,
                   &monster.m_experience, &monster.m_treasuretype,
                   &monster.m_type, &monster.m_flock) != 8) {
            fprintf(stderr, "generate_phantasia_assets: malformed line: %s", line);
            fclose(input);
            fclose(monsters);
            fclose(energy_void);
            return 1;
        }
        if (!write_i386_monster(monsters, &monster)) {
            perror("generate_phantasia_assets");
            fclose(input);
            fclose(monsters);
            fclose(energy_void);
            return 1;
        }
    }

    if (!write_i386_energyvoid(energy_void, 314159.0, -271828.0, 1u)) {
        perror("generate_phantasia_assets");
        fclose(input);
        fclose(monsters);
        fclose(energy_void);
        return 1;
    }

    fclose(input);
    fclose(monsters);
    fclose(energy_void);
    return 0;
}
