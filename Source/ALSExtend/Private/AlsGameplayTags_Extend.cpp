#include "AlsGameplayTags_Extend.h"

namespace AlsLocomotionModeTags
{
	UE_DEFINE_GAMEPLAY_TAG(Flying, FName{TEXTVIEW("Als.LocomotionMode.Flying")})
	UE_DEFINE_GAMEPLAY_TAG(Swimming, FName{TEXTVIEW("Als.LocomotionMode.Swimming")})
	UE_DEFINE_GAMEPLAY_TAG(Sliding, FName{TEXTVIEW("Als.LocomotionMode.Sliding")})
	UE_DEFINE_GAMEPLAY_TAG(FreeClimbing, FName{TEXTVIEW("Als.LocomotionMode.FreeClimbing")})
	UE_DEFINE_GAMEPLAY_TAG(RopeSwing, FName{TEXTVIEW("Als.LocomotionMode.RopeSwing")})
	UE_DEFINE_GAMEPLAY_TAG(Gliding, FName{TEXTVIEW("Als.LocomotionMode.Gliding")})
}

namespace AlsLocomotionActionTags
{
	UE_DEFINE_GAMEPLAY_TAG(AttackCombo, FName{TEXTVIEW("Als.LocomotionAction.AttackCombo")})
	UE_DEFINE_GAMEPLAY_TAG(ClimbDownLedge, FName{TEXTVIEW("Als.LocomotionAction.ClimbDownLedge")})
	UE_DEFINE_GAMEPLAY_TAG(ClimbDownFloor, FName{TEXTVIEW("Als.LocomotionAction.ClimbDownFloor")})
}