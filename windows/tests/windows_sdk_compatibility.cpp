// rpcndr.h defines this legacy macro before app headers in native builds.
// Keep the pure model usable in that include order without requiring a Windows SDK here.
#define small char
#include "features/tasks/task_model.h"
#undef small
