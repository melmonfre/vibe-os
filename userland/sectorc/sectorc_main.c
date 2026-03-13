#include "sectorc_internal.h"
#include <userland/modules/include/fs.h>

#define SECTORC_VERSION "sectorc 0.2"

static int sectorc_arg_equal(const char *a, const char *b) {
    return sectorc_string_equal(a, b);
}

static int dump_append_text(char *buf, int *len, int max_len, const char *text) {
    while (*text != '\0') {
        if (*len >= max_len - 1) {
            return -1;
        }
        buf[(*len)++] = *text++;
    }
    buf[*len] = '\0';
    return 0;
}

static int dump_append_char(char *buf, int *len, int max_len, char c) {
    if (*len >= max_len - 1) {
        return -1;
    }
    buf[*len] = c;
    *len += 1;
    buf[*len] = '\0';
    return 0;
}

static int dump_append_int(char *buf, int *len, int max_len, int value) {
    char tmp[16];
    unsigned int uvalue;
    int pos = 0;

    if (value < 0) {
        if (dump_append_char(buf, len, max_len, '-') != 0) {
            return -1;
        }
        uvalue = (unsigned int)(-(value + 1)) + 1u;
    } else {
        uvalue = (unsigned int)value;
    }

    if (uvalue == 0u) {
        return dump_append_char(buf, len, max_len, '0');
    }

    while (uvalue > 0u && pos < (int)sizeof(tmp)) {
        tmp[pos++] = (char)('0' + (uvalue % 10u));
        uvalue /= 10u;
    }

    while (pos > 0) {
        if (dump_append_char(buf, len, max_len, tmp[--pos]) != 0) {
            return -1;
        }
    }
    return 0;
}

static int build_output_dump(const char *source_path,
                             const struct sectorc_program *program,
                             char *out,
                             int max_len) {
    int len = 0;

    out[0] = '\0';
    if (dump_append_text(out, &len, max_len, "SECTORC-BC1\n") != 0 ||
        dump_append_text(out, &len, max_len, "source ") != 0 ||
        dump_append_text(out, &len, max_len, source_path) != 0 ||
        dump_append_text(out, &len, max_len, "\nmain ") != 0 ||
        dump_append_int(out, &len, max_len, program->main_function) != 0 ||
        dump_append_text(out, &len, max_len, "\nglobals ") != 0 ||
        dump_append_int(out, &len, max_len, program->global_count) != 0 ||
        dump_append_text(out, &len, max_len, "\nfunctions ") != 0 ||
        dump_append_int(out, &len, max_len, program->function_count) != 0 ||
        dump_append_text(out, &len, max_len, "\nstrings ") != 0 ||
        dump_append_int(out, &len, max_len, program->string_count) != 0 ||
        dump_append_text(out, &len, max_len, "\nformat opcode operand pairs\ncode") != 0) {
        return -1;
    }

    for (int i = 0; i < program->code_count; ++i) {
        if (dump_append_char(out, &len, max_len, ' ') != 0 ||
            dump_append_int(out, &len, max_len, program->code[i]) != 0) {
            return -1;
        }
    }

    if (dump_append_char(out, &len, max_len, '\n') != 0) {
        return -1;
    }
    return 0;
}

static void sectorc_print_usage(const char *prog) {
    sectorc_write("uso: ");
    sectorc_write(prog);
    sectorc_write_line(" [opcoes] <arquivo.c>");
    sectorc_write_line("opcoes:");
    sectorc_write_line("  -h, -help, --help  mostra ajuda");
    sectorc_write_line("  -v, --version      mostra versao");
    sectorc_write_line("  -o <arquivo>       grava bytecode textual no VFS");
    sectorc_write_line("subset: globais int, funcoes sem argumentos, if, while, atribuicao, print()");
}

int sectorc_main(int argc, char **argv) {
    static struct sectorc_program program;
    char output_dump[FS_FILE_MAX + 1];
    const char *source = 0;
    const char *path = 0;
    const char *output_path = 0;
    const char *prog = (argc > 0 && argv != 0 && argv[0] != 0) ? argv[0] : "sectorc";
    int compile_status;
    int exec_status;

    if (argc <= 1) {
        sectorc_print_usage(prog);
        return 0;
    }

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (sectorc_arg_equal(arg, "-h") ||
            sectorc_arg_equal(arg, "-help") ||
            sectorc_arg_equal(arg, "--help")) {
            sectorc_print_usage(prog);
            return 0;
        }

        if (sectorc_arg_equal(arg, "-v") ||
            sectorc_arg_equal(arg, "--version")) {
            sectorc_write_line(SECTORC_VERSION);
            return 0;
        }

        if (sectorc_arg_equal(arg, "-o")) {
            if (i + 1 >= argc) {
                sectorc_write_line("sectorc: faltou caminho apos -o");
                return 0;
            }
            output_path = argv[++i];
            continue;
        }

        if (arg[0] == '-' && arg[1] == 'o' && arg[2] != '\0') {
            output_path = arg + 2;
            continue;
        }

        if (arg[0] == '-') {
            sectorc_write("sectorc: opcao desconhecida ");
            sectorc_write_line(arg);
            return 0;
        }

        if (path == 0) {
            path = arg;
        } else {
            sectorc_write_line("sectorc: argumentos extras ignorados");
        }
    }

    if (path == 0) {
        sectorc_print_usage(prog);
        return 0;
    }

    sectorc_write("compilando ");
    sectorc_write(path);
    sectorc_write_line("...");

    if (sectorc_read_source(path, &source) != 0) {
        sectorc_write_line("sectorc: arquivo nao encontrado");
        return 0;
    }

    compile_status = sectorc_compile(path, source, &program);
    if (compile_status != 0) {
        sectorc_write_line(program.error);
        return 0;
    }

    sectorc_write_line("ok");
    sectorc_write("bytecode: ");
    sectorc_write_int(program.code_count / 2);
    sectorc_write_line(" instrucoes");

    if (output_path != 0) {
        if (build_output_dump(path, &program, output_dump, (int)sizeof(output_dump)) != 0) {
            sectorc_write_line("sectorc: saida -o excede limite do VFS");
            return 0;
        }
        if (fs_write_file(output_path, output_dump, 0) != 0) {
            sectorc_write_line("sectorc: falha ao gravar arquivo de saida");
            return 0;
        }
        sectorc_write("saida gerada em ");
        sectorc_write_line(output_path);
        return 0;
    }

    sectorc_write_line("executando...");

    exec_status = sectorc_execute(&program);
    if (exec_status != 0) {
        sectorc_write_line(program.error);
    }
    return 0;
}
