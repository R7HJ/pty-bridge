# Утилита pty_bridge
Cоздёт пару связанных между собой виртуальных последовательных портов (ttyV0 и ttyV1) для любых программ. Например, чтобы соединить между собой [(X)NET](https://www.qsl.net/swlkp3/Packet/(X)Net/) и [TNCD](https://github.com/ben-kuhn/tncd).

Компилируем код командой 
`gcc -O2 -Wall -Wextra -o pty_bridge pty_bridge.c -lutil`

Запускаем утилиту с выводом в консоль 
`./pty_bridge`

Запускаем утилиту как демон 
`./pty_bridge -d`

Проверяем что порты были созданы 
`ls -l /tmp/ttyV0 /tmp/ttyV1`

Останавливаем демона 
`sudo pkill -TERM pty_bridge`

Если ваша программа не захочет работать с виртуальными портами находящимися в /tmp (/tmp/ttyV0 /tmp/ttyV1), тогда измените `#define LINK0  "/tmp/ttyV0"` на `#define LINK0  "/dev/ttyV0"` и также для другого порта. В этом случае запускать нужно от root `sudo ./pty_bridge`.
