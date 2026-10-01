#include "MiniGameplayTags.h"

namespace MiniGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InitState_Spawned, "InitState.Spawned", "Actor or component has spawned and can receive extensions.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InitState_DataAvailable, "InitState.DataAvailable", "Required data is available.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InitState_DataInitialized, "InitState.DataInitialized", "Required data has been initialized.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InitState_GameplayReady, "InitState.GameplayReady", "Actor or component is ready for gameplay.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Move, "InputTag.Move", "Move on the ground plane.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Look, "InputTag.Look", "Rotate the local view.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Jump, "InputTag.Jump", "Jump while held.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Fire, "InputTag.Ability.Fire", "Fire input routed to GAS.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Reload, "InputTag.Ability.Reload", "Reload input routed to GAS.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_SwitchWeapon, "InputTag.Ability.SwitchWeapon", "Switch weapon input routed to GAS.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Aim, "InputTag.Ability.Aim", "Aim input routed to GAS.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Aiming, "State.Aiming", "An active aim ability owns this state.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "State.Dead", "The player cannot use regular abilities while dead.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_SpawnProtected, "State.SpawnProtected", "This life has temporary server-authoritative spawn protection.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Reloading, "State.Reloading", "Reload interrupts firing and aiming.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Gameplay_AbilityInputBlocked, "Gameplay.AbilityInputBlocked", "Gameplay ability input is blocked.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Fire, "Ability.Fire", "Firing ability family.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Jump, "Ability.Jump", "Jumping ability family.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Aim, "Ability.Aim", "Aiming ability family.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Damage, "Data.Damage", "Server-authoritative damage magnitude for the instant damage effect.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GamePhase_MiniArena_Warmup, "GamePhase.MiniArena.Warmup", "Arena warmup phase.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GamePhase_MiniArena_Playing, "GamePhase.MiniArena.Playing", "Arena playing phase.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GamePhase_MiniArena_PostMatch, "GamePhase.MiniArena.PostMatch", "Arena post-match phase.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_RifleFire, "GameplayCue.Mini.RifleFire", "Accepted rifle shot presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_PistolFire, "GameplayCue.Mini.PistolFire", "Accepted pistol shot presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_Impact, "GameplayCue.Mini.Impact", "Authoritative hit surface presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_Damage, "GameplayCue.Mini.Damage", "Authoritative victim damage presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_Death, "GameplayCue.Mini.Death", "Authoritative death presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_Reload, "GameplayCue.Mini.Reload", "Active weapon reload presentation.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Mini_HitConfirmed, "GameplayCue.Mini.HitConfirmed", "Owner-only confirmed hit event for local UI.");
}
