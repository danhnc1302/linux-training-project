#ifndef LOGGER_H
#define LOGGER_H

int logger_init(void);

int logger_log(const char *sender,
               const char *message);

void logger_close(void);

#endif