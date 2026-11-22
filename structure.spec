# Пример из методички: корень "a"
dir      a
dir      a/b
dir      a/c
dir      a/e
symlink  a/d  c
file     a/b/cat.txt  text catts
file     a/c/dog.txt  empty
hardlink a/e/f.txt  a/b/cat.txt
file     a/e/g.bin  random 500
file     a/e/h.bin  zero 35
