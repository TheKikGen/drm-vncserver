#ifndef TKLOG_H
#define TKLOG_H

#include <stdio.h>

#define tklog_fatal(m...) printf(m)
#define tklog_info(m...) printf(m)
#define tklog_debug(m...) printf(m)
#define tklog_error(m...) printf(m)
#define tklog_warn(m...) printf(m)

#endif // TKLOG_H
