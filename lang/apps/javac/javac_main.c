#include <lang/include/vibe_app_runtime.h>

enum {
    PRINT_EXPR_NONE = 0,
    PRINT_EXPR_STRING = 1,
    PRINT_EXPR_INT = 2
};

struct print_expr {
    int kind;
    char string_value[512];
    unsigned char int_code[256];
    int int_code_len;
    int int_max_stack;
};

struct expr_parser {
    const char *text;
    int pos;
    int error;
    unsigned char *out;
    int out_len;
    int out_pos;
    int cur_stack;
    int max_stack;
};

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

static int is_ident_char(char c, int first) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$') {
        return 1;
    }
    if (!first && c >= '0' && c <= '9') {
        return 1;
    }
    return 0;
}

static int find_substr(const char *haystack, int haystack_len, const char *needle) {
    int needle_len = str_len(needle);
    int i;
    int j;

    if (!haystack || !needle || needle_len <= 0 || haystack_len < needle_len) {
        return -1;
    }

    for (i = 0; i <= haystack_len - needle_len; ++i) {
        for (j = 0; j < needle_len; ++j) {
            if (haystack[i + j] != needle[j]) {
                break;
            }
        }
        if (j == needle_len) {
            return i;
        }
    }
    return -1;
}

static int copy_identifier(const char *src, int src_len, char *out, int out_len) {
    int len = 0;

    if (!src || src_len <= 0 || !out || out_len <= 1 || !is_ident_char(src[0], 1)) {
        return -1;
    }
    while (len < src_len && len < (out_len - 1) && is_ident_char(src[len], len == 0)) {
        out[len] = src[len];
        ++len;
    }
    out[len] = '\0';
    return len;
}

static int extract_class_name(const char *src, int size, char *out, int out_len) {
    int pos = find_substr(src, size, "class ");

    if (pos < 0) {
        return -1;
    }
    pos += 6;
    while (pos < size && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) {
        ++pos;
    }
    return copy_identifier(src + pos, size - pos, out, out_len) > 0 ? 0 : -1;
}

static int extract_package_name(const char *src, int size, char *out, int out_len) {
    int pos = find_substr(src, size, "package ");
    int len = 0;

    if (pos < 0) {
        out[0] = '\0';
        return 0;
    }
    pos += 8;
    while (pos < size && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) {
        ++pos;
    }
    while (pos < size && len < (out_len - 1)) {
        char c = src[pos++];
        if (c == ';') {
            break;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        out[len++] = c;
    }
    out[len] = '\0';
    return 0;
}

static void mkdir_p(const char *path) {
    char temp[256];
    int len = 0;

    if (!path || !path[0]) {
        return;
    }
    while (path[len] != '\0' && len < (int)sizeof(temp) - 1) {
        temp[len] = path[len];
        ++len;
    }
    temp[len] = '\0';
    for (int i = 1; i < len; ++i) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            (void)vibe_app_create_dir(temp);
            temp[i] = '/';
        }
    }
    (void)vibe_app_create_dir(temp);
}

static void append_path_component(char *dst, int max_len, int *pos, const char *src, int dots_to_slashes) {
    while (src && *src && *pos < (max_len - 1)) {
        char c = *src++;
        if (dots_to_slashes && c == '.') {
            c = '/';
        }
        dst[(*pos)++] = c;
    }
    dst[*pos] = '\0';
}

static void build_output_file_path(const char *out_dir,
                                   const char *package_name,
                                   const char *class_name,
                                   char *out,
                                   int out_len) {
    int pos = 0;

    out[0] = '\0';
    if (out_dir && out_dir[0]) {
        append_path_component(out, out_len, &pos, out_dir, 0);
        if (pos > 0 && out[pos - 1] != '/' && pos < (out_len - 1)) {
            out[pos++] = '/';
            out[pos] = '\0';
        }
    }
    if (package_name && package_name[0]) {
        append_path_component(out, out_len, &pos, package_name, 1);
        if (pos > 0 && out[pos - 1] != '/' && pos < (out_len - 1)) {
            out[pos++] = '/';
            out[pos] = '\0';
        }
    }
    append_path_component(out, out_len, &pos, class_name, 0);
    append_path_component(out, out_len, &pos, ".class", 0);
}

