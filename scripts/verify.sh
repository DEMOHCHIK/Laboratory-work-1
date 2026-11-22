#!/bin/sh
# verify.sh <spec>
#
# Автопроверка одной структуры:
#   1. строит дерево C-программой (bin/fsbuild) и sh-скриптом (spec2sh.sh);
#   2. сравнивает деревья: типы, пути, размеры, цели символьных ссылок,
#      счётчики жёстких ссылок;
#   3. проверяет, что жёсткие ссылки действительно указывают на один i-node;
#   4. сравнивает содержимое всех не-случайных файлов;
#   5. проверяет bin/fsremove, в том числе что он НЕ заходит по символьной
#      ссылке за пределы дерева.
#
# Нужен GNU find/stat (обычный Linux).

set -u

[ $# -eq 1 ] || { echo "Использование: $0 <spec>" >&2; exit 2; }

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SPEC=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

fail=0
ok()  { printf '  [ OK ]  %s\n' "$1"; }
bad() { printf '  [FAIL]  %s\n' "$1"; fail=1; }

echo "== $1"
mkdir "$TMP/c" "$TMP/sh"

# --- 1. построение двумя способами ---------------------------------------
( cd "$TMP/c" && "$ROOT/bin/fsbuild" "$SPEC" >/dev/null ) \
    || { bad "fsbuild завершился с ошибкой"; exit 1; }
sh "$ROOT/scripts/spec2sh.sh" "$SPEC" > "$TMP/build.sh" \
    || { bad "spec2sh не смог разобрать spec"; exit 1; }
( cd "$TMP/sh" && sh "$TMP/build.sh" >/dev/null 2>&1 ) \
    || { bad "сгенерированный sh-скрипт завершился с ошибкой"; exit 1; }
ok "дерево построено C-программой и sh-скриптом"

# --- 2. сравнение структуры ----------------------------------------------
signature() {
    ( cd "$1" && find . -mindepth 1 \
        \( -type d -printf 'd %p\n' \) -o \
        \( -type l -printf 'l %p -> %l\n' \) -o \
        \( -type f -printf 'f %p size=%s links=%n\n' \) | sort )
}
signature "$TMP/c"  > "$TMP/c.sig"
signature "$TMP/sh" > "$TMP/sh.sig"
if diff "$TMP/c.sig" "$TMP/sh.sig" >/dev/null; then
    ok "структура совпадает ($(wc -l < "$TMP/c.sig") объектов: типы, размеры, цели ссылок, links)"
else
    bad "структуры различаются:"
    diff "$TMP/c.sig" "$TMP/sh.sig"
fi

# --- 3. жёсткие ссылки = один i-node ---------------------------------------
awk '{ sub(/\r$/, "") } $1 == "hardlink" { print $2, $3 }' "$SPEC" > "$TMP/hl.txt"
while read -r l e; do
    [ -n "$l" ] || continue
    for t in c sh; do
        if [ "$(stat -c %i "$TMP/$t/$l")" = "$(stat -c %i "$TMP/$t/$e")" ]; then
            ok "[$t] $l и $e — один i-node"
        else
            bad "[$t] $l и $e — РАЗНЫЕ i-node"
        fi
    done
done < "$TMP/hl.txt"

# --- 4. содержимое детерминированных файлов --------------------------------
awk '{ sub(/\r$/, "") } $1 == "file" && $3 != "random" { print $2 }' "$SPEC" > "$TMP/files.txt"
checked=0; mismatch=0
while read -r f; do
    [ -n "$f" ] || continue
    checked=$((checked + 1))
    if ! cmp -s "$TMP/c/$f" "$TMP/sh/$f"; then
        bad "содержимое $f различается"
        mismatch=1
    fi
done < "$TMP/files.txt"
[ "$mismatch" -eq 0 ] && ok "содержимое совпало у $checked файлов (empty/text/zero)"

# --- 5. fsremove ------------------------------------------------------------
# «Ловушка»: символьная ссылка внутри дерева на каталог ВНЕ дерева.
mkdir "$TMP/precious" && : > "$TMP/precious/keep.txt"
first=""
for d in "$TMP/c"/*; do
    if [ -d "$d" ] && [ ! -L "$d" ]; then first=$d; break; fi
done
[ -n "$first" ] && ln -s "$TMP/precious" "$first/trap_link"

for d in "$TMP/c"/*; do
    if [ -d "$d" ] && [ ! -L "$d" ]; then
        "$ROOT/bin/fsremove" "$d" >/dev/null || bad "fsremove упал на $d"
    fi
done

if [ -z "$(find "$TMP/c" -mindepth 1 -type d)" ]; then
    ok "fsremove удалил все каталоги дерева"
else
    bad "после fsremove остались каталоги"
fi
if [ -f "$TMP/precious/keep.txt" ]; then
    ok "fsremove не пошёл по символьной ссылке за пределы дерева"
else
    bad "fsremove удалил файл вне дерева!"
fi

if [ "$fail" -eq 0 ]; then echo "== PASSED"; else echo "== FAILED"; exit 1; fi
