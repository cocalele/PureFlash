//
// Created by lele on 5/24/20.
//

#include "pf_error_handler.h"

int PfErrorHandler::submit_error(IoSubTask* t, PfMessageStatus sc)
{
    S5LOG_FATAL("%s not implemented", __FUNCTION__);
    //error report may for: 1) normal IO, 2) internal Io like CoW, recoverying
}