static void skip_ws(struct expr_parser *p) {
    while (!p->error) {
        char c = p->text[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++p->pos;
            continue;
        }
        break;
    }
}

static void emit_u1(struct expr_parser *p, unsigned char b) {
    if (p->error || p->out_pos >= p->out_len) {
        p->error = 1;
        return;
    }
    p->out[p->out_pos++] = b;
}

static void note_push(struct expr_parser *p, int delta) {
    p->cur_stack += delta;
    if (p->cur_stack > p->max_stack) {
        p->max_stack = p->cur_stack;
    }
    if (p->cur_stack < 0) {
        p->error = 1;
    }
}

static void emit_push_int(struct expr_parser *p, int value) {
    if (value >= -1 && value <= 5) {
        emit_u1(p, (unsigned char)(0x03 + value));
    } else if (value >= -128 && value <= 127) {
        emit_u1(p, 0x10);
        emit_u1(p, (unsigned char)(value & 0xFF));
    } else if (value >= -32768 && value <= 32767) {
        emit_u1(p, 0x11);
        emit_u1(p, (unsigned char)((value >> 8) & 0xFF));
        emit_u1(p, (unsigned char)(value & 0xFF));
    } else {
        p->error = 1;
        return;
    }
    note_push(p, 1);
}

static int parse_expr(struct expr_parser *p);

static int parse_number(struct expr_parser *p, int *value_out) {
    int sign = 1;
    int value = 0;
    int seen = 0;

    skip_ws(p);
    if (p->text[p->pos] == '-') {
        sign = -1;
        ++p->pos;
    }
    while (p->text[p->pos] >= '0' && p->text[p->pos] <= '9') {
        value = value * 10 + (p->text[p->pos] - '0');
        ++p->pos;
        seen = 1;
    }
    if (!seen) {
        return -1;
    }
    *value_out = value * sign;
    return 0;
}

static int parse_factor(struct expr_parser *p) {
    int value;

    skip_ws(p);
    if (p->text[p->pos] == '(') {
        ++p->pos;
        if (parse_expr(p) != 0) {
            return -1;
        }
        skip_ws(p);
        if (p->text[p->pos] != ')') {
            p->error = 1;
            return -1;
        }
        ++p->pos;
        return 0;
    }
    if (parse_number(p, &value) != 0) {
        p->error = 1;
        return -1;
    }
    emit_push_int(p, value);
    return p->error ? -1 : 0;
}

static int parse_term(struct expr_parser *p) {
    if (parse_factor(p) != 0) {
        return -1;
    }
    for (;;) {
        char op;
        skip_ws(p);
        op = p->text[p->pos];
        if (op != '*' && op != '/') {
            break;
        }
        ++p->pos;
        if (parse_factor(p) != 0) {
            return -1;
        }
        emit_u1(p, op == '*' ? 0x68 : 0x6c);
        note_push(p, -1);
    }
    return p->error ? -1 : 0;
}

static int parse_expr(struct expr_parser *p) {
    if (parse_term(p) != 0) {
        return -1;
    }
    for (;;) {
        char op;
        skip_ws(p);
        op = p->text[p->pos];
        if (op != '+' && op != '-') {
            break;
        }
        ++p->pos;
        if (parse_term(p) != 0) {
            return -1;
        }
        emit_u1(p, op == '+' ? 0x60 : 0x64);
        note_push(p, -1);
    }
    return p->error ? -1 : 0;
}

