#include <lang/include/vibe_app_runtime.h>

#define JAVA_CLASS_MAGIC "VJ01\n"
#define JVM_MAGIC 0xCAFEBABEu

enum {
    CP_UTF8 = 1,
    CP_INTEGER = 3,
    CP_FLOAT = 4,
    CP_LONG = 5,
    CP_DOUBLE = 6,
    CP_CLASS = 7,
    CP_STRING = 8,
    CP_FIELDREF = 9,
    CP_METHODREF = 10,
    CP_NAMEANDTYPE = 12
};

enum {
    JV_NONE = 0,
    JV_INT = 1,
    JV_STRING = 2,
    JV_PRINTSTREAM = 3
};

typedef unsigned char u1;
typedef unsigned short u2;
typedef unsigned int u4;

struct cp_entry {
    u1 tag;
    u2 a;
    u2 b;
    u4 value;
    char *text;
};

struct method_code {
    const u1 *code;
    u4 code_length;
    u2 max_stack;
    u2 max_locals;
};

struct class_file {
    const u1 *data;
    int size;
    struct cp_entry *cp;
    u2 cp_count;
    struct method_code main_code;
};

struct jvalue {
    int kind;
    int int_value;
    const char *string_value;
};

static void java_debug(const char *text) {
    const struct vibe_app_context *ctx = vibe_app_get_context();

    if (ctx && ctx->host && ctx->host->write_debug && text) {
        ctx->host->write_debug(text);
    }
}

static int str_len(const char *text) {
    int len = 0;
    while (text && text[len] != '\0') {
        ++len;
    }
    return len;
}

