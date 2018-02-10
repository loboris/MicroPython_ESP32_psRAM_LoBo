
#include "idf_reent.h"


static struct _idf_reent s_r;

struct _idf_reent* __idf_getreent() {
  return &s_r;
}