static int extract_print_expression(const char *src, int size, struct print_expr *expr) {
    static const char *marker = "System.out.println(";
    int pos = find_substr(src, size, marker);
    int out_pos = 0;

    if (!expr) {
        return -1;
    }
    memset(expr, 0, sizeof(*expr));
    if (pos < 0) {
        return -1;
    }
    pos += str_len(marker);
    while (pos < size && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) {
        ++pos;
    }
    if (pos >= size) {
        return -1;
    }
    if (src[pos] == '"') {
        ++pos;
        while (pos < size && src[pos] != '"' && out_pos < (int)sizeof(expr->string_value) - 1) {
            if (src[pos] == '\\' && pos + 1 < size) {
                ++pos;
                if (src[pos] == 'n') {
                    expr->string_value[out_pos++] = '\n';
                    ++pos;
                    continue;
                }
            }
            expr->string_value[out_pos++] = src[pos++];
        }
        expr->string_value[out_pos] = '\0';
        expr->kind = PRINT_EXPR_STRING;
        return (pos < size && src[pos] == '"') ? 0 : -1;
    } else {
        struct expr_parser parser;

        memset(&parser, 0, sizeof(parser));
        parser.text = src + pos;
        parser.out = expr->int_code;
        parser.out_len = (int)sizeof(expr->int_code);
        if (parse_expr(&parser) != 0) {
            return -1;
        }
        skip_ws(&parser);
        if (parser.text[parser.pos] != ')') {
            return -1;
        }
        expr->kind = PRINT_EXPR_INT;
        expr->int_code_len = parser.out_pos;
        expr->int_max_stack = parser.max_stack > 0 ? parser.max_stack : 1;
        return 0;
    }
}

static void write_u1(unsigned char *out, int cap, int *pos, unsigned char value) {
    if (*pos < cap) {
        out[(*pos)++] = value;
    }
}

static void write_u2(unsigned char *out, int cap, int *pos, unsigned short value) {
    write_u1(out, cap, pos, (unsigned char)((value >> 8) & 0xFF));
    write_u1(out, cap, pos, (unsigned char)(value & 0xFF));
}

static void write_u4(unsigned char *out, int cap, int *pos, unsigned int value) {
    write_u2(out, cap, pos, (unsigned short)((value >> 16) & 0xFFFF));
    write_u2(out, cap, pos, (unsigned short)(value & 0xFFFF));
}

static void write_bytes(unsigned char *out, int cap, int *pos, const unsigned char *src, int len) {
    int i;
    for (i = 0; i < len; ++i) {
        write_u1(out, cap, pos, src[i]);
    }
}

static void write_utf8_cp(unsigned char *out, int cap, int *pos, const char *text) {
    int len = str_len(text);
    write_u1(out, cap, pos, 1);
    write_u2(out, cap, pos, (unsigned short)len);
    write_bytes(out, cap, pos, (const unsigned char *)text, len);
}

static void write_class_cp(unsigned char *out, int cap, int *pos, unsigned short name_index) {
    write_u1(out, cap, pos, 7);
    write_u2(out, cap, pos, name_index);
}

static void write_string_cp(unsigned char *out, int cap, int *pos, unsigned short utf8_index) {
    write_u1(out, cap, pos, 8);
    write_u2(out, cap, pos, utf8_index);
}

static void write_name_and_type_cp(unsigned char *out, int cap, int *pos, unsigned short name_index, unsigned short desc_index) {
    write_u1(out, cap, pos, 12);
    write_u2(out, cap, pos, name_index);
    write_u2(out, cap, pos, desc_index);
}

static void write_ref_cp(unsigned char tag, unsigned char *out, int cap, int *pos, unsigned short class_index, unsigned short nt_index) {
    write_u1(out, cap, pos, tag);
    write_u2(out, cap, pos, class_index);
    write_u2(out, cap, pos, nt_index);
}

