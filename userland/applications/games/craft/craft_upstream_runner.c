#include <stdlib.h>
#include <string.h>
#include <userland/modules/include/syscalls.h>
#include <userland/modules/include/utils.h>

int doom_printf(const char *fmt, ...);
int doom_snprintf(char *str, size_t size, const char *fmt, ...);
int doom_sscanf(const char *str, const char *fmt, ...);

float floorf(float x);
float roundf(float x);
float powf(float x, float y);
float cosf(float x);
float sinf(float x);
float atan2f(float y, float x);
float sqrtf(float x);
void craft_glfw_inject_key(int raw);
void craft_glfw_set_mouse_state(int x, int y, int dx, int dy,
                                int wheel, uint8_t buttons, int focused, int inside);
void craft_glfw_set_window_size(int width, int height);
void craft_glfw_request_close(void);
void craft_glfw_reset_embedded(void);
void craft_gl_blit_to(int x, int y, int width, int height);
void craft_gl_reset_objects(void);
void craft_gl_debug_frame_reset(void);
void craft_gl_debug_frame_report(void);
void craft_thread_pump(int max_jobs);
void craft_thread_reset(void);
void craft_thread_set_parallel_workers(int workers);
int craft_runtime_chunk_preload_budget(void);

static unsigned int g_craft_runner_rng_state = 1u;

static void craft_debug_stage(const char *message) {
    static int g_craft_debug_budget = 128;

    if (!message || !message[0] || g_craft_debug_budget <= 0) {
        return;
    }
    g_craft_debug_budget -= 1;
    sys_write_debug(message);
    sys_write_debug("\n");
}

static void craft_debug_metrics(const char *label,
                                int a, int b, int c, int d) {
    static int g_craft_metrics_budget = 24;
    char line[128];

    if (!label || g_craft_metrics_budget <= 0) {
        return;
    }
    g_craft_metrics_budget -= 1;
    doom_snprintf(line, sizeof(line), "%s %d %d %d %d\n", label, a, b, c, d);
    sys_write_debug(line);
}

static char *craft_runner_strncat(char *dest, const char *src, size_t n) {
    size_t dest_len = strlen(dest);
    size_t i = 0u;
    while (i < n && src[i] != '\0') {
        dest[dest_len + i] = src[i];
        i += 1u;
    }
    dest[dest_len + i] = '\0';
    return dest;
}

static int craft_runner_atoi(const char *nptr) {
    int sign = 1;
    int value = 0;
    if (!nptr) {
        return 0;
    }
    while (*nptr == ' ' || *nptr == '\t' || *nptr == '\n' || *nptr == '\r') {
        nptr++;
    }
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    while (*nptr >= '0' && *nptr <= '9') {
        value = value * 10 + (*nptr - '0');
        nptr++;
    }
    return value * sign;
}

static void craft_runner_srand(unsigned int seed) {
    g_craft_runner_rng_state = seed ? seed : 1u;
}

