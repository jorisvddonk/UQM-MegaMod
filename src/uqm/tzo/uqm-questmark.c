#include "tzo.h"
#include "json_ez.h"
#include "uqm-questmark.h"

#include "uqm/comm.h"
#include "uqm/commglue.h"
#include "uqm/resinst.h"
#include "uqm/globdata.h"

static TzoVM *vm;

void emit(TzoVM *vm)
{
	char *str = asString(_pop(vm));
	printf("%s ", str);
	char* clip = "MISSING.ogg";
	SpliceTrack(clip, str, NULL, NULL);
}

void getresponse(TzoVM *vm)
{
	tzo_pause(vm);
	// actual response handling done by main game engine!
}

void response(TzoVM *vm)
{
	Value a = _pop(vm); // number
	Value b = _pop(vm); // string
	int pc = a.number_value;
	char *str = asString(b);
	DoResponsePhrase(pc, &ResponseHandler, str);
}

void getCaptainName(TzoVM *vm)
{
	_push(vm, *makeString(GLOBAL_SIS (CommanderName)));
}

void getShipName(TzoVM *vm)
{
	_push(vm, *makeString(GLOBAL_SIS (ShipName)));
}

static void
ResponseHandler (RESPONSE_REF R)
{
	_push(vm, *makeNumber(R));
	char* asd = "\n";
	char* clip = "MISSING.ogg";
	SpliceTrack(clip, asd, NULL, NULL);
	tzo_run(vm);
}

static void
TzoIntro (void)
{
	char* asd = "\n";
	char* clip = "MISSING.ogg";
	SpliceTrack(clip, asd, NULL, NULL);
	tzo_run(vm);
}

/**
 * Initialize a Tzo VM, and replace the input LOCDATA's init_encounter function with a function
 * that starts up the Tzo/QuestMark VM and executes it
 */
LOCDATA*
replaceWithQuestMarkConversation (LOCDATA *retval)
{
	
	vm = createTzoVM();
	initRuntime(vm);
	registerForeignFunction(vm, "emit", &emit);
	registerForeignFunction(vm, "response", &response);
	registerForeignFunction(vm, "getResponse", &getresponse);
	registerForeignFunction(vm, "getCaptainName", &getCaptainName);
	registerForeignFunction(vm, "getShipName", &getShipName);
	struct json_value_s *root = loadFileGetJSON(vm, "questmark_in.json"); // TODO: load via generic package file loading system
	struct json_object_s *rootObj = json_value_as_object(root);
	struct json_array_s *inputProgram = get_object_key_as_array(rootObj, "programList");
	struct json_object_s *labelMap = get_object_key_as_object(rootObj, "labelMap");
	if (labelMap != NULL)
	{
		initLabelMapFromJSONObject(vm, labelMap);
	}
	initProgramListFromJSONArray(vm, inputProgram);

	retval->init_encounter_func = TzoIntro; // replace init encounter function with Tzo-enabled encounter function

	return (retval);
}
