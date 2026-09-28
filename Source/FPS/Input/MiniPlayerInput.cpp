#include "MiniPlayerInput.h"

#include "InputMappingContext.h"

int32 UMiniPlayerInput::GetMappingRegistrationCount(const UInputMappingContext* MappingContext) const
{
	const FAppliedInputContextData* Data = MappingContext
		? GetAppliedInputContextData().Find(MappingContext) : nullptr;
	return Data ? Data->RegistrationCount : 0;
}
