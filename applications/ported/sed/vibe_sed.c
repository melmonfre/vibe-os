#include <stdbool.h>

#include <lang/include/vibe_app_runtime.h>

#define EOF (-1)

enum sed_cmd_type {
  SED_CMD_SUB,
  SED_CMD_PRINT
};

struct sed_command {
  enum sed_cmd_type type;
  char pattern[128];
  char replacement[128];
  bool global;
};

struct sed_program {
  struct sed_command commands[16];
  int count;
};

static int
append_char(char **buf, size_t *len, size_t *cap, char ch)
{
  char *next;
  size_t new_cap;

  if (*len + 1 >= *cap)
    {
      new_cap = *cap ? (*cap * 2) : 128;
      next = realloc(*buf, new_cap);
      if (!next)
        return -1;
      *buf = next;
      *cap = new_cap;
    }
  (*buf)[(*len)++] = ch;
  (*buf)[*len] = '\0';
  return 0;
}

static const char *
find_substr(const char *haystack, const char *needle)
{
  size_t needle_len;
  size_t i;

  needle_len = strlen(needle);
  if (needle_len == 0)
    return haystack;

  for (i = 0; haystack[i] != '\0'; ++i)
    {
      if (strncmp(haystack + i, needle, needle_len) == 0)
        return haystack + i;
    }
  return NULL;
}

static int
append_data(char **buf, size_t *len, size_t *cap, const char *src, size_t n)
{
  size_t i;

  for (i = 0; i < n; ++i)
    {
      if (append_char(buf, len, cap, src[i]) != 0)
        return -1;
    }
  return 0;
}

static int
read_line(FILE *fp, char **line_out)
{
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;
  int ch;

  while ((ch = fgetc(fp)) != EOF)
    {
      if (append_char(&buf, &len, &cap, (char) ch) != 0)
        {
          free(buf);
          return -1;
        }
      if (ch == '\n')
        break;
    }

  if (!buf && ch == EOF)
    return 0;

  if (!buf)
    {
      buf = malloc(1);
      if (!buf)
        return -1;
      buf[0] = '\0';
    }

  *line_out = buf;
  return 1;
}

static const char *
parse_segment(const char *src, char delim, char *dst, size_t dst_size)
{
  size_t pos = 0;

  while (*src && *src != delim)
    {
      if (*src == '\\' && src[1] == delim)
        ++src;
      if (pos + 1 >= dst_size)
        return NULL;
      dst[pos++] = *src++;
    }
  if (*src != delim)
    return NULL;
  dst[pos] = '\0';
  return src + 1;
}

static int
parse_script(const char *script, struct sed_program *program)
{
  const char *p = script;

  memset(program, 0, sizeof(*program));
  while (*p)
    {
      while (*p == ' ' || *p == '\t' || *p == ';')
        ++p;
      if (!*p)
        break;
      if (program->count >= (int) (sizeof(program->commands) / sizeof(program->commands[0])))
        return -1;

      if (*p == 'p')
        {
          program->commands[program->count++].type = SED_CMD_PRINT;
          ++p;
          continue;
        }

      if (*p == 's')
        {
          char delim;
          struct sed_command *cmd = &program->commands[program->count];

          ++p;
          delim = *p++;
          if (!delim)
            return -1;
          p = parse_segment(p, delim, cmd->pattern, sizeof(cmd->pattern));
          if (!p)
            return -1;
          p = parse_segment(p, delim, cmd->replacement, sizeof(cmd->replacement));
          if (!p)
            return -1;
          cmd->type = SED_CMD_SUB;
          cmd->global = false;
          while (*p && *p != ';')
            {
              if (*p == 'g')
                cmd->global = true;
              else if (*p != ' ' && *p != '\t')
                return -1;
              ++p;
            }
          ++program->count;
          continue;
        }

      return -1;
    }

  return program->count > 0 ? 0 : -1;
}

static int
apply_subst(const struct sed_command *cmd, const char *input, char **output)
{
  const char *cursor = input;
  const char *match;
  size_t pat_len = strlen(cmd->pattern);
  size_t rep_len = strlen(cmd->replacement);
  char *buf = NULL;
  size_t len = 0;
  size_t cap = 0;

  if (pat_len == 0)
    {
      size_t input_len = strlen(input);
      *output = malloc(input_len + 1);
      if (*output)
        memcpy(*output, input, input_len + 1);
      return *output ? 0 : -1;
    }

  while ((match = find_substr(cursor, cmd->pattern)) != NULL)
    {
      if (append_data(&buf, &len, &cap, cursor, (size_t) (match - cursor)) != 0)
        goto fail;
      if (append_data(&buf, &len, &cap, cmd->replacement, rep_len) != 0)
        goto fail;
      cursor = match + pat_len;
      if (!cmd->global)
        break;
    }

  if (append_data(&buf, &len, &cap, cursor, strlen(cursor)) != 0)
    goto fail;
  *output = buf;
  return 0;

fail:
  free(buf);
  return -1;
}

static int
process_stream(FILE *fp, const struct sed_program *program, bool suppress_default)
{
  char *line = NULL;
  int status;

  while ((status = read_line(fp, &line)) > 0)
    {
      char *current = line;
      int extra_prints = 0;
      int i;

      line = NULL;
      for (i = 0; i < program->count; ++i)
        {
          if (program->commands[i].type == SED_CMD_PRINT)
            {
              ++extra_prints;
              continue;
            }

          char *next = NULL;
          if (apply_subst(&program->commands[i], current, &next) != 0)
            {
              free(current);
              return 1;
            }
          free(current);
          current = next;
        }

      if (!suppress_default)
        fputs(current, stdout);
      while (extra_prints-- > 0)
        fputs(current, stdout);

      free(current);
    }

  return status < 0 ? 1 : 0;
}

int
vibe_app_main(int argc, char **argv)
{
  struct sed_program program;
  const char *script = NULL;
  bool suppress_default = false;
  int argi = 1;
  int rc = 0;

  while (argi < argc && argv[argi] && argv[argi][0] == '-')
    {
      if (strcmp(argv[argi], "-n") == 0)
        suppress_default = true;
      else if (strcmp(argv[argi], "-e") == 0 && argi + 1 < argc)
        script = argv[++argi];
      else
        {
          fprintf(stderr, "sed: unsupported option: %s\n", argv[argi]);
          return 1;
        }
      ++argi;
    }

  if (!script && argi < argc)
    script = argv[argi++];

  if (!script)
    {
      fputs("usage: sed [-n] [-e script] [script] [file...]\n", stderr);
      return 1;
    }

  if (parse_script(script, &program) != 0)
    {
      fprintf(stderr, "sed: unsupported script: %s\n", script);
      return 1;
    }

  if (argi >= argc)
    return process_stream(stdin, &program, suppress_default);

  for (; argi < argc; ++argi)
    {
      FILE *fp = fopen(argv[argi], "r");
      if (!fp)
        {
          fprintf(stderr, "sed: cannot open %s\n", argv[argi]);
          rc = 1;
          continue;
        }
      if (process_stream(fp, &program, suppress_default) != 0)
        rc = 1;
      fclose(fp);
    }

  return rc;
}
