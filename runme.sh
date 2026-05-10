#!/bin/bash

CUR=$(pwd)
CFG="$CUR/myinit.cfg"
LOG="/tmp/myinit.log"

# очистка
pkill -9 myinit 2>/dev/null
pkill -9 sleep 2>/dev/null
rm -f "$LOG" /tmp/myinit.pid

# служебные файлы
echo "init-test" > "$CUR/in"
echo "init-test" > "$CUR/out1"
echo "init-test" > "$CUR/out2"
echo "init-test" > "$CUR/out3"
echo "init-test" > "$CUR/out_single"

echo "--- СТАРТ ---"

# компиляция если нужно
if [ ! -f "./myinit" ]; then
    echo "Ошибка: myinit не найден. Сначала make"
    exit 1
fi

# конфиг с 3 процессами
cat > "$CFG" << EOF
/bin/sleep 100 $CUR/in $CUR/out1
/bin/sleep 100 $CUR/in $CUR/out2
/bin/sleep 100 $CUR/in $CUR/out3
EOF

# запуск myinit (демонизируется сам)
./myinit "$CFG"
sleep 2

# находим реальный PID демона
MYINIT_PID=$(pgrep myinit | head -1)
if [ -z "$MYINIT_PID" ]; then
    echo "Ошибка: myinit не запустился"
    exit 1
fi
echo "myinit PID: $MYINIT_PID"

sleep 2

echo "--- ПРОВЕРКА PS (3 sleep) ---"
ps -ef | grep "[s]leep"

# убиваем один из дочерних процессов
P2=$(ps -ef | grep "/bin/sleep 100" | head -1 | awk '{print $2}')
if [ -n "$P2" ]; then
    echo "Убиваем процесс: $P2"
    kill -9 "$P2"
else
    echo "Процесс не найден"
fi

sleep 3

echo "--- ПРОВЕРКА ПОСЛЕ РЕСТАРТА ---"
ps -ef | grep "[s]leep"

echo "--- SIGHUP (переконфиг на 1 процесс) ---"

cat > "$CFG" << EOF
/bin/sleep 200 $CUR/in $CUR/out_single
EOF

kill -HUP "$MYINIT_PID"
sleep 3

echo "--- ФИНАЛЬНАЯ ПРОВЕРКА ---"
ps -ef | grep "[s]leep"

echo "--- ЛОГ ---"
cat "$LOG"

echo "--- КОЛИЧЕСТВО sleep ---"
ps -ef | grep "[s]leep" | wc -l

# очистка
kill "$MYINIT_PID" 2>/dev/null
pkill sleep 2>/dev/null
rm -f "$CUR"/in "$CUR"/out*