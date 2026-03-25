#include <string.h>

#include <userland/applications/games/craft/upstream/src/db.h>
#include <userland/modules/include/fs.h>

#define CRAFT_DB_MAGIC 0x43524442u
#define CRAFT_DB_VERSION 2u
#define CRAFT_DB_MAX_BLOCKS 8192
#define CRAFT_DB_MAX_LIGHTS 4096
#define CRAFT_DB_MAX_SIGNS 1024
#define CRAFT_DB_MAX_KEYS 2048
#define CRAFT_DB_MAX_AUTHS 8

struct craft_db_block_record {
    int p;
    int q;
    int x;
    int y;
    int z;
    int w;
};

struct craft_db_sign_record {
    int p;
    int q;
    int x;
    int y;
    int z;
    int face;
    char text[MAX_SIGN_LENGTH];
};

struct craft_db_key_record {
    int p;
    int q;
    int key;
};

struct craft_db_auth_record {
    int used;
    int selected;
    char username[64];
    char identity_token[128];
};

struct craft_db_image {
    unsigned int magic;
    unsigned int version;
    int state_valid;
    float x;
    float y;
    float z;
    float rx;
    float ry;
    int block_count;
    int light_count;
    int sign_count;
    int key_count;
    struct craft_db_block_record blocks[CRAFT_DB_MAX_BLOCKS];
    struct craft_db_block_record lights[CRAFT_DB_MAX_LIGHTS];
    struct craft_db_sign_record signs[CRAFT_DB_MAX_SIGNS];
    struct craft_db_key_record keys[CRAFT_DB_MAX_KEYS];
    struct craft_db_auth_record auths[CRAFT_DB_MAX_AUTHS];
};

static int g_db_enabled = 0;
static int g_db_loaded = 0;
static char g_db_path[256];
static struct craft_db_image g_db;

static void craft_db_reset(void) {
    memset(&g_db, 0, sizeof(g_db));
    g_db.magic = CRAFT_DB_MAGIC;
    g_db.version = CRAFT_DB_VERSION;
}

static int craft_db_write_file(void) {
    if (!g_db_path[0]) {
        return 0;
    }
    return fs_write_bytes(g_db_path, (const uint8_t *)&g_db, (int)sizeof(g_db));
}

static void craft_db_load_file(const char *path) {
    struct craft_db_image disk;
    int node;
    int got;

    craft_db_reset();
    if (!path || !path[0]) {
        return;
    }
    node = fs_resolve(path);
    if (node < 0) {
        return;
    }
    got = fs_read_node_bytes(node, 0, &disk, (int)sizeof(disk));
    if (got != (int)sizeof(disk)) {
        return;
    }
    if (disk.magic != CRAFT_DB_MAGIC || disk.version != CRAFT_DB_VERSION) {
        return;
    }
    g_db = disk;
}

static int craft_db_find_block(struct craft_db_block_record *records, int count,
                               int p, int q, int x, int y, int z) {
    for (int i = 0; i < count; ++i) {
        if (records[i].p == p && records[i].q == q &&
            records[i].x == x && records[i].y == y && records[i].z == z) {
            return i;
        }
    }
    return -1;
}

static void craft_db_upsert_block(struct craft_db_block_record *records, int *count,
                                  int limit, int p, int q, int x, int y, int z, int w) {
    int index = craft_db_find_block(records, *count, p, q, x, y, z);
    if (w == 0) {
        if (index >= 0) {
            records[index] = records[*count - 1];
            *count -= 1;
        }
        return;
    }
    if (index >= 0) {
        records[index].w = w;
        return;
    }
    if (*count >= limit) {
        return;
    }
    records[*count].p = p;
    records[*count].q = q;
    records[*count].x = x;
    records[*count].y = y;
    records[*count].z = z;
    records[*count].w = w;
    *count += 1;
}

static int craft_db_find_sign(int x, int y, int z, int face) {
    for (int i = 0; i < g_db.sign_count; ++i) {
        if (g_db.signs[i].x == x && g_db.signs[i].y == y &&
            g_db.signs[i].z == z && g_db.signs[i].face == face) {
            return i;
        }
    }
    return -1;
}

static int craft_db_find_key(int p, int q) {
    for (int i = 0; i < g_db.key_count; ++i) {
        if (g_db.keys[i].p == p && g_db.keys[i].q == q) {
            return i;
        }
    }
    return -1;
}

