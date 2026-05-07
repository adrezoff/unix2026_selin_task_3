#!/bin/bash

CUR=$(pwd)
CFG="$CUR/myinit.cfg"
LOG="/tmp/myinit.log"

# Очистка
pkill -9 myinit 2>/dev/null
pkill -9 sleep 2>/dev/null
rm -f "$LOG"
touch "$CUR/in"

# 1. Создаем конфиг
echo "/bin/sleep $CUR/in $CUR/out1" > "$CFG"
echo "/bin/sleep $CUR/in $CUR/out2" >> "$CFG"
echo "/bin/sleep $CUR/in $CUR/out3" >> "$CFG"

echo "--- КОМПИЛЯЦИЯ ---"
gcc myinit.c -o myinit

echo "--- СТАРТ ---"
./myinit "$CFG"
sleep 2

echo "--- ПРОВЕРКА PS (3 sleep) ---"
ps -ef | grep "[s]leep"

echo "--- УБИВАЕМ ПРОЦЕСС НОМЕР 2 ---"
P2=$(ps -ef | grep "[s]leep" | awk 'NR==2{print $2}')
kill -9 $P2
sleep 1

echo "--- ПРОВЕРКА ПОСЛЕ РЕСТАРТА ---"
ps -ef | grep "[s]leep"

echo "--- SIGHUP (1 процесс) ---"
echo "/bin/sleep $CUR/in $CUR/out_single" > "$CFG"
pkill -HUP myinit
sleep 1

echo "--- ФИНАЛЬНАЯ ПРОВЕРКА ---"
ps -ef | grep "[s]leep"

echo "--- ЛОГ ---"
cat "$LOG"