/*
 * fsbuild — строит дерево каталогов и файлов по текстовому описанию (spec).
 *
 * Для работы с файловой системой используются ТОЛЬКО вызовы из раздела 1.2
 * методички: mkdir, open, close, read, write, ftruncate, link, symlink.
 * Сам spec-файл тоже читается через open/read.
 *
 * Формат spec (одна команда на строку, пути отсчитываются от текущего каталога):
 *
 *   # комментарий (только в начале строки)
 *   dir      <путь>
 *   file     <путь> empty
 *   file     <путь> text <произвольная строка до конца строки>
 *   file     <путь> random <N>       N случайных байт из /dev/urandom
 *   file     <путь> zero   <N>       N нулевых байт
 *   symlink  <путь-ссылки> <цель>    цель записывается в ссылку как есть
 *   hardlink <путь-ссылки> <существующий-файл>
 *
 * Команды выполняются сверху вниз, родительские каталоги нужно объявлять
 * раньше вложенных объектов (как с обычным mkdir без -p).
 *
 * Сборка: gcc -std=c11 -Wall -Wextra -o bin/fsbuild src/fsbuild.c
 * Запуск: ./bin/fsbuild structure.spec
 */
#define _DEFAULT_SOURCE
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define RANDOM_SOURCE "/dev/urandom"

/* ---------- сообщения об ошибках ---------- */

static void die(const char *op, const char *path)
{
    fprintf(stderr, "%s(%s): %s\n", op, path, strerror(errno));
    exit(EXIT_FAILURE);
}

static void die_line(const char *spec, int lineno, const char *msg)
{
    fprintf(stderr, "%s:%d: %s\n", spec, lineno, msg);
    exit(EXIT_FAILURE);
}

/* ---------- примитивы работы с ФС (только вызовы из 1.2) ---------- */

/* write() может записать меньше, чем просили, поэтому пишем в цикле */
static void write_all(int fd, const char *path, const void *buf, size_t n)
{
    const char *p = buf;
    while (n > 0) {
        ssize_t w = write(fd, p, n);
        if (w == -1)
            die("write", path);
        p += w;
        n -= (size_t)w;
    }
}

/* O_EXCL: убеждаемся, что файл создан нами, а не открыт уже существующий */
static int create_file(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRWXU);
    if (fd == -1)
        die("open", path);
    return fd;
}

static void make_dir(const char *path)
{
    printf("mkdir     %s\n", path);
    if (mkdir(path, S_IRWXU) == -1)
        die("mkdir", path);
}

/* content == NULL -> пустой файл */
static void make_text(const char *path, const char *content)
{
    printf("file      %s  (%s)\n", path, content ? "text" : "empty");
    int fd = create_file(path);
    if (content)
        write_all(fd, path, content, strlen(content));
    close(fd);
}

static void make_random(const char *path, size_t size)
{
    printf("file      %s  (random, %zu B)\n", path, size);
    int rnd = open(RANDOM_SOURCE, O_RDONLY);
    if (rnd == -1)
        die("open", RANDOM_SOURCE);
    int fd = create_file(path);

    char buf[4096];
    while (size > 0) {
        size_t chunk = size < sizeof buf ? size : sizeof buf;
        ssize_t r = read(rnd, buf, chunk);
        if (r == -1)
            die("read", RANDOM_SOURCE);
        write_all(fd, path, buf, (size_t)r);
        size -= (size_t)r;
    }
    close(fd);
    close(rnd);
}

/* ftruncate до большего размера дополняет файл нулями */
static void make_zeros(const char *path, size_t size)
{
    printf("file      %s  (zero, %zu B)\n", path, size);
    int fd = create_file(path);
    if (ftruncate(fd, (off_t)size) == -1)
        die("ftruncate", path);
    close(fd);
}

static void make_hardlink(const char *linkpath, const char *existing)
{
    printf("link      %s  ==  %s\n", linkpath, existing);
    if (link(existing, linkpath) == -1)
        die("link", linkpath);
}

/*
 * ВАЖНО: target символьной ссылки отсчитывается от каталога, в котором лежит
 * САМА ССЫЛКА, а не от рабочего каталога программы.
 */
