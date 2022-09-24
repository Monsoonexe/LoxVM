#pragma once

#include "object.h"
#include "scanner.h"
#include "vm.h"

// test this
#define MAX_NESTED_CALLS UINT16_MAX

ObjectFunction* compile(const char* source);
void markCompilerRoots();