#include "bulk_probe.h"
void BulkProbe::begin() { memset(_ring, 0, sizeof _ring); _hist[0][0] = 1.0f; }
