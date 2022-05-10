#ifndef __COMMAND_H
#define __COMMAND_H

#include "ctr.h" // DECL_CTR

// Declare a constant exported to the host
#define DECL_CONSTANT(NAME, VALUE)                              \
    DECL_CTR_INT("DECL_CONSTANT " NAME, 1, CTR_INT(VALUE))
#define DECL_CONSTANT_STR(NAME, VALUE)                  \
    DECL_CTR("DECL_CONSTANT_STR " NAME " " VALUE)

// Declare an enumeration
#define DECL_ENUMERATION(ENUM, NAME, VALUE)                             \
    DECL_CTR_INT("DECL_ENUMERATION " ENUM " " NAME, 1, CTR_INT(VALUE))
#define DECL_ENUMERATION_RANGE(ENUM, NAME, VALUE, COUNT)        \
    DECL_CTR_INT("DECL_ENUMERATION_RANGE " ENUM " " NAME,       \
                 2, CTR_INT(VALUE), CTR_INT(COUNT))

#endif // command.h