static void build_source_name(const char *class_name, char *out, int out_len) {
    int pos = 0;
    while (*class_name && pos < (out_len - 1)) {
        out[pos++] = *class_name++;
    }
    if (pos + 5 < out_len) {
        out[pos++] = '.';
        out[pos++] = 'j';
        out[pos++] = 'a';
        out[pos++] = 'v';
        out[pos++] = 'a';
    }
    out[pos] = '\0';
}

static int build_classfile(const char *package_name,
                           const char *class_name,
                           const struct print_expr *expr,
                           unsigned char *out,
                           int out_cap) {
    char internal_name[192];
    char source_name[160];
    unsigned char init_code[5];
    unsigned char main_code[384];
    int cp_count;
    int pos = 0;
    int main_len = 0;
    unsigned short idx_this_utf8 = 1;
    unsigned short idx_this_class = 2;
    unsigned short idx_obj_utf8 = 3;
    unsigned short idx_obj_class = 4;
    unsigned short idx_init_utf8 = 5;
    unsigned short idx_void_desc = 6;
    unsigned short idx_init_nt = 7;
    unsigned short idx_code_utf8 = 9;
    unsigned short idx_main_utf8 = 10;
    unsigned short idx_main_desc = 11;
    unsigned short idx_sys_utf8 = 12;
    unsigned short idx_sys_class = 13;
    unsigned short idx_out_utf8 = 14;
    unsigned short idx_ps_desc = 15;
    unsigned short idx_out_nt = 16;
    unsigned short idx_ps_utf8 = 18;
    unsigned short idx_ps_class = 19;
    unsigned short idx_println_utf8 = 20;
    unsigned short idx_print_desc = 21;
    unsigned short idx_print_nt = 22;
    unsigned short idx_sourcefile_utf8;
    unsigned short idx_source_name_utf8;
    unsigned short idx_message_utf8 = 0;
    unsigned short idx_message_string = 0;

    if (!class_name || !expr || !out || out_cap <= 0) {
        return -1;
    }

    internal_name[0] = '\0';
    if (package_name && package_name[0]) {
        int name_pos = 0;
        append_path_component(internal_name, sizeof(internal_name), &name_pos, package_name, 1);
        if (name_pos > 0 && name_pos < (int)sizeof(internal_name) - 1) {
            internal_name[name_pos++] = '/';
            internal_name[name_pos] = '\0';
        }
        append_path_component(internal_name, sizeof(internal_name), &name_pos, class_name, 0);
    } else {
        append_path_component(internal_name, sizeof(internal_name), &(int){0}, class_name, 0);
    }

    build_source_name(class_name, source_name, sizeof(source_name));
    init_code[0] = 0x2A;
    init_code[1] = 0xB7;
    init_code[2] = 0x00;
    init_code[3] = 0x08;
    init_code[4] = 0xB1;

    main_code[main_len++] = 0xB2;
    main_code[main_len++] = 0x00;
    main_code[main_len++] = 0x11;
    if (expr->kind == PRINT_EXPR_STRING) {
        idx_message_utf8 = 24;
        idx_message_string = 25;
        idx_sourcefile_utf8 = 26;
        idx_source_name_utf8 = 27;
        main_code[main_len++] = 0x12;
        main_code[main_len++] = (unsigned char)idx_message_string;
        cp_count = 28;
    } else {
        idx_sourcefile_utf8 = 24;
        idx_source_name_utf8 = 25;
        memcpy(main_code + main_len, expr->int_code, (size_t)expr->int_code_len);
        main_len += expr->int_code_len;
        cp_count = 26;
    }
    main_code[main_len++] = 0xB6;
    main_code[main_len++] = 0x00;
    main_code[main_len++] = 0x17;
    main_code[main_len++] = 0xB1;

    write_u4(out, out_cap, &pos, 0xCAFEBABEu);
    write_u2(out, out_cap, &pos, 0u);
    write_u2(out, out_cap, &pos, 52u);
    write_u2(out, out_cap, &pos, (unsigned short)cp_count);

    write_utf8_cp(out, out_cap, &pos, internal_name);
    write_class_cp(out, out_cap, &pos, idx_this_utf8);
    write_utf8_cp(out, out_cap, &pos, "java/lang/Object");
    write_class_cp(out, out_cap, &pos, idx_obj_utf8);
    write_utf8_cp(out, out_cap, &pos, "<init>");
    write_utf8_cp(out, out_cap, &pos, "()V");
    write_name_and_type_cp(out, out_cap, &pos, idx_init_utf8, idx_void_desc);
    write_ref_cp(10, out, out_cap, &pos, idx_obj_class, idx_init_nt);
    write_utf8_cp(out, out_cap, &pos, "Code");
    write_utf8_cp(out, out_cap, &pos, "main");
    write_utf8_cp(out, out_cap, &pos, "([Ljava/lang/String;)V");
    write_utf8_cp(out, out_cap, &pos, "java/lang/System");
    write_class_cp(out, out_cap, &pos, idx_sys_utf8);
    write_utf8_cp(out, out_cap, &pos, "out");
    write_utf8_cp(out, out_cap, &pos, "Ljava/io/PrintStream;");
    write_name_and_type_cp(out, out_cap, &pos, idx_out_utf8, idx_ps_desc);
    write_ref_cp(9, out, out_cap, &pos, idx_sys_class, idx_out_nt);
    write_utf8_cp(out, out_cap, &pos, "java/io/PrintStream");
    write_class_cp(out, out_cap, &pos, idx_ps_utf8);
    write_utf8_cp(out, out_cap, &pos, "println");
    write_utf8_cp(out, out_cap, &pos, expr->kind == PRINT_EXPR_STRING ? "(Ljava/lang/String;)V" : "(I)V");
    write_name_and_type_cp(out, out_cap, &pos, idx_println_utf8, idx_print_desc);
    write_ref_cp(10, out, out_cap, &pos, idx_ps_class, idx_print_nt);
    if (expr->kind == PRINT_EXPR_STRING) {
        write_utf8_cp(out, out_cap, &pos, expr->string_value);
        write_string_cp(out, out_cap, &pos, idx_message_utf8);
    }
    write_utf8_cp(out, out_cap, &pos, "SourceFile");
    write_utf8_cp(out, out_cap, &pos, source_name);

    write_u2(out, out_cap, &pos, 0x0021u);
    write_u2(out, out_cap, &pos, idx_this_class);
    write_u2(out, out_cap, &pos, idx_obj_class);
    write_u2(out, out_cap, &pos, 0u);
    write_u2(out, out_cap, &pos, 0u);
    write_u2(out, out_cap, &pos, 2u);

    write_u2(out, out_cap, &pos, 0x0001u);
    write_u2(out, out_cap, &pos, idx_init_utf8);
    write_u2(out, out_cap, &pos, idx_void_desc);
    write_u2(out, out_cap, &pos, 1u);
    write_u2(out, out_cap, &pos, idx_code_utf8);
    write_u4(out, out_cap, &pos, 17u);
    write_u2(out, out_cap, &pos, 1u);
    write_u2(out, out_cap, &pos, 1u);
    write_u4(out, out_cap, &pos, 5u);
    write_bytes(out, out_cap, &pos, init_code, 5);
    write_u2(out, out_cap, &pos, 0u);
    write_u2(out, out_cap, &pos, 0u);

    write_u2(out, out_cap, &pos, 0x0009u);
    write_u2(out, out_cap, &pos, idx_main_utf8);
    write_u2(out, out_cap, &pos, idx_main_desc);
    write_u2(out, out_cap, &pos, 1u);
    write_u2(out, out_cap, &pos, idx_code_utf8);
    write_u4(out, out_cap, &pos, (unsigned int)(12 + main_len));
    write_u2(out, out_cap, &pos, (unsigned short)(1 + (expr->kind == PRINT_EXPR_INT ? expr->int_max_stack : 1)));
    write_u2(out, out_cap, &pos, 1u);
    write_u4(out, out_cap, &pos, (unsigned int)main_len);
    write_bytes(out, out_cap, &pos, main_code, main_len);
    write_u2(out, out_cap, &pos, 0u);
    write_u2(out, out_cap, &pos, 0u);

    write_u2(out, out_cap, &pos, 1u);
    write_u2(out, out_cap, &pos, idx_sourcefile_utf8);
    write_u4(out, out_cap, &pos, 2u);
    write_u2(out, out_cap, &pos, idx_source_name_utf8);
    return pos;
}