static int craft_runner_rand(void) {
    g_craft_runner_rng_state = g_craft_runner_rng_state * 1103515245u + 12345u;
    return (int)((g_craft_runner_rng_state >> 16) & 0x7fffu);
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#define printf doom_printf
#define snprintf doom_snprintf
#define sscanf doom_sscanf
#define strncat craft_runner_strncat
#define atoi craft_runner_atoi
#define srand craft_runner_srand
#define rand craft_runner_rand
#define main craft_upstream_main
#include "upstream/src/main.c"
#undef main
#undef rand
#undef srand
#undef atoi
#undef strncat
#undef sscanf
#undef snprintf
#undef printf

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

struct craft_runtime_state {
    int bootstrapped;
    int session_active;
    int fullscreen;
    int compat_visual_tier;
    int enable_signs;
    int enable_players;
    int enable_item;
    int enable_observe2;
    int hud_interval;
    int player_buffer_valid;
    int target_create_radius;
    int target_render_radius;
    int target_delete_radius;
    int target_sign_radius;
    int target_preload_budget;
    int target_worker_limit;
    int warmup_frames;
    int memory_poll_frames;
    uint32_t memory_pressure_level;
    uint32_t memory_working_set_bytes;
    uint32_t memory_opportunistic_bytes;
    GLuint sky_buffer;
    FPS fps;
    float player_buffer_x;
    float player_buffer_y;
    float player_buffer_z;
    float player_buffer_rx;
    float player_buffer_ry;
    double previous;
    double last_commit;
    double last_update;
    double smoothed_frame_ms;
};

static struct craft_runtime_state g_runtime;
static Attrib g_block_attrib;
static Attrib g_line_attrib;
static Attrib g_text_attrib;
static Attrib g_sky_attrib;
// static Attrib block_attrib;
// static Attrib line_attrib;
// static Attrib text_attrib;
// static Attrib sky_attrib;

void craft_upstream_resize(int width, int height);
static void craft_sync_quality_profile(void);

static float craft_runner_absf(float value) {
    return value < 0.0f ? -value : value;
}

static int craft_runner_min_int(int a, int b) {
    return a < b ? a : b;
}

static int craft_runner_max_int(int a, int b) {
    return a > b ? a : b;
}

static void craft_refresh_memory_budget(int force) {
    struct mk_memory_status status;
    struct mk_memory_budget budget;
    int preload_budget = 5;
    int worker_limit = 2;
    int delete_radius = 1;

    if (!force && g_runtime.memory_poll_frames > 0) {
        g_runtime.memory_poll_frames -= 1;
        return;
    }

    memset(&status, 0, sizeof(status));
    memset(&budget, 0, sizeof(budget));
    if (sys_memory_status(&status) < 0 || sys_memory_budget(&budget) < 0 ||
        status.abi_version != MK_MEMORY_STATUS_ABI_VERSION ||
        budget.abi_version != MK_MEMORY_BUDGET_ABI_VERSION) {
        g_runtime.target_preload_budget = 5;
        g_runtime.target_worker_limit = 2;
        g_runtime.memory_pressure_level = MK_MEMORY_PRESSURE_NORMAL;
        g_runtime.memory_working_set_bytes = 0;
        g_runtime.memory_opportunistic_bytes = 0;
        craft_thread_set_parallel_workers(g_runtime.target_worker_limit);
        g_runtime.memory_poll_frames = 32;
        return;
    }

    g_runtime.memory_pressure_level = budget.pressure_level;
    g_runtime.memory_working_set_bytes = budget.working_set_target_bytes;
    g_runtime.memory_opportunistic_bytes = budget.opportunistic_target_bytes;

    if (budget.absolute_ceiling_bytes >= 0x03000000u &&
        budget.pressure_level <= MK_MEMORY_PRESSURE_ELEVATED) {
        preload_budget = 9;
        worker_limit = 4;
        delete_radius = 2;
    }
    else if (budget.absolute_ceiling_bytes >= 0x02000000u &&
             budget.pressure_level <= MK_MEMORY_PRESSURE_HIGH) {
        preload_budget = 7;
        worker_limit = 3;
        delete_radius = 2;
    }
    else if (budget.pressure_level >= MK_MEMORY_PRESSURE_HIGH) {
        preload_budget = 3;
        worker_limit = 1;
        delete_radius = 1;
    }

    if (budget.concurrency_hint != 0u) {
        worker_limit = craft_runner_min_int(worker_limit, (int)budget.concurrency_hint);
    }
    worker_limit = craft_runner_max_int(worker_limit, 1);
    g_runtime.target_preload_budget = preload_budget;
    g_runtime.target_worker_limit = worker_limit;
    g_runtime.target_delete_radius = delete_radius;
    craft_thread_set_parallel_workers(worker_limit);
    g_runtime.memory_poll_frames = 32;
}

int craft_runtime_chunk_preload_budget(void) {
    if (g_runtime.target_preload_budget > 0) {
        return g_runtime.target_preload_budget;
    }
    return 5;
}

static int craft_runner_local_player_visible(void) {
    return g->observe1 != 0 || g->observe2 != 0;
}

static void craft_runner_drop_local_player_buffer(Player *player) {
    if (player && player->buffer) {
        del_buffer(player->buffer);
        player->buffer = 0;
    }
    g_runtime.player_buffer_valid = 0;
}

static void craft_runner_refresh_local_player_buffer(Player *player) {
    State *s;

    if (!player) {
        return;
    }
    if (!craft_runner_local_player_visible()) {
        craft_runner_drop_local_player_buffer(player);
        return;
    }

    s = &player->state;
    if (!g_runtime.player_buffer_valid ||
        craft_runner_absf(s->x - g_runtime.player_buffer_x) > 0.05f ||
        craft_runner_absf(s->y - g_runtime.player_buffer_y) > 0.05f ||
        craft_runner_absf(s->z - g_runtime.player_buffer_z) > 0.05f ||
        craft_runner_absf(s->rx - g_runtime.player_buffer_rx) > 0.01f ||
        craft_runner_absf(s->ry - g_runtime.player_buffer_ry) > 0.01f)
    {
        del_buffer(player->buffer);
        player->buffer = gen_player_buffer(s->x, s->y, s->z, s->rx, s->ry);
        g_runtime.player_buffer_x = s->x;
        g_runtime.player_buffer_y = s->y;
        g_runtime.player_buffer_z = s->z;
        g_runtime.player_buffer_rx = s->rx;
        g_runtime.player_buffer_ry = s->ry;
        g_runtime.player_buffer_valid = 1;
    }
}

static void craft_apply_runtime_visual_profile(int width, int height) {
    int area = width * height;
    int tier = 2;

    craft_refresh_memory_budget(0);
    if (g_runtime.smoothed_frame_ms > 45.0) {
        tier = 0;
    } else if (g_runtime.smoothed_frame_ms > 30.0 || area > 480 * 360) {
        tier = 1;
    }

    g_runtime.compat_visual_tier = tier;
    g_runtime.enable_signs = 0;
    g_runtime.enable_item = 0;
    g_runtime.enable_observe2 = 0;
    g_runtime.enable_players = (tier >= 2);
    g_runtime.hud_interval = (tier <= 0) ? 8 : ((tier == 1) ? 4 : 2);

    if (tier <= 0) {
        g_runtime.target_create_radius = 1;
        g_runtime.target_render_radius = 1;
        if (g_runtime.memory_pressure_level >= MK_MEMORY_PRESSURE_HIGH) {
            g_runtime.target_delete_radius = 1;
        }
        g_runtime.target_sign_radius = 0;
    } else {
        g_runtime.target_create_radius = 1;
        g_runtime.target_render_radius = 1;
        if (g_runtime.memory_pressure_level >= MK_MEMORY_PRESSURE_HIGH) {
            g_runtime.target_delete_radius = 1;
        }
        g_runtime.target_sign_radius = 0;
    }
    craft_sync_quality_profile();
}

static void craft_render_debug_cube(Player *player) {
    State *s;
    float vx;
    float vy;
    float vz;
    float matrix[16];
    GLuint buffer;

    if (!player) {
        return;
    }
    s = &player->state;
    get_sight_vector(s->rx, s->ry, &vx, &vy, &vz);
    set_matrix_3d(
        matrix, g->width, g->height,
        s->x, s->y, s->z, s->rx, s->ry, g->fov, g->ortho, g->render_radius);
    glUseProgram(g_block_attrib.program);
    glUniformMatrix4fv(g_block_attrib.matrix, 1, GL_FALSE, matrix);
    glUniform3f(g_block_attrib.camera, s->x, s->y, s->z);
    glUniform1i(g_block_attrib.sampler, 0);
    glUniform1i(g_block_attrib.extra1, 2);
    glUniform1f(g_block_attrib.extra2, get_daylight());
    glUniform1f(g_block_attrib.extra3, chunk_fog_distance(g->render_radius));
    glUniform1i(g_block_attrib.extra4, g->ortho);
    glUniform1f(g_block_attrib.timer, time_of_day());

    buffer = gen_cube_buffer(
        s->x + vx * 3.0f,
        s->y + vy * 3.0f,
        s->z + vz * 3.0f,
        0.75f,
        GRASS);
    draw_item(&g_block_attrib, buffer, 36);
    del_buffer(buffer);
}

static void craft_sync_quality_profile(void) {
    int create_radius = 1;
    int render_radius = 1;
    int delete_radius = g_runtime.target_delete_radius > 0 ? g_runtime.target_delete_radius : 1;
    int sign_radius = g_runtime.target_sign_radius;

    if (g_runtime.session_active) {
        if (g_runtime.warmup_frames <= 0) {
            sign_radius = 0;
        } else if (g_runtime.warmup_frames < 6) {
            sign_radius = 0;
        }
    }

    if (sign_radius > render_radius) {
        sign_radius = render_radius;
    }

    g->create_radius = create_radius;
    g->render_radius = render_radius;
    g->delete_radius = delete_radius;
    g->sign_radius = sign_radius;
}

static void craft_apply_quality_profile(int width, int height, int fullscreen) {
    int create_radius = 1;
    int render_radius = 1;
    int delete_radius = 1;
    int sign_radius = 0;

    (void)width;
    (void)height;
    (void)fullscreen;

    g_runtime.target_create_radius = create_radius;
    g_runtime.target_render_radius = render_radius;
    g_runtime.target_delete_radius = delete_radius;
    g_runtime.target_sign_radius = sign_radius;
    g_runtime.target_preload_budget = 5;
    g_runtime.target_worker_limit = 2;
    g_runtime.smoothed_frame_ms = 33.0;
    g_runtime.player_buffer_valid = 0;
    g_runtime.memory_poll_frames = 0;
    craft_refresh_memory_budget(1);
    craft_sync_quality_profile();
    craft_apply_runtime_visual_profile(width, height);
}

static int craft_upstream_bootstrap(void) {
    GLuint texture;
    GLuint font;
    GLuint sky;
    GLuint sign;
    GLuint program;

    if (g_runtime.bootstrapped) {
        return 0;
    }

    craft_debug_stage("craft: bootstrap begin");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    craft_runner_srand(time(NULL));
    craft_runner_rand();

    if (!glfwInit()) {
        return -1;
    }
    craft_debug_stage("craft: after glfwInit");
    create_window();
    if (!g->window) {
        glfwTerminate();
        return -1;
    }
    craft_debug_stage("craft: after create_window");

    glfwMakeContextCurrent(g->window);
    glfwSwapInterval(0);
    glfwSetInputMode(g->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(g->window, on_key);
    glfwSetCharCallback(g->window, on_char);
    glfwSetMouseButtonCallback(g->window, on_mouse_button);
    glfwSetScrollCallback(g->window, on_scroll);

    if (glewInit() != GLEW_OK) {
        glfwTerminate();
        return -1;
    }
    craft_debug_stage("craft: after glewInit");

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glLogicOp(GL_INVERT);
    glClearColor(0, 0, 0, 1);

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    load_png_texture("textures/texture.png");

    glGenTextures(1, &font);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    load_png_texture("textures/font.png");

    glGenTextures(1, &sky);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sky);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    load_png_texture("textures/sky.png");

    glGenTextures(1, &sign);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, sign);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    load_png_texture("textures/sign.png");
    craft_debug_stage("craft: after textures");

    program = load_program(
        "shaders/block_vertex.glsl", "shaders/block_fragment.glsl");
    g_block_attrib.program = program;
    g_block_attrib.position = glGetAttribLocation(program, "position");
    g_block_attrib.normal = glGetAttribLocation(program, "normal");
    g_block_attrib.uv = glGetAttribLocation(program, "uv");
    g_block_attrib.matrix = glGetUniformLocation(program, "matrix");
    g_block_attrib.sampler = glGetUniformLocation(program, "sampler");
    g_block_attrib.extra1 = glGetUniformLocation(program, "sky_sampler");
    g_block_attrib.extra2 = glGetUniformLocation(program, "daylight");
    g_block_attrib.extra3 = glGetUniformLocation(program, "fog_distance");
    g_block_attrib.extra4 = glGetUniformLocation(program, "ortho");
    g_block_attrib.camera = glGetUniformLocation(program, "camera");
    g_block_attrib.timer = glGetUniformLocation(program, "timer");

    program = load_program(
        "shaders/line_vertex.glsl", "shaders/line_fragment.glsl");
    g_line_attrib.program = program;
    g_line_attrib.position = glGetAttribLocation(program, "position");
    g_line_attrib.matrix = glGetUniformLocation(program, "matrix");

    program = load_program(
        "shaders/text_vertex.glsl", "shaders/text_fragment.glsl");
    g_text_attrib.program = program;
    g_text_attrib.position = glGetAttribLocation(program, "position");
    g_text_attrib.uv = glGetAttribLocation(program, "uv");
    g_text_attrib.matrix = glGetUniformLocation(program, "matrix");
    g_text_attrib.sampler = glGetUniformLocation(program, "sampler");
    g_text_attrib.extra1 = glGetUniformLocation(program, "is_sign");

    program = load_program(
        "shaders/sky_vertex.glsl", "shaders/sky_fragment.glsl");
    g_sky_attrib.program = program;
    g_sky_attrib.position = glGetAttribLocation(program, "position");
    g_sky_attrib.normal = glGetAttribLocation(program, "normal");
    g_sky_attrib.uv = glGetAttribLocation(program, "uv");
    g_sky_attrib.matrix = glGetUniformLocation(program, "matrix");
    g_sky_attrib.sampler = glGetUniformLocation(program, "sampler");
    g_sky_attrib.timer = glGetUniformLocation(program, "timer");
    craft_debug_stage("craft: after shaders");

    g->mode = MODE_OFFLINE;
    doom_snprintf(g->db_path, MAX_PATH_LENGTH, "%s", DB_PATH);
    craft_apply_quality_profile(800, 600, 0);

    craft_debug_stage("craft: before workers");
    for (int i = 0; i < WORKERS; ++i) {
        Worker *worker = g->workers + i;
        worker->index = i;
        worker->state = WORKER_IDLE;
        mtx_init(&worker->mtx, mtx_plain);
        cnd_init(&worker->cnd);
        thrd_create(&worker->thrd, worker_run, worker);
    }
    craft_debug_stage("craft: after workers");

    g_runtime.bootstrapped = 1;
    return 0;
}

