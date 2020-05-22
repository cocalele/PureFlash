//
// Created by lele on 5/24/20.
//

#ifndef PUREFLASH_PFERRORHANDLER_H
#define PUREFLASH_PFERRORHANDLER_H


#include <pf_message.h>
#include "pf_dispatcher.h"

class PfErrorHandler {
public:
    int submit_error(IoSubTask* t, PfMessageStatus sc);
};


#endif //PUREFLASH_PFERRORHANDLER_H
