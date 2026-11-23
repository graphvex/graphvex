#ifndef GRAPHVEX_API_TEST_H
#define GRAPHVEX_API_TEST_H

#include "vex.h"

/**
 * Single suite entry in the consolidated graphvex/api automated test runner.
 * name: CLI filter token matching argv[1].
 * run: suite entrypoint executed when selected.
 */
typedef struct SuiteEntry {
    string name;
    fn (*run)();
} SuiteEntry;

#endif
