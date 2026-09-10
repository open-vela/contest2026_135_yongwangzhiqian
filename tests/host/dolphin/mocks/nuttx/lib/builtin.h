/* SPDX-License-Identifier: Apache-2.0 */
#ifndef __MOCK_NUTTX_LIB_BUILTIN_H
#define __MOCK_NUTTX_LIB_BUILTIN_H
#define FAR
#include <sys/types.h>
typedef int (*main_t)(int argc, char *argv[]);
struct builtin_s
{
  const char *name;
  int priority;
  int stacksize;
  main_t main;
};
int builtin_isavail(const char *appname);
const struct builtin_s *builtin_for_index(int index);
#endif