static void print_help(void) {
    puts("Usage: javac [-version] [-help] <File.java>");
    puts("VibeOS javac compiles a minimal real classfile subset:");
    puts("- class <Name>");
    puts("- System.out.println(\"text\") or integer expressions");
}

static int compile_file(const char *path, const char *out_dir) {
    const char *src = 0;
    int size = 0;
    char class_name[128];
    char package_name[128];
    char output_path[160];
    unsigned char output[2048];
    struct print_expr expr;
    int output_size;

    if (vibe_app_read_file(path, &src, &size) != 0 || !src || size <= 0) {
        printf("javac: file not found: %s\n", path);
        return 1;
    }
    if (extract_class_name(src, size, class_name, sizeof(class_name)) != 0) {
        printf("javac: could not find class name in %s\n", path);
        return 1;
    }
    (void)extract_package_name(src, size, package_name, sizeof(package_name));
    if (extract_print_expression(src, size, &expr) != 0) {
        puts("javac: only System.out.println(\"...\") or integer expressions are supported right now");
        return 1;
    }

    if (out_dir && out_dir[0]) {
        mkdir_p(out_dir);
    }
    if (package_name[0]) {
        char package_dir[256];
        int package_pos = 0;
        package_dir[0] = '\0';
        if (out_dir && out_dir[0]) {
            append_path_component(package_dir, sizeof(package_dir), &package_pos, out_dir, 0);
            if (package_pos > 0 && package_dir[package_pos - 1] != '/' && package_pos < (int)sizeof(package_dir) - 1) {
                package_dir[package_pos++] = '/';
                package_dir[package_pos] = '\0';
            }
        }
        append_path_component(package_dir, sizeof(package_dir), &package_pos, package_name, 1);
        mkdir_p(package_dir);
    }
    build_output_file_path(out_dir, package_name, class_name, output_path, sizeof(output_path));
    output_size = build_classfile(package_name, class_name, &expr, output, (int)sizeof(output));
    if (output_size <= 0) {
        printf("javac: failed to build class for %s\n", path);
        return 1;
    }
    if (vibe_app_write_file(output_path, output, output_size) != 0) {
        printf("javac: failed to write %s\n", output_path);
        return 1;
    }

    printf("wrote %s\n", output_path);
    return 0;
}

int vibe_app_main(int argc, char **argv) {
    const char *out_dir = "";
    int argi = 1;

    if (argc <= 1) {
        print_help();
        return 0;
    }

    if (str_eq_local(argv[argi], "-version")) {
        puts("javac 1.8.0-vibe");
        return 0;
    }
    if (str_eq_local(argv[argi], "-help") || str_eq_local(argv[argi], "--help")) {
        print_help();
        return 0;
    }

    while (argi < argc) {
        if (str_eq_local(argv[argi], "-d") && argi + 1 < argc) {
            out_dir = argv[argi + 1];
            argi += 2;
            continue;
        }
        if ((str_eq_local(argv[argi], "-cp") || str_eq_local(argv[argi], "-classpath")) &&
            argi + 1 < argc) {
            argi += 2;
            continue;
        }
        break;
    }
    if (argi >= argc) {
        print_help();
        return 1;
    }

    return compile_file(argv[argi], out_dir);
}
