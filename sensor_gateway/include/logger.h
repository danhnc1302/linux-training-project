#ifndef LOGGER_H
#define LOGGER_H

#define LOG_FIFO_NAME   "logFifo"
#define LOG_FILE_NAME   "gateway.log"
#define LOG_MSG_MAX     200         /* section 3.8: message max 200 bytes */

/* Open the FIFO for writing (blocks until the log process opens it).
 * Return 0 on success, -1 on error. */
int logger_init(void);

/* Send "<source> <message>\n" to the log process with ONE write()
 * (<= PIPE_BUF, so it is atomic). Thread-safe. Ignored before init. */
void logger_log(const char *source, const char *message);

/* Close the write end: the log process then reads EOF and exits. */
void logger_close(void);

#endif
