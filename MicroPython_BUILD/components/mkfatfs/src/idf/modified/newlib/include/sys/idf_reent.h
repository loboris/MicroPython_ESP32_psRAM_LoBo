#pragma once


struct _idf_reent {
  int _errno;
};

struct _idf_reent* __idf_getreent();


