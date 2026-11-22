#!/bin/sh
# spec2sh.sh <spec> > build.sh
#
# Превращает описание структуры (spec) в обычный sh-скрипт из команд
# mkdir / touch / printf / head / truncate / ln — то есть во второй пункт
# лабораторной. Формат spec описан в src/fsbuild.c.

[ $# -eq 1 ] || { echo "Использование: $0 <spec>" >&2; exit 2; }

awk '
# Оборачивает строку в одинарные кавычки для sh (внутренние кавычки экранируются)
function sq(s,   out, i, c) {
    out = q
    for (i = 1; i <= length(s); i++) {
        c = substr(s, i, 1)
        if (c == q) out = out q "\\" q q
        else        out = out c
    }
    return out q
}

BEGIN {
    q = sprintf("%c", 39)
    print "#!/bin/sh"
    print "# Сгенерировано scripts/spec2sh.sh из spec-файла"
    print "set -e"
}

{ sub(/\r$/, "") }
/^[ \t]*(#|$)/ { next }

$1 == "dir"      { print "mkdir " sq($2); next }
$1 == "symlink"  { print "ln -s " sq($3) " " sq($2); next }
$1 == "hardlink" { print "ln " sq($3) " " sq($2); next }

$1 == "file" && $3 == "empty"  { print "touch " sq($2); next }
$1 == "file" && $3 == "random" { print "head -c " $4 " /dev/urandom > " sq($2); next }
$1 == "file" && $3 == "zero"   { print "truncate -s " $4 " " sq($2); next }
$1 == "file" && $3 == "text" {
    t = $0
    sub(/^[ \t]*file[ \t]+[^ \t]+[ \t]+text[ \t]+/, "", t)
    sub(/[ \t]+$/, "", t)
    # printf %s не добавляет перевод строки (в отличие от echo)
    print "printf " sq("%s") " " sq(t) " > " sq($2)
    next
}

{
    printf "spec2sh: строка %d: не понимаю: %s\n", NR, $0 > "/dev/stderr"
    bad = 1
}
END { exit bad }
' "$1"