static void make_symlink(const char *linkpath, const char *target)
{
    printf("symlink   %s  ->  %s\n", linkpath, target);
    if (symlink(target, linkpath) == -1)
        die("symlink", linkpath);
}

/* ---------- чтение и разбор spec ---------- */

static char *read_whole_file(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        die("open", path);

    size_t cap = 4096, size = 0;
    char *buf = malloc(cap + 1);
    if (!buf)
        die("malloc", path);

    for (;;) {
        if (size == cap) {
            cap *= 2;
            char *bigger = realloc(buf, cap + 1);
            if (!bigger)
                die("realloc", path);
            buf = bigger;
        }
        ssize_t r = read(fd, buf + size, cap - size);
        if (r == -1)
            die("read", path);
        if (r == 0)
            break;
        size += (size_t)r;
    }
    close(fd);
    buf[size] = '\0';
    return buf;
}

static char *skip_ws(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    return s;
}

/* Отрезает от строки следующее слово; NULL, если слов больше нет */
static char *next_token(char **cur)
{
    char *s = skip_ws(*cur);
    if (*s == '\0') {
        *cur = s;
        return NULL;
    }
    char *start = s;
    while (*s && !isspace((unsigned char)*s))
        s++;
    if (*s)
        *s++ = '\0';
    *cur = s;
    return start;
}

static size_t parse_size(const char *spec, int lineno, const char *tok)
{
    if (tok == NULL || *tok == '\0' || *tok == '-')
        die_line(spec, lineno, "ожидался размер в байтах (целое число >= 0)");
    char *end;
    errno = 0;
    unsigned long v = strtoul(tok, &end, 10);
    if (*end != '\0' || errno != 0)
        die_line(spec, lineno, "ожидался размер в байтах (целое число >= 0)");
    return (size_t)v;
}

static void process_line(const char *spec, int lineno, char *line)
{
    /* убираем хвостовые пробелы и '\r' (если файл правили в Windows) */
    size_t n = strlen(line);
    while (n > 0 && isspace((unsigned char)line[n - 1]))
        line[--n] = '\0';

    char *cur = line;
    char *cmd = next_token(&cur);
    if (cmd == NULL || cmd[0] == '#')
        return;

    if (strcmp(cmd, "dir") == 0) {
        char *path = next_token(&cur);
        if (!path)
            die_line(spec, lineno, "dir: не указан путь");
        make_dir(path);

    } else if (strcmp(cmd, "file") == 0) {
        char *path = next_token(&cur);
        char *kind = next_token(&cur);
        if (!path || !kind)
            die_line(spec, lineno,
                     "file: ожидается file <путь> <empty|text|random|zero> ...");

        if (strcmp(kind, "empty") == 0) {
            make_text(path, NULL);
        } else if (strcmp(kind, "text") == 0) {
            char *content = skip_ws(cur);
            if (*content == '\0')
                die_line(spec, lineno, "file text: не задана строка");
            make_text(path, content);
        } else if (strcmp(kind, "random") == 0) {
            make_random(path, parse_size(spec, lineno, next_token(&cur)));
        } else if (strcmp(kind, "zero") == 0) {
            make_zeros(path, parse_size(spec, lineno, next_token(&cur)));
        } else {
            die_line(spec, lineno, "file: неизвестный тип содержимого");
        }

    } else if (strcmp(cmd, "symlink") == 0) {
        char *linkpath = next_token(&cur);
        char *target = next_token(&cur);
        if (!linkpath || !target)
            die_line(spec, lineno, "symlink: ожидается symlink <ссылка> <цель>");
        make_symlink(linkpath, target);

    } else if (strcmp(cmd, "hardlink") == 0) {
        char *linkpath = next_token(&cur);
        char *existing = next_token(&cur);
        if (!linkpath || !existing)
            die_line(spec, lineno,
                     "hardlink: ожидается hardlink <ссылка> <существующий файл>");
        make_hardlink(linkpath, existing);

    } else {
        die_line(spec, lineno, "неизвестная команда (dir/file/symlink/hardlink)");
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <spec-файл>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *buf = read_whole_file(argv[1]);

    int lineno = 0;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        process_line(argv[1], ++lineno, line);
        line = nl ? nl + 1 : NULL;
    }

    free(buf);
    return EXIT_SUCCESS;
}
