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

# запускаем в фоне и сохраняем PID (важно для SIGHUP)
./myinit "$CFG" &
MYINIT_PID=$!

sleep 2

echo "--- ПРОВЕРКА PS (3 sleep) ---"
ps -ef | grep "[s]leep"

echo "--- УБИВАЕМ ПРОЦЕСС НОМЕР 2 ---"

# стабильный поиск второго процесса (НЕ через NR==2)
P2=$(pgrep -f "/bin/sleep.*out2" | head -n 1)

if [ ! -z "$P2" ]; then
    kill -9 $P2
    echo "Убит PID $P2. Ждем рестарта..."
    sleep 1
fi

echo "--- ПРОВЕРКА ПОСЛЕ РЕСТАРТА ---"
ps -ef | grep "[s]leep"

echo "--- SIGHUP (смена на 1 процесс) ---"

echo "/bin/sleep $CUR/in $CUR/out_single" > "$CFG"

# отправляем сигнал строго в нужный myinit
kill -HUP $MYINIT_PID
sleep 1

echo "--- ФИНАЛЬНАЯ ПРОВЕРКА ---"
ps -ef | grep "[s]leep"

echo "--- ЛОГ ---"
cat "$LOG"

echo "--- ПРОВЕРКА КОЛИЧЕСТВА PROCESSES ---"
echo "sleep count: $(pgrep -f /bin/sleep | wc -l)"