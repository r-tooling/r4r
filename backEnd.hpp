#pragma once
#include "middleEnd.hpp"

void report(const MiddleEndState&);
void chrootBased(const MiddleEndState&);
void csvBased(const MiddleEndState&, absFilePath output);
