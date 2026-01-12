#ifndef HISTORY_H
#define HISTORY_H

#include "common.h"

struct blob;

struct change;

void history_init(struct change **history);
void history_free(struct change **history);
void history_save(struct change **history, enum change_type type, struct blob *blob, size_t pos, size_t len);
bool history_step(struct change **history, struct blob *blob, struct change **target, size_t *pos);

#endif