static int craft_upstream_begin_session(void) {
    Player *me;
    State *s;
    int loaded;
    int spawn_y;

    if (g_runtime.session_active) {
        return 0;
    }

    craft_debug_stage("craft: begin session");
    if (g->mode == MODE_OFFLINE || USE_CACHE) {
        db_enable();
        if (db_init(g->db_path)) {
            return -1;
        }
        if (g->mode == MODE_ONLINE) {
            db_delete_all_signs();
        }
    }

    if (g->mode == MODE_ONLINE) {
        client_enable();
        client_connect(g->server_addr, g->server_port);
        client_start();
        client_version(1);
        login();
    }

    reset_model();
    memset(&g_runtime.fps, 0, sizeof(g_runtime.fps));
    g_runtime.last_commit = glfwGetTime();
    g_runtime.last_update = glfwGetTime();
    g_runtime.sky_buffer = gen_sky_buffer();
    craft_refresh_memory_budget(1);

    me = g->players;
    s = &g->players->state;
    me->id = 0;
    me->name[0] = '\0';
    me->buffer = 0;
    g->player_count = 1;

    s->x = 0.0f;
    s->y = 0.0f;
    s->z = 0.0f;
    s->rx = 0.0f;
    s->ry = 0.0f;
    loaded = db_load_state(&s->x, &s->y, &s->z, &s->rx, &s->ry);
    craft_debug_stage("craft: after db load");
    if (loaded) {
        if (s->x < -256.0f || s->x > 256.0f || s->z < -256.0f || s->z > 256.0f) {
            s->x = 0.0f;
            s->z = 0.0f;
        }
        if (s->rx < -6.28319f || s->rx > 6.28319f) {
            s->rx = 0.0f;
        }
        if (s->ry < -1.4f || s->ry > 1.4f) {
            s->ry = 0.0f;
        }
    }
    force_chunks_budget(me, 0, 1);
    spawn_y = highest_block(s->x, s->z) + 3;
    if (spawn_y < 3) {
        force_chunks_budget(me, 1, craft_runtime_chunk_preload_budget());
        spawn_y = highest_block(s->x, s->z) + 3;
    }
    if (count_drawable_chunks(chunked(s->x), chunked(s->z), 1) == 0) {
        force_chunks_budget(me, 1, craft_runtime_chunk_preload_budget());
    }
    craft_debug_metrics("craft: bootstrap",
                        g->chunk_count,
                        count_drawable_chunks(chunked(s->x), chunked(s->z), 1),
                        spawn_y,
                        craft_thread_parallel_workers());
    if (!loaded) {
        s->x = 0.0f;
        s->z = 0.0f;
        s->rx = 0.0f;
        s->ry = -0.25f;
        s->y = (float)spawn_y;
    } else {
        if (s->y < 1.0f || s->y > 255.0f || player_intersects_block(2, s->x, s->y, s->z,
                (int)roundf(s->x), (int)roundf(s->y), (int)roundf(s->z))) {
            s->y = (float)spawn_y;
            s->ry = -0.25f;
        }
    }

    g_runtime.previous = glfwGetTime();
    g_runtime.warmup_frames = 0;
    g_runtime.player_buffer_valid = 0;
    g_runtime.session_active = 1;
    craft_sync_quality_profile();
    craft_debug_stage("craft: session ready");
    return 0;
}

