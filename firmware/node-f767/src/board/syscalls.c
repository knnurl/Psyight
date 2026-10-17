/* printf to the ST-LINK virtual COM port (USART3, 115200 8N1). newlib-nano calls _write; the
 * other system calls come from nosys.specs. Blocking: about 87 us per character, so the main
 * loop prints only short status lines. */
#include "stm32f7xx_hal.h"

extern UART_HandleTypeDef g_uart3;

int _write(int file, char *data, int length) {
    (void)file;
    HAL_UART_Transmit(&g_uart3, (uint8_t *)data, (uint16_t)length, 100);
    return length;
}

/* The rest of newlib's system interface, as stubs: there is no file system or process. */
#include <errno.h>
#include <sys/stat.h>

int _close(int file) {
    (void)file;
    return -1;
}

int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR; /* Character device: printf stays line-buffered. */
    return 0;
}

int _isatty(int file) {
    (void)file;
    return 1;
}

int _lseek(int file, int offset, int whence) {
    (void)file;
    (void)offset;
    (void)whence;
    return 0;
}

int _read(int file, char *data, int length) {
    (void)file;
    (void)data;
    (void)length;
    return 0;
}

int _getpid(void) { return 1; }

int _kill(int pid, int signal) {
    (void)pid;
    (void)signal;
    errno = EINVAL;
    return -1;
}
