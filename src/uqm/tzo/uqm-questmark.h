#include "tzo.h"
#include "uqm/commglue.h"

void emit(TzoVM *vm);

void getresponse(TzoVM *vm);

void response(TzoVM *vm);

static void
ResponseHandler (RESPONSE_REF R);

static void
Intro2 (void);

LOCDATA*
replaceWithQuestMarkConversation (LOCDATA *retval);