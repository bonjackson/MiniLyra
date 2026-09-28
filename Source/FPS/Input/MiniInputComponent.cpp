#include "MiniInputComponent.h"

void UMiniInputComponent::RemoveBinds(TArray<uint32>& Handles)
{
	for (uint32 Handle : Handles)
	{
		RemoveBindingByHandle(Handle);
	}
	Handles.Reset();
}
