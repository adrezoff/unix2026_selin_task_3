# unix2026_selin_task_3

Репозиторий с решением задачи №3 из курса "Операционные системы Unix"

[Описание задачи](https://aragont.github.io/OS-UNIX/#%D0%B7%D0%B0%D0%B4%D0%B0%D1%87%D0%B0-3)

---

## Авторство

- **Автор:** `Селин Андрей Георгиевич`
- **email:** `andreyselin19@yandex.ru`

## Локальный запуск
1. Клонируем репозиторий
```bush
git clone https://github.com/adrezoff/unix2026_selin_task_3.git
```
2. Переходим в корень репозитория
```bush
cd unix2026_selin_task_3
```
3. Компилируем основной алгоритм
```bush
make clean
```
```bush
make
```

3. Выдаём права runme на исполнение
```bush
chmod +x runme.sh
```

4. Запускаем скрипт (время выполнения ~ 5 минут)
```bush
./runme.sh
```

5. Посмотреть log файл
```bush
cat /tmp/myinit.log
```

## Лог успешных запусков
- `Darwin MacBook-Air-Selin.local 22.6.0 Darwin Kernel Version 22.6.0: Wed Jul  5 22:22:52 PDT 2023; root:xnu-8796.141.3~6/RELEASE_ARM64_T8103 arm64`
- `Linux Ubuntu 6.17.0-14-generic #14-Ubuntu SMP PREEMPT_DYNAMIC Fri Jan  9 16:29:17 UTC 2026 aarch64 GNU/Linux`