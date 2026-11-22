# Более хитрая структура: ссылки в подкаталогах, висячая ссылка, текст с пробелами
dir      lab
dir      lab/docs
dir      lab/data
dir      lab/backup
file     lab/docs/readme.txt  text Hello, UNIX! It's a file.
file     lab/data/blob.bin    random 1024
file     lab/data/zeros.bin   zero 4096
file     lab/data/empty.log   empty
# жёсткая ссылка: путь от текущего каталога
hardlink lab/backup/blob.copy lab/data/blob.bin
# символьная ссылка: цель отсчитывается от каталога САМОЙ ссылки
symlink  lab/docs/to-data  ../data
symlink  lab/latest        data/blob.bin
# висячая ссылка: цели не существует
symlink  lab/dangling      nowhere.txt