static void craft_upstream_end_session(void) {
    State *s;

    if (!g_runtime.session_active) {
        return;
    }

    craft_thread_reset();
    s = &g->players->state;
    db_save_state(s->x, s->y, s->z, s->rx, s->ry);
    db_close();
    db_disable();
    client_stop();
    client_disable();
    del_buffer(g_runtime.sky_buffer);
    g_runtime.sky_buffer = 0;
    delete_all_chunks();
    delete_all_players();
    g_runtime.session_active = 0;
}

int craft_upstream_start(int width, int height) {
    craft_gl_reset_objects();
    if (craft_upstream_bootstrap() != 0) {
        return -1;
    }
    craft_glfw_set_window_size(width, height);
    craft_apply_quality_profile(width, height, g_runtime.fullscreen);
    if (craft_upstream_begin_session() != 0) {
        craft_upstream_end_session();
        return -1;
    }
    return 0;
}

int craft_upstream_frame(void) {
    Player *me;
    Player *player;
    State *s;
    char text_buffer[1024];
    int show_hud;
    float ts;
    float tx;
    float ty;
    int face_count;
    double frame_begin;
    double frame_end;
    double frame_ms;

    if (!g_runtime.bootstrapped) {
        return -1;
    }
    if (!g_runtime.session_active && craft_upstream_begin_session() != 0) {
        return -1;
    }
    frame_begin = glfwGetTime();
    craft_debug_stage("craft: frame begin");
    craft_thread_pump(g_runtime.warmup_frames < 6 ? 4 : 2);

    me = g->players;
    s = &g->players->state;

    g->scale = get_scale_factor();
    glfwGetFramebufferSize(g->window, &g->width, &g->height);
    glViewport(0, 0, g->width, g->height);
    craft_debug_stage("craft: frame viewport");

    if (g->time_changed) {
        g->time_changed = 0;
        g_runtime.last_commit = glfwGetTime();
        g_runtime.last_update = glfwGetTime();
        memset(&g_runtime.fps, 0, sizeof(g_runtime.fps));
    }
    update_fps(&g_runtime.fps);
    {
        double now = glfwGetTime();
        double dt = now - g_runtime.previous;
        dt = MIN(dt, 0.2);
        dt = MAX(dt, 0.0);
        g_runtime.previous = now;

        handle_mouse_input();
        handle_movement(dt);

        {
            char *buffer = client_recv();
            if (buffer) {
                parse_buffer(buffer);
                free(buffer);
            }
        }

        if (now - g_runtime.last_commit > COMMIT_INTERVAL) {
            g_runtime.last_commit = now;
            db_commit();
        }

        if (now - g_runtime.last_update > 0.1) {
            g_runtime.last_update = now;
            client_position(s->x, s->y, s->z, s->rx, s->ry);
        }
    }
    craft_debug_stage("craft: frame simulation");

    g->observe1 = g->observe1 % g->player_count;
    g->observe2 = g->observe2 % g->player_count;
    delete_chunks();
    craft_runner_refresh_local_player_buffer(me);
    for (int i = 1; i < g->player_count; ++i) {
        interpolate_player(g->players + i);
    }
    player = g->players + g->observe1;
    show_hud = (g_runtime.hud_interval <= 1) || ((g_runtime.warmup_frames % g_runtime.hud_interval) == 0);
    craft_debug_stage("craft: frame buffers");

    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);
    render_sky(&g_sky_attrib, player, g_runtime.sky_buffer);
    glClear(GL_DEPTH_BUFFER_BIT);
    craft_debug_stage("craft: frame sky");
    craft_gl_debug_frame_reset();
    face_count = render_chunks(&g_block_attrib, player);
    if (g_runtime.warmup_frames < 8) {
        craft_debug_metrics("craft: frame chunks",
                            g->chunk_count,
                            face_count,
                            count_drawable_chunks(chunked(s->x), chunked(s->z), 1),
                            craft_thread_parallel_workers());
        craft_gl_debug_frame_report();
    }
    if (face_count <= 0) {
        craft_render_debug_cube(player);
    }
    craft_debug_stage("craft: frame chunks");
    if (g_runtime.enable_signs) {
        render_signs(&g_text_attrib, player);
    }
    render_sign(&g_text_attrib, player);
    if (g_runtime.enable_players) {
        render_players(&g_block_attrib, player);
    }
    if (SHOW_WIREFRAME) {
        render_wireframe(&g_line_attrib, player);
    }

    glClear(GL_DEPTH_BUFFER_BIT);
    if (SHOW_CROSSHAIRS) {
        render_crosshairs(&g_line_attrib);
    }
    if (SHOW_ITEM && g_runtime.enable_item) {
        render_item(&g_block_attrib);
    }
    craft_debug_stage("craft: frame world done");

    ts = 12 * g->scale;
    tx = ts / 2;
    ty = g->height - ts;
    if (show_hud && SHOW_INFO_TEXT) {
        int hour = time_of_day() * 24;
        char am_pm = hour < 12 ? 'a' : 'p';
        hour = hour % 12;
        hour = hour ? hour : 12;
        doom_snprintf(
            text_buffer, 1024,
            "(%d, %d) (%.2f, %.2f, %.2f) [%d, %d, %d] %d%cm %dfps",
            chunked(s->x), chunked(s->z), s->x, s->y, s->z,
            g->player_count, g->chunk_count,
            face_count * 2, hour, am_pm, g_runtime.fps.fps);
        render_text(&g_text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);
        ty -= ts * 2;
    }
    if (show_hud && SHOW_CHAT_TEXT) {
        for (int i = 0; i < MAX_MESSAGES; ++i) {
            int index = (g->message_index + i) % MAX_MESSAGES;
            if (strlen(g->messages[index])) {
                render_text(&g_text_attrib, ALIGN_LEFT, tx, ty, ts,
                    g->messages[index]);
                ty -= ts * 2;
            }
        }
    }
    if (g->typing) {
        doom_snprintf(text_buffer, 1024, "> %s", g->typing_buffer);
        render_text(&g_text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);
        ty -= ts * 2;
    }
    if (show_hud && SHOW_PLAYER_NAMES) {
        if (player != me) {
            render_text(&g_text_attrib, ALIGN_CENTER,
                g->width / 2, ts, ts, player->name);
        }
        {
            Player *other = player_crosshair(player);
            if (other) {
                render_text(&g_text_attrib, ALIGN_CENTER,
                    g->width / 2, g->height / 2 - ts - 24, ts,
                    other->name);
            }
        }
    }

    if (g_runtime.enable_observe2 && g->observe2) {
        int pw = 256 * g->scale;
        int ph = 256 * g->scale;
        int offset = 32 * g->scale;
        int pad = 3 * g->scale;
        int sw = pw + pad * 2;
        int sh = ph + pad * 2;

        player = g->players + g->observe2;

        glEnable(GL_SCISSOR_TEST);
        glScissor(g->width - sw - offset + pad, offset - pad, sw, sh);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);
        glViewport(g->width - pw - offset, offset, pw, ph);

        g->width = pw;
        g->height = ph;
        g->ortho = 0;
        g->fov = 65;

        render_sky(&g_sky_attrib, player, g_runtime.sky_buffer);
        glClear(GL_DEPTH_BUFFER_BIT);
        render_chunks(&g_block_attrib, player);
        if (g_runtime.enable_signs) {
            render_signs(&g_text_attrib, player);
        }
        if (g_runtime.enable_players) {
            render_players(&g_block_attrib, player);
        }
        glClear(GL_DEPTH_BUFFER_BIT);
        if (show_hud && SHOW_PLAYER_NAMES) {
            render_text(&g_text_attrib, ALIGN_CENTER,
                pw / 2, ts, ts, player->name);
        }
    }

    glfwSwapBuffers(g->window);
    craft_debug_stage("craft: frame swap");
    glfwPollEvents();
    craft_debug_stage("craft: frame poll");
    if (glfwWindowShouldClose(g->window)) {
        craft_upstream_end_session();
        return 0;
    }
    if (g->mode_changed) {
        g->mode_changed = 0;
        craft_upstream_end_session();
        if (craft_upstream_begin_session() != 0) {
            return -1;
        }
    }
    if (g_runtime.warmup_frames < 16) {
        g_runtime.warmup_frames += 1;
        craft_sync_quality_profile();
    }
    frame_end = glfwGetTime();
    frame_ms = (frame_end - frame_begin) * 1000.0;
    if (frame_ms > 0.0) {
        if (g_runtime.smoothed_frame_ms <= 0.0) {
            g_runtime.smoothed_frame_ms = frame_ms;
        } else {
            g_runtime.smoothed_frame_ms =
                g_runtime.smoothed_frame_ms * 0.85 + frame_ms * 0.15;
        }
        craft_apply_runtime_visual_profile(g->width, g->height);
    }
    return 1;
}

void craft_upstream_stop(void) {
    craft_upstream_end_session();
    craft_gl_reset_objects();
    if (g_runtime.bootstrapped) {
        craft_glfw_request_close();
        glfwTerminate();
        craft_glfw_reset_embedded();
        curl_global_cleanup();
        memset(&g_runtime, 0, sizeof(g_runtime));
    }
}

void craft_upstream_resize(int width, int height) {
    craft_glfw_set_window_size(width, height);
    craft_apply_quality_profile(width, height, g_runtime.fullscreen);
}

void craft_upstream_set_fullscreen(int fullscreen, int width, int height) {
    g_runtime.fullscreen = fullscreen != 0;
    craft_upstream_resize(width, height);
}

void craft_upstream_queue_key(int key) {
    craft_glfw_inject_key(key);
}

void craft_upstream_set_mouse(int x, int y, int dx, int dy, int wheel,
                              uint8_t buttons, int focused, int inside) {
    craft_glfw_set_mouse_state(x, y, dx, dy, wheel, buttons, focused, inside);
}

void craft_upstream_blit(int x, int y, int width, int height) {
    craft_gl_blit_to(x, y, width, height);
}

void craft_upstream_request_close(void) {
    craft_glfw_request_close();
}
