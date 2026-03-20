#include <stdlib.h>
#include <string.h>

#include <userland/applications/games/craft/upstream/src/client.h>

#define CRAFT_CLIENT_QUEUE_MAX 32
#define CRAFT_CLIENT_LINE_MAX 256

static int g_client_enabled = 0;
static int g_client_running = 0;
static char g_client_host[64];
static int g_client_port = 0;
static char g_client_queue[CRAFT_CLIENT_QUEUE_MAX][CRAFT_CLIENT_LINE_MAX];
static int g_client_queue_count = 0;

static void craft_client_queue_push(const char *line) {
    if (!line || !line[0] || g_client_queue_count >= CRAFT_CLIENT_QUEUE_MAX) {
        return;
    }
    strncpy(g_client_queue[g_client_queue_count], line, CRAFT_CLIENT_LINE_MAX - 1);
    g_client_queue[g_client_queue_count][CRAFT_CLIENT_LINE_MAX - 1] = '\0';
    g_client_queue_count += 1;
}

static void craft_client_append(char *dst, size_t dst_size, const char *src) {
    size_t len = strlen(dst);
    size_t i = 0;
    if (len >= dst_size) {
        return;
    }
    while (src && src[i] && len + i + 1 < dst_size) {
        dst[len + i] = src[i];
        i += 1;
    }
    dst[len + i] = '\0';
}

static char *craft_client_queue_flush(void) {
    size_t total = 1;
    char *buffer;
    size_t offset = 0;

    if (g_client_queue_count == 0) {
        return 0;
    }
    for (int i = 0; i < g_client_queue_count; ++i) {
        total += strlen(g_client_queue[i]) + 1;
    }
    buffer = (char *)malloc(total);
    if (!buffer) {
        g_client_queue_count = 0;
        return 0;
    }
    for (int i = 0; i < g_client_queue_count; ++i) {
        size_t len = strlen(g_client_queue[i]);
        memcpy(buffer + offset, g_client_queue[i], len);
        offset += len;
        buffer[offset++] = '\n';
    }
    buffer[offset] = '\0';
    g_client_queue_count = 0;
    return buffer;
}

void client_enable() { g_client_enabled = 1; }
void client_disable() { g_client_enabled = 0; g_client_running = 0; g_client_queue_count = 0; }
int get_client_enabled() { return g_client_enabled; }

void client_connect(char *hostname, int port) {
    strncpy(g_client_host, hostname ? hostname : "offline", sizeof(g_client_host) - 1);
    g_client_host[sizeof(g_client_host) - 1] = '\0';
    g_client_port = port;
}

void client_start() {
    g_client_running = 1;
    craft_client_queue_push("T,Offline client active");
}

void client_stop() {
    g_client_running = 0;
    g_client_queue_count = 0;
}

void client_send(char *data) {
    (void)data;
}

char *client_recv() {
    if (!g_client_running) {
        return 0;
    }
    return craft_client_queue_flush();
}

void client_version(int version) {
    (void)version;
}

void client_login(const char *username, const char *identity_token) {
    char line[CRAFT_CLIENT_LINE_MAX];
    (void)identity_token;
    if (username && username[0]) {
        strncpy(line, "T,Logged in offline as ", sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        craft_client_append(line, sizeof(line), username);
        craft_client_queue_push(line);
    }
}

void client_position(float x, float y, float z, float rx, float ry) {
    (void)x; (void)y; (void)z; (void)rx; (void)ry;
}

void client_chunk(int p, int q, int key) {
    (void)p; (void)q; (void)key;
}

void client_block(int x, int y, int z, int w) {
    (void)x; (void)y; (void)z; (void)w;
}

void client_light(int x, int y, int z, int w) {
    (void)x; (void)y; (void)z; (void)w;
}

void client_sign(int x, int y, int z, int face, const char *text) {
    (void)x; (void)y; (void)z; (void)face; (void)text;
}

void client_talk(const char *text) {
    char line[CRAFT_CLIENT_LINE_MAX];
    if (!text || !text[0]) {
        return;
    }
    strncpy(line, "T,", sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';
    craft_client_append(line, sizeof(line), text);
    craft_client_queue_push(line);
}