static int str_eq_local(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] != '\0' || b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int has_suffix(const char *text, const char *suffix) {
    int text_len = str_len(text);
    int suffix_len = str_len(suffix);
    int i;

    if (suffix_len > text_len) {
        return 0;
    }
    for (i = 0; i < suffix_len; ++i) {
        if (text[text_len - suffix_len + i] != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

static void copy_text(char *dst, int max_len, const char *src) {
    int i = 0;

    if (!dst || max_len <= 0) {
        return;
    }
    while (src && src[i] != '\0' && i < (max_len - 1)) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void copy_with_dots_as_slashes(const char *src, char *dst, int max_len) {
    int i = 0;
    while (src && src[i] != '\0' && i < (max_len - 1)) {
        dst[i] = (src[i] == '.') ? '/' : src[i];
        ++i;
    }
    dst[i] = '\0';
}

static void build_class_path(const char *arg, char *out, int out_len) {
    int pos = 0;
    const char *suffix = ".class";

    if (has_suffix(arg, suffix)) {
        copy_text(out, out_len, arg);
        return;
    }

    while (arg && *arg && pos < (out_len - 1)) {
        out[pos++] = *arg++;
    }
    for (int i = 0; suffix[i] != '\0' && pos < (out_len - 1); ++i) {
        out[pos++] = suffix[i];
    }
    out[pos] = '\0';
}

static void join_path(char *out, int out_len, const char *prefix, const char *suffix) {
    int pos = 0;

    if (!out || out_len <= 0) {
        return;
    }
    if (prefix && prefix[0]) {
        while (*prefix && pos < (out_len - 1)) {
            out[pos++] = *prefix++;
        }
        if (pos > 0 && out[pos - 1] != '/' && pos < (out_len - 1)) {
            out[pos++] = '/';
        }
    }
    while (suffix && *suffix && pos < (out_len - 1)) {
        out[pos++] = *suffix++;
    }
    out[pos] = '\0';
}

static int read_metadata_line(const char *data, int size, int line_index, char *out, int out_len) {
    int line = 0;
    int start = 0;
    int end = 0;
    int i;
    int len;

    if (!data || size < 0 || !out || out_len <= 0) {
        return -1;
    }

    for (i = 0; i <= size; ++i) {
        if (line == line_index) {
            start = i;
            while (i < size && data[i] != '\n') {
                ++i;
            }
            end = i;
            len = end - start;
            if (len >= out_len) {
                len = out_len - 1;
            }
            for (i = 0; i < len; ++i) {
                out[i] = data[start + i];
            }
            out[len] = '\0';
            return 0;
        }
        while (i < size && data[i] != '\n') {
            ++i;
        }
        ++line;
    }
    return -1;
}

static u1 read_u1(const u1 *data, int size, int *pos, int *ok) {
    if (!ok || !*ok || !data || !pos || *pos < 0 || *pos >= size) {
        if (ok) {
            *ok = 0;
        }
        return 0;
    }
    return data[(*pos)++];
}

static u2 read_u2(const u1 *data, int size, int *pos, int *ok) {
    u2 hi = read_u1(data, size, pos, ok);
    u2 lo = read_u1(data, size, pos, ok);
    return (u2)((hi << 8) | lo);
}

static u4 read_u4(const u1 *data, int size, int *pos, int *ok) {
    u4 a = read_u2(data, size, pos, ok);
    u4 b = read_u2(data, size, pos, ok);
    return (a << 16) | b;
}

static const char *cp_utf8(const struct class_file *cf, u2 index) {
    if (!cf || index == 0 || index >= cf->cp_count) {
        return 0;
    }
    if (cf->cp[index].tag != CP_UTF8) {
        return 0;
    }
    return cf->cp[index].text;
}

static const char *cp_class_name(const struct class_file *cf, u2 index) {
    if (!cf || index == 0 || index >= cf->cp_count || cf->cp[index].tag != CP_CLASS) {
        return 0;
    }
    return cp_utf8(cf, cf->cp[index].a);
}

static const char *cp_string_value(const struct class_file *cf, u2 index) {
    if (!cf || index == 0 || index >= cf->cp_count || cf->cp[index].tag != CP_STRING) {
        return 0;
    }
    return cp_utf8(cf, cf->cp[index].a);
}

static int cp_integer_value(const struct class_file *cf, u2 index, int *value_out) {
    if (!cf || !value_out || index == 0 || index >= cf->cp_count || cf->cp[index].tag != CP_INTEGER) {
        return -1;
    }
    *value_out = (int)cf->cp[index].value;
    return 0;
}

static int cp_resolve_member(const struct class_file *cf,
                             u2 index,
                             const char **class_name,
                             const char **member_name,
                             const char **descriptor) {
    const struct cp_entry *entry;
    const struct cp_entry *nt;

    if (!cf || index == 0 || index >= cf->cp_count) {
        return -1;
    }
    entry = &cf->cp[index];
    if (entry->tag != CP_FIELDREF && entry->tag != CP_METHODREF) {
        return -1;
    }
    if (!class_name || !member_name || !descriptor) {
        return -1;
    }
    *class_name = cp_class_name(cf, entry->a);
    if (!*class_name || entry->b == 0 || entry->b >= cf->cp_count) {
        return -1;
    }
    nt = &cf->cp[entry->b];
    if (nt->tag != CP_NAMEANDTYPE) {
        return -1;
    }
    *member_name = cp_utf8(cf, nt->a);
    *descriptor = cp_utf8(cf, nt->b);
    return (*member_name && *descriptor) ? 0 : -1;
}

static void free_class_file(struct class_file *cf) {
    u2 i;

    if (!cf || !cf->cp) {
        return;
    }
    for (i = 1; i < cf->cp_count; ++i) {
        if (cf->cp[i].tag == CP_UTF8) {
            free(cf->cp[i].text);
            cf->cp[i].text = 0;
        }
    }
    free(cf->cp);
    cf->cp = 0;
}

static int parse_class_file(const u1 *data, int size, struct class_file *cf) {
    int pos = 0;
    int ok = 1;
    u2 i;
    u2 methods_count;

    if (!data || size < 10 || !cf) {
        return -1;
    }
    memset(cf, 0, sizeof(*cf));
    cf->data = data;
    cf->size = size;

    if (read_u4(data, size, &pos, &ok) != JVM_MAGIC) {
        return -1;
    }
    (void)read_u2(data, size, &pos, &ok);
    (void)read_u2(data, size, &pos, &ok);
    cf->cp_count = read_u2(data, size, &pos, &ok);
    if (!ok || cf->cp_count < 2) {
        return -1;
    }
    cf->cp = (struct cp_entry *)calloc(cf->cp_count, sizeof(struct cp_entry));
    if (!cf->cp) {
        return -1;
    }

    for (i = 1; i < cf->cp_count; ++i) {
        struct cp_entry *entry = &cf->cp[i];
        u2 len;
        entry->tag = read_u1(data, size, &pos, &ok);
        if (!ok) {
            free_class_file(cf);
            return -1;
        }
        switch (entry->tag) {
            case CP_UTF8:
                len = read_u2(data, size, &pos, &ok);
                if (!ok || pos + len > size) {
                    free_class_file(cf);
                    return -1;
                }
                entry->text = (char *)malloc((size_t)len + 1u);
                if (!entry->text) {
                    free_class_file(cf);
                    return -1;
                }
                memcpy(entry->text, data + pos, len);
                entry->text[len] = '\0';
                pos += len;
                break;
            case CP_INTEGER:
                entry->value = read_u4(data, size, &pos, &ok);
                break;
            case CP_CLASS:
            case CP_STRING:
                entry->a = read_u2(data, size, &pos, &ok);
                break;
            case CP_FIELDREF:
            case CP_METHODREF:
            case CP_NAMEANDTYPE:
                entry->a = read_u2(data, size, &pos, &ok);
                entry->b = read_u2(data, size, &pos, &ok);
                break;
            case CP_LONG:
            case CP_DOUBLE:
                (void)read_u4(data, size, &pos, &ok);
                (void)read_u4(data, size, &pos, &ok);
                ++i;
                break;
            case CP_FLOAT:
                (void)read_u4(data, size, &pos, &ok);
                break;
            default:
                free_class_file(cf);
                return -1;
        }
        if (!ok) {
            free_class_file(cf);
            return -1;
        }
    }

    (void)read_u2(data, size, &pos, &ok);
    (void)read_u2(data, size, &pos, &ok);
    (void)read_u2(data, size, &pos, &ok);

    {
        u2 interfaces_count = read_u2(data, size, &pos, &ok);
        while (ok && interfaces_count-- > 0) {
            (void)read_u2(data, size, &pos, &ok);
        }
    }
    {
        u2 fields_count = read_u2(data, size, &pos, &ok);
        while (ok && fields_count-- > 0) {
            u2 attr_count;
            (void)read_u2(data, size, &pos, &ok);
            (void)read_u2(data, size, &pos, &ok);
            (void)read_u2(data, size, &pos, &ok);
            attr_count = read_u2(data, size, &pos, &ok);
            while (ok && attr_count-- > 0) {
                u4 attr_len;
                (void)read_u2(data, size, &pos, &ok);
                attr_len = read_u4(data, size, &pos, &ok);
                pos += (int)attr_len;
                if (pos > size) {
                    ok = 0;
                }
            }
        }
    }

    methods_count = read_u2(data, size, &pos, &ok);
    for (i = 0; ok && i < methods_count; ++i) {
        u2 access_flags = read_u2(data, size, &pos, &ok);
        u2 name_index = read_u2(data, size, &pos, &ok);
        u2 desc_index = read_u2(data, size, &pos, &ok);
        u2 attr_count = read_u2(data, size, &pos, &ok);
        const char *name = cp_utf8(cf, name_index);
        const char *desc = cp_utf8(cf, desc_index);
        int is_main = name && desc &&
                      str_eq_local(name, "main") &&
                      str_eq_local(desc, "([Ljava/lang/String;)V") &&
                      (access_flags & 0x0008u);
        u2 attr_i;

        for (attr_i = 0; ok && attr_i < attr_count; ++attr_i) {
            u2 attr_name_index = read_u2(data, size, &pos, &ok);
            u4 attr_len = read_u4(data, size, &pos, &ok);
            const char *attr_name = cp_utf8(cf, attr_name_index);
            int attr_start = pos;

            if (is_main && attr_name && str_eq_local(attr_name, "Code")) {
                cf->main_code.max_stack = read_u2(data, size, &pos, &ok);
                cf->main_code.max_locals = read_u2(data, size, &pos, &ok);
                cf->main_code.code_length = read_u4(data, size, &pos, &ok);
                if (!ok || pos + (int)cf->main_code.code_length > size) {
                    free_class_file(cf);
                    return -1;
                }
                cf->main_code.code = data + pos;
            }
            pos = attr_start + (int)attr_len;
            if (pos > size) {
                ok = 0;
            }
        }
    }

    if (!ok || !cf->main_code.code || cf->main_code.code_length == 0u) {
        free_class_file(cf);
        return -1;
    }
    return 0;
}

static int execute_main_code(const struct class_file *cf) {
    struct jvalue stack[256];
    struct jvalue locals[256];
    int sp = 0;
    u4 pc = 0;

    memset(stack, 0, sizeof(stack));
    memset(locals, 0, sizeof(locals));

    while (pc < cf->main_code.code_length) {
        u1 op = cf->main_code.code[pc++];

        switch (op) {
            case 0x02: case 0x03: case 0x04: case 0x05:
            case 0x06: case 0x07: case 0x08:
                stack[sp].kind = JV_INT;
                stack[sp].int_value = (int)op - 3;
                ++sp;
                break;
            case 0x10:
                stack[sp].kind = JV_INT;
                stack[sp].int_value = (signed char)cf->main_code.code[pc++];
                ++sp;
                break;
            case 0x11: {
                int value = (short)((cf->main_code.code[pc] << 8) | cf->main_code.code[pc + 1]);
                pc += 2;
                stack[sp].kind = JV_INT;
                stack[sp].int_value = value;
                ++sp;
                break;
            }
            case 0x12: {
                u2 index = cf->main_code.code[pc++];
                int int_value = 0;
                const char *str_value = cp_string_value(cf, index);

                if (str_value) {
                    stack[sp].kind = JV_STRING;
                    stack[sp].string_value = str_value;
                    ++sp;
                    break;
                }
                if (cp_integer_value(cf, index, &int_value) == 0) {
                    stack[sp].kind = JV_INT;
                    stack[sp].int_value = int_value;
                    ++sp;
                    break;
                }
                printf("java: unsupported ldc constant #%d\n", index);
                return 1;
            }
            case 0x13: {
                u2 index = (u2)((cf->main_code.code[pc] << 8) | cf->main_code.code[pc + 1]);
                int int_value = 0;
                const char *str_value;
                pc += 2;
                str_value = cp_string_value(cf, index);
                if (str_value) {
                    stack[sp].kind = JV_STRING;
                    stack[sp].string_value = str_value;
                    ++sp;
                    break;
                }
                if (cp_integer_value(cf, index, &int_value) == 0) {
                    stack[sp].kind = JV_INT;
                    stack[sp].int_value = int_value;
                    ++sp;
                    break;
                }
                printf("java: unsupported ldc_w constant #%d\n", index);
                return 1;
            }
            case 0x15: {
                u1 index = cf->main_code.code[pc++];
                stack[sp++] = locals[index];
                break;
            }
            case 0x1a: case 0x1b: case 0x1c: case 0x1d:
                stack[sp++] = locals[op - 0x1a];
                break;
            case 0x36: {
                u1 index = cf->main_code.code[pc++];
                locals[index] = stack[--sp];
                break;
            }
            case 0x3b: case 0x3c: case 0x3d: case 0x3e:
                locals[op - 0x3b] = stack[--sp];
                break;
            case 0x59:
                stack[sp] = stack[sp - 1];
                ++sp;
                break;
            case 0x60:
                stack[sp - 2].int_value += stack[sp - 1].int_value;
                --sp;
                break;
            case 0x64:
                stack[sp - 2].int_value -= stack[sp - 1].int_value;
                --sp;
                break;
            case 0x68:
                stack[sp - 2].int_value *= stack[sp - 1].int_value;
                --sp;
                break;
            case 0x6c:
                if (stack[sp - 1].int_value == 0) {
                    puts("java: divide by zero");
                    return 1;
                }
                stack[sp - 2].int_value /= stack[sp - 1].int_value;
                --sp;
                break;
            case 0x74:
                stack[sp - 1].int_value = -stack[sp - 1].int_value;
                break;
            case 0xb2: {
                u2 index = (u2)((cf->main_code.code[pc] << 8) | cf->main_code.code[pc + 1]);
                const char *class_name = 0;
                const char *member_name = 0;
                const char *descriptor = 0;
                pc += 2;
                if (cp_resolve_member(cf, index, &class_name, &member_name, &descriptor) != 0 ||
                    !str_eq_local(class_name, "java/lang/System") ||
                    !str_eq_local(member_name, "out")) {
                    printf("java: unsupported getstatic #%d\n", index);
                    return 1;
                }
                stack[sp].kind = JV_PRINTSTREAM;
                ++sp;
                break;
            }
            case 0xb6: {
                u2 index = (u2)((cf->main_code.code[pc] << 8) | cf->main_code.code[pc + 1]);
                const char *class_name = 0;
                const char *member_name = 0;
                const char *descriptor = 0;
                struct jvalue arg;
                struct jvalue recv;
                pc += 2;

                if (sp < 2) {
                    return 1;
                }
                arg = stack[--sp];
                recv = stack[--sp];
                if (cp_resolve_member(cf, index, &class_name, &member_name, &descriptor) != 0 ||
                    recv.kind != JV_PRINTSTREAM ||
                    !str_eq_local(class_name, "java/io/PrintStream")) {
                    printf("java: unsupported invokevirtual #%d\n", index);
                    return 1;
                }
                if (str_eq_local(member_name, "println")) {
                    if (arg.kind == JV_STRING && str_eq_local(descriptor, "(Ljava/lang/String;)V")) {
                        printf("%s\n", arg.string_value ? arg.string_value : "");
                    } else if (arg.kind == JV_INT && str_eq_local(descriptor, "(I)V")) {
                        printf("%d\n", arg.int_value);
                    } else {
                        puts("java: unsupported println signature");
                        return 1;
                    }
                } else if (str_eq_local(member_name, "print")) {
                    if (arg.kind == JV_STRING && str_eq_local(descriptor, "(Ljava/lang/String;)V")) {
                        printf("%s", arg.string_value ? arg.string_value : "");
                    } else if (arg.kind == JV_INT && str_eq_local(descriptor, "(I)V")) {
                        printf("%d", arg.int_value);
                    } else {
                        puts("java: unsupported print signature");
                        return 1;
                    }
                } else {
                    printf("java: unsupported method %s\n", member_name);
                    return 1;
                }
                break;
            }
            case 0xb1:
                return 0;
            default:
                printf("java: unsupported opcode 0x%x\n", (unsigned int)op);
                return 1;
        }
        if (sp < 0 || sp >= (int)(sizeof(stack) / sizeof(stack[0]))) {
            puts("java: operand stack overflow");
            return 1;
        }
    }
    return 0;
}

static int run_vibe_class(const char *path) {
    const char *data = 0;
    int size = 0;
    char class_name[128];
    char message[512];
    struct class_file cf;

    if (vibe_app_read_file(path, &data, &size) != 0 || !data || size <= 0) {
        printf("java: class not found: %s\n", path);
        return 1;
    }

    if (size >= 5 && strncmp(data, JAVA_CLASS_MAGIC, 5) == 0) {
        if (read_metadata_line(data, size, 1, class_name, sizeof(class_name)) != 0 ||
            read_metadata_line(data, size, 2, message, sizeof(message)) != 0) {
            printf("java: invalid legacy class metadata: %s\n", path);
            return 1;
        }
        printf("%s\n", message);
        return 0;
    }

    if (parse_class_file((const u1 *)data, size, &cf) != 0) {
        printf("java: unsupported class format: %s\n", path);
        return 1;
    }
    {
        int rc = execute_main_code(&cf);
        free_class_file(&cf);
        return rc;
    }
}

static void print_help(void) {
    puts("Usage: java [-version] [-help] <MainClass|file.class>");
    puts("VibeOS Java runs a subset of real JVM classfiles plus legacy VibeJava classes.");
}

int vibe_app_main(int argc, char **argv) {
    char class_path[160];
    char converted[160];
    const char *classpath = "";
    int argi = 1;

    if (argc <= 1) {
        print_help();
        return 0;
    }

    if (str_eq_local(argv[argi], "-version")) {
        java_debug("java: version ok\n");
        puts("openjdk version \"1.8.0-vibe\"");
        puts("OpenJDK Runtime Environment (VibeOS subset JVM)");
        return 0;
    }
    if (str_eq_local(argv[argi], "-help") || str_eq_local(argv[argi], "--help")) {
        print_help();
        return 0;
    }

    while (argi < argc) {
        if ((str_eq_local(argv[argi], "-cp") || str_eq_local(argv[argi], "-classpath")) &&
            argi + 1 < argc) {
            classpath = argv[argi + 1];
            argi += 2;
            continue;
        }
        break;
    }
    if (argi >= argc) {
        print_help();
        return 1;
    }

    if (has_suffix(argv[argi], ".class")) {
        join_path(class_path, sizeof(class_path), classpath, argv[argi]);
    } else {
        copy_with_dots_as_slashes(argv[argi], converted, sizeof(converted));
        build_class_path(converted, class_path, sizeof(class_path));
        if (classpath[0]) {
            join_path(converted, sizeof(converted), classpath, class_path);
            copy_text(class_path, sizeof(class_path), converted);
        }
    }
    return run_vibe_class(class_path);
}
