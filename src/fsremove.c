/*
 * fsremove — рекурсивно удаляет каталог (средний вариант лабораторной).
 *
 * Используются только вызовы из раздела 1.2: scandir, alphasort, readlink,
 * unlink, rmdir.
 *
 * Меры безопасности:
 *  1. Символьные ссылки НЕ разыменовываются. scandir/opendir идёт по ссылке
 *     в целевой каталог, и обычный «наивный» обход мог бы удалить файлы
 *     ЗА ПРЕДЕЛАМИ дерева. Поэтому рекурсия идёт только для записей типа
 *     DT_DIR (у ссылки тип DT_LNK, она удаляется через unlink как файл).
 *  2. Если сам переданный путь — символьная ссылка, программа отказывается
 *     работать (проверка через readlink: на не-ссылке он даёт EINVAL).
 *  3. Отказ для "/", "." и "..".
 *
 * Сборка: gcc -std=c11 -Wall -Wextra -o bin/fsremove src/fsremove.c
 * Запуск: ./bin/fsremove <каталог>
 */
#define _DEFAULT_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void die(const char *op, const char *path)
{
    fprintf(stderr, "%s(%s): %s\n", op, path, strerror(errno));
    exit(EXIT_FAILURE);
}

/* фильтр для scandir: пропускаем "." и ".." */
static int not_dots(const struct dirent *e)
{
    return strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0;
}

static void remove_tree(const char *path);

static void remove_entry(const char *path, unsigned char type)
{
    if (type == DT_UNKNOWN) {
        /* ФС не сообщила тип записи: пробуем unlink, для каталога он даст EISDIR */
        if (unlink(path) == 0) {
            printf("unlink    %s\n", path);
            return;
        }
        if (errno != EISDIR)
            die("unlink", path);
        type = DT_DIR;
    }

    if (type == DT_DIR) {
        remove_tree(path);
    } else {
        /* обычные файлы, жёсткие и символьные ссылки — всё через unlink */
        printf("unlink    %s\n", path);
        if (unlink(path) == -1)
            die("unlink", path);
    }
}

static void remove_tree(const char *path)
{
    struct dirent **list;
    int n = scandir(path, &list, not_dots, alphasort);
    if (n == -1)
        die("scandir", path);

    for (int i = 0; i < n; i++) {
        char child[PATH_MAX];
        if (snprintf(child, sizeof child, "%s/%s", path, list[i]->d_name)
            >= (int)sizeof child) {
            fprintf(stderr, "слишком длинный путь: %s/%s\n", path, list[i]->d_name);
            exit(EXIT_FAILURE);
        }
        remove_entry(child, list[i]->d_type);
        free(list[i]);
    }
    free(list);

    printf("rmdir     %s\n", path);
    if (rmdir(path) == -1)
        die("rmdir", path);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <каталог>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *root = argv[1];

    if (strcmp(root, "/") == 0 || strcmp(root, ".") == 0 || strcmp(root, "..") == 0) {
        fprintf(stderr, "Отказ: '%s' удалять нельзя\n", root);
        return EXIT_FAILURE;
    }

    char tmp[PATH_MAX];
    if (readlink(root, tmp, sizeof tmp) != -1) {
        fprintf(stderr, "Отказ: '%s' — символьная ссылка, а не каталог\n", root);
        return EXIT_FAILURE;
    }
    if (errno != EINVAL)        /* EINVAL = «не ссылка» — это нормально */
        die("readlink", root);

    remove_tree(root);
    return EXIT_SUCCESS;
}
