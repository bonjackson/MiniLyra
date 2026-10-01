#include "MiniDamageResult.h"

const TCHAR* MiniDamageTargetKindToString(EMiniDamageTargetKind Kind)
{
	switch (Kind)
	{
	case EMiniDamageTargetKind::Player: return TEXT("Player");
	case EMiniDamageTargetKind::PracticeTarget: return TEXT("PracticeTarget");
	default: return TEXT("None");
	}
}
