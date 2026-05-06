#!/bin/bash

# Компилируем myinit с помощью make
echo "Compiling myinit..."
make clean
make

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

# Создаем тестовые скрипты
echo "Creating test scripts..."

# Создаем каталоги для тестов
mkdir -p /tmp/myinit_test
mkdir -p /tmp/myinit_test/in
mkdir -p /tmp/myinit_test/out

# Тестовый процесс 1: бесконечный цикл
cat > /tmp/myinit_test/test1.sh << 'EOF'
#!/bin/bash
while true; do
    echo "Test process 1 running at $(date)" >> /tmp/myinit_test/out1
    sleep 5
done
EOF

# Тестовый процесс 2: бесконечный цикл
cat > /tmp/myinit_test/test2.sh << 'EOF'
#!/bin/bash
while true; do
    echo "Test process 2 running at $(date)" >> /tmp/myinit_test/out2
    sleep 5
done
EOF

# Тестовый процесс 3: бесконечный цикл
cat > /tmp/myinit_test/test3.sh << 'EOF'
#!/bin/bash
while true; do
    echo "Test process 3 running at $(date)" >> /tmp/myinit_test/out3
    sleep 5
done
EOF

# Делаем скрипты исполняемыми
chmod +x /tmp/myinit_test/test1.sh
chmod +x /tmp/myinit_test/test2.sh
chmod +x /tmp/myinit_test/test3.sh

# Создаем файлы для ввода/вывода
touch /tmp/myinit_test/in1 /tmp/myinit_test/out1
touch /tmp/myinit_test/in2 /tmp/myinit_test/out2
touch /tmp/myinit_test/in3 /tmp/myinit_test/out3

# Создаем начальный конфигурационный файл
cat > /tmp/myinit_test/config.txt << EOF
/tmp/myinit_test/test1.sh /tmp/myinit_test/in1 /tmp/myinit_test/out1
/tmp/myinit_test/test2.sh /tmp/myinit_test/in2 /tmp/myinit_test/out2
/tmp/myinit_test/test3.sh /tmp/myinit_test/in3 /tmp/myinit_test/out3
EOF

# Очищаем лог-файл
rm -f /tmp/myinit.log

# Запускаем myinit
echo "Starting myinit..."
./myinit /tmp/myinit_test/config.txt &

# Сохраняем PID myinit
MYINIT_PID=$!
echo "myinit started with PID: $MYINIT_PID"

# Даем время на запуск всех процессов
sleep 3

# Проверяем, что запущены 3 дочерних процесса
echo ""
echo "=== Test 1: Checking that 3 child processes are running ==="
ps aux | grep "test[123].sh" | grep -v grep
CHILD_COUNT=$(ps aux | grep "test[123].sh" | grep -v grep | wc -l)
echo "Number of child processes: $CHILD_COUNT"

if [ $CHILD_COUNT -eq 3 ]; then
    echo "✓ Test 1 passed: 3 processes running"
else
    echo "✗ Test 1 failed: Expected 3 processes, found $CHILD_COUNT"
fi

# Убиваем процесс номер 2 (средний)
echo ""
echo "=== Test 2: Killing process 2 ==="
PID2=$(ps aux | grep "test2.sh" | grep -v grep | awk '{print $2}' | head -1)
if [ -n "$PID2" ]; then
    echo "Killing process 2 (PID=$PID2)..."
    kill -9 $PID2
else
    echo "Process 2 not found!"
fi

# Ждем перезапуска
sleep 3

# Проверяем, что снова 3 процесса
echo ""
echo "=== Test 2a: Checking that all 3 processes are running again ==="
ps aux | grep "test[123].sh" | grep -v grep
NEW_CHILD_COUNT=$(ps aux | grep "test[123].sh" | grep -v grep | wc -l)
echo "Number of child processes after restart: $NEW_CHILD_COUNT"

if [ $NEW_CHILD_COUNT -eq 3 ]; then
    echo "✓ Test 2 passed: Process was restarted, 3 processes running"
else
    echo "✗ Test 2 failed: Expected 3 processes, found $NEW_CHILD_COUNT"
fi

# Создаем новый конфигурационный файл только с одним процессом
echo ""
echo "=== Test 3: Changing config and sending SIGHUP ==="
cat > /tmp/myinit_test/config_new.txt << EOF
/tmp/myinit_test/test1.sh /tmp/myinit_test/in1 /tmp/myinit_test/out1
EOF

# Копируем новый конфиг на место старого
cp /tmp/myinit_test/config_new.txt /tmp/myinit_test/config.txt

# Находим PID myinit
MYINIT_PID=$(pgrep myinit)
if [ -n "$MYINIT_PID" ]; then
    echo "Sending SIGHUP to myinit (PID=$MYINIT_PID)..."
    kill -HUP $MYINIT_PID
else
    echo "myinit not found!"
fi

# Ждем применения нового конфига
sleep 3

# Проверяем, что запущен только один процесс
echo ""
echo "=== Test 3a: Checking that only 1 child process is running ==="
FINAL_COUNT=$(ps aux | grep "test[123].sh" | grep -v grep | wc -l)
ps aux | grep "test[123].sh" | grep -v grep

if [ $FINAL_COUNT -eq 1 ]; then
    echo "✓ Test 3 passed: Only 1 process running after SIGHUP"
else
    echo "✗ Test 3 failed: Expected 1 process, found $FINAL_COUNT"
fi

# Проверяем содержимое лог-файла
echo ""
echo "=== Log file contents ==="
if [ -f /tmp/myinit.log ]; then
    cat /tmp/myinit.log
    echo ""

    # Проверяем наличие ожидаемых записей в логе
    if grep -q "Started process 0" /tmp/myinit.log && \
       grep -q "Started process 1" /tmp/myinit.log && \
       grep -q "Started process 2" /tmp/myinit.log; then
        echo "✓ Log contains start entries for all 3 processes"
    else
        echo "✗ Log missing start entries"
    fi

    if grep -q "exited" /tmp/myinit.log && \
       grep -q "Restarting process 1" /tmp/myinit.log; then
        echo "✓ Log contains process restart information"
    else
        echo "✗ Log missing restart information"
    fi

    if grep -q "SIGHUP received" /tmp/myinit.log && \
       grep -q "Configuration reloaded" /tmp/myinit.log; then
        echo "✓ Log contains SIGHUP handling information"
    else
        echo "✗ Log missing SIGHUP information"
    fi
else
    echo "Log file not found!"
fi

# Очистка
echo ""
echo "=== Cleaning up ==="
pkill -f "test[123].sh"
pkill myinit
make clean

echo ""
echo "All tests completed!"

# Выводим итоговый результат
if [ $CHILD_COUNT -eq 3 ] && [ $NEW_CHILD_COUNT -eq 3 ] && [ $FINAL_COUNT -eq 1 ]; then
    echo "✓ ALL TESTS PASSED!"
    exit 0
else
    echo "✗ SOME TESTS FAILED!"
    exit 1
fi