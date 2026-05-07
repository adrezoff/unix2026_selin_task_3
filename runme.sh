#!/bin/bash

CUR=$(pwd)
CFG="$CUR/myinit.cfg"
LOG="/tmp/myinit.log"

# Очистка предыдущих запусков и лога
pkill -9 myinit 2>/dev/null
pkill -9 sleep 2>/dev/null
rm -f "$LOG"
touch "$CUR/in"

echo "/bin/sleep $CUR/in $CUR/out1" > "$CFG"
echo "/bin/sleep $CUR/in $CUR/out2" >> "$CFG"
echo "/bin/sleep $CUR/in $CUR/out3" >> "$CFG"

echo "--- СТАРТ ---"
if [ ! -f "./myinit" ]; then
    echo "Ошибка: исполняемый файл myinit не найден. Сначала выполните make."
    exit 1
fi

./myinit "$CFG"
sleep 2

echo "--- ПРОВЕРКА PS (3 sleep) ---"
ps -ef | grep "[s]leep"

echo "--- УБИВАЕМ ПРОЦЕСС НОМЕР 2 ---"
P2=$(ps -ef | grep "[s]leep" | awk 'NR==2{print $2}')
if [ ! -z "$P2" ]; then
    kill -9 $P2
    echo "Убит PID $P2. Ждем рестарта..."
    sleep 1
fi

echo "--- ПРОВЕРКА ПОСЛЕ РЕСТАРТА ---"
ps -ef | grep "[s]leep"

echo "--- SIGHUP (смена на 1 процесс) ---"
echo "/bin/sleep $CUR/in $CUR/out_single" > "$CFG"
pkill -HUP myinit
sleep 1

echo "--- ФИНАЛЬНАЯ ПРОВЕРКА ---"
ps -ef | grep "[s]leep"

echo "--- ЛОГ ---"
cat "$LOG"