#ifndef KORE_MARKOV_H
#define KORE_MARKOV_H

#include "../../include/kore_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void generateMarkovText(Expression expr, char* out_buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* KORE_MARKOV_H */