void db_enable() { g_db_enabled = 1; }
void db_disable() { g_db_enabled = 0; }
int get_db_enabled() { return g_db_enabled; }

int db_init(char *path) {
    if (path) {
        strncpy(g_db_path, path, sizeof(g_db_path) - 1);
        g_db_path[sizeof(g_db_path) - 1] = '\0';
    } else {
        g_db_path[0] = '\0';
    }
    if (g_db_path[0] && fs_resolve(g_db_path) < 0) {
        (void)fs_create(g_db_path, 0);
    }
    craft_db_load_file(g_db_path);
    g_db_loaded = 1;
    return 0;
}

void db_close() {
    if (g_db_loaded) {
        (void)craft_db_write_file();
    }
    g_db_loaded = 0;
}

void db_commit() {
    if (g_db_loaded) {
        (void)craft_db_write_file();
    }
}

void db_auth_set(char *username, char *identity_token) {
    int free_slot = -1;
    for (int i = 0; i < CRAFT_DB_MAX_AUTHS; ++i) {
        if (g_db.auths[i].used && strcmp(g_db.auths[i].username, username) == 0) {
            strncpy(g_db.auths[i].identity_token, identity_token, sizeof(g_db.auths[i].identity_token) - 1);
            g_db.auths[i].identity_token[sizeof(g_db.auths[i].identity_token) - 1] = '\0';
            g_db.auths[i].selected = 1;
            for (int j = 0; j < CRAFT_DB_MAX_AUTHS; ++j) {
                if (j != i) g_db.auths[j].selected = 0;
            }
            return;
        }
        if (!g_db.auths[i].used && free_slot < 0) {
            free_slot = i;
        }
    }
    if (free_slot < 0) {
        return;
    }
    g_db.auths[free_slot].used = 1;
    g_db.auths[free_slot].selected = 1;
    strncpy(g_db.auths[free_slot].username, username, sizeof(g_db.auths[free_slot].username) - 1);
    g_db.auths[free_slot].username[sizeof(g_db.auths[free_slot].username) - 1] = '\0';
    strncpy(g_db.auths[free_slot].identity_token, identity_token, sizeof(g_db.auths[free_slot].identity_token) - 1);
    g_db.auths[free_slot].identity_token[sizeof(g_db.auths[free_slot].identity_token) - 1] = '\0';
    for (int i = 0; i < CRAFT_DB_MAX_AUTHS; ++i) {
        if (i != free_slot) g_db.auths[i].selected = 0;
    }
}

int db_auth_select(char *username) {
    for (int i = 0; i < CRAFT_DB_MAX_AUTHS; ++i) {
        if (g_db.auths[i].used && strcmp(g_db.auths[i].username, username) == 0) {
            for (int j = 0; j < CRAFT_DB_MAX_AUTHS; ++j) {
                g_db.auths[j].selected = (j == i);
            }
            return 1;
        }
    }
    return 0;
}

void db_auth_select_none() {
    for (int i = 0; i < CRAFT_DB_MAX_AUTHS; ++i) {
        g_db.auths[i].selected = 0;
    }
}

