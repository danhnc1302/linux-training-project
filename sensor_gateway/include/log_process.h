#ifndef LOG_PROCESS_H
#define LOG_PROCESS_H

/* Body of the log process (child created by fork()).
 * Reads "<source> <message>\n" from the FIFO and writes
 * "<seq> <YYYY-MM-DD HH:MM:SS> <source> <message>" to gateway.log.
 * Returns when all writers have closed the FIFO (EOF). */
void run_log_process(void);

#endif