int db_auth_get(char *username, char *identity_token, int identity_token_length) {
    for (int i = 0; i < CRAFT_DB_MAX_AUTHS; ++i) {
        if (g_db.auths[i].used && strcmp(g_db.auths[i].username, username) == 0) {
            strncpy(identity_token, g_db.auths[i].identity_token, identity_token_length - 1);
            identity_token[identity_token_length - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

int db_auth_get_selected(char *username, int username_length, char *identity_token, int identity_token_length) {
    for (int i = 0; i < CRAFT_DB_MAX_AUTHS; ++i) {
        if (g_db.auths[i].used && g_db.auths[i].selected) {
            strncpy(username, g_db.auths[i].username, username_length - 1);
            username[username_length - 1] = '\0';
            strncpy(identity_token, g_db.auths[i].identity_token, identity_token_length - 1);
            identity_token[identity_token_length - 1] = '\0';
            return 1;
        }
    }
    return 0;
}

void db_save_state(float x, float y, float z, float rx, float ry) {
    g_db.state_valid = 1;
    g_db.x = x;
    g_db.y = y;
    g_db.z = z;
    g_db.rx = rx;
    g_db.ry = ry;
}

int db_load_state(float *x, float *y, float *z, float *rx, float *ry) {
    if (!g_db.state_valid) {
        return 0;
    }
    if (x) *x = g_db.x;
    if (y) *y = g_db.y;
    if (z) *z = g_db.z;
    if (rx) *rx = g_db.rx;
    if (ry) *ry = g_db.ry;
    return 1;
}

void db_insert_block(int p, int q, int x, int y, int z, int w) {
    craft_db_upsert_block(g_db.blocks, &g_db.block_count, CRAFT_DB_MAX_BLOCKS, p, q, x, y, z, w);
}

void db_insert_light(int p, int q, int x, int y, int z, int w) {
    craft_db_upsert_block(g_db.lights, &g_db.light_count, CRAFT_DB_MAX_LIGHTS, p, q, x, y, z, w);
}

void db_insert_sign(int p, int q, int x, int y, int z, int face, const char *text) {
    int index = craft_db_find_sign(x, y, z, face);
    if (index < 0) {
        if (g_db.sign_count >= CRAFT_DB_MAX_SIGNS) {
            return;
        }
        index = g_db.sign_count++;
    }
    g_db.signs[index].p = p;
    g_db.signs[index].q = q;
    g_db.signs[index].x = x;
    g_db.signs[index].y = y;
    g_db.signs[index].z = z;
    g_db.signs[index].face = face;
    strncpy(g_db.signs[index].text, text ? text : "", sizeof(g_db.signs[index].text) - 1);
    g_db.signs[index].text[sizeof(g_db.signs[index].text) - 1] = '\0';
}

void db_delete_sign(int x, int y, int z, int face) {
    int index = craft_db_find_sign(x, y, z, face);
    if (index >= 0) {
        g_db.signs[index] = g_db.signs[g_db.sign_count - 1];
        g_db.sign_count -= 1;
    }
}

void db_delete_signs(int x, int y, int z) {
    for (int i = g_db.sign_count - 1; i >= 0; --i) {
        if (g_db.signs[i].x == x && g_db.signs[i].y == y && g_db.signs[i].z == z) {
            g_db.signs[i] = g_db.signs[g_db.sign_count - 1];
            g_db.sign_count -= 1;
        }
    }
}

void db_delete_all_signs() {
    g_db.sign_count = 0;
}

void db_load_blocks(Map *map, int p, int q) {
    for (int i = 0; i < g_db.block_count; ++i) {
        if (g_db.blocks[i].p == p && g_db.blocks[i].q == q) {
            map_set(map, g_db.blocks[i].x, g_db.blocks[i].y, g_db.blocks[i].z, g_db.blocks[i].w);
        }
    }
}

void db_load_lights(Map *map, int p, int q) {
    for (int i = 0; i < g_db.light_count; ++i) {
        if (g_db.lights[i].p == p && g_db.lights[i].q == q) {
            map_set(map, g_db.lights[i].x, g_db.lights[i].y, g_db.lights[i].z, g_db.lights[i].w);
        }
    }
}

void db_load_signs(SignList *list, int p, int q) {
    for (int i = 0; i < g_db.sign_count; ++i) {
        if (g_db.signs[i].p == p && g_db.signs[i].q == q) {
            sign_list_add(list, g_db.signs[i].x, g_db.signs[i].y, g_db.signs[i].z,
                          g_db.signs[i].face, g_db.signs[i].text);
        }
    }
}

int db_get_key(int p, int q) {
    int index = craft_db_find_key(p, q);
    return index >= 0 ? g_db.keys[index].key : 0;
}

void db_set_key(int p, int q, int key) {
    int index = craft_db_find_key(p, q);
    if (key == 0) {
        if (index >= 0) {
            g_db.keys[index] = g_db.keys[g_db.key_count - 1];
            g_db.key_count -= 1;
        }
        return;
    }
    if (index < 0) {
        if (g_db.key_count >= CRAFT_DB_MAX_KEYS) {
            return;
        }
        index = g_db.key_count++;
        g_db.keys[index].p = p;
        g_db.keys[index].q = q;
    }
    g_db.keys[index].key = key;
}

void db_worker_start() {}
void db_worker_stop() {}
int db_worker_run(void *arg) { (void)arg; return 0; }
