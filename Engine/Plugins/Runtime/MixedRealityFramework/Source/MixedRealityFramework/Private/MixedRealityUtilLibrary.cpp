// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MixedRealityUtilLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Components/MaterialBillboardComponent.h"
#include "Misc/ConfigCacheIni.h" // for GetFloat()

/* MixedRealityUtilLibrary_Impl 
 *****************************************************************************/

namespace MixedRealityUtilLibrary_Impl
{
	static bool IsActorOwnedByPlayer(AActor* ActorInst, ULocalPlayer* Player);
}

static bool MixedRealityUtilLibrary_Impl::IsActorOwnedByPlayer(AActor* ActorInst, ULocalPlayer* Player)
{
	bool bIsOwned = false;
	if (ActorInst)
	{
		if (UWorld* ActorWorld = ActorInst->GetWorld())
		{
			APlayerController* Controller = Player->GetPlayerController(ActorWorld);
			if (Controller && ActorInst->IsOwnedBy(Controller))
			{
				bIsOwned = true;
			}
			else if (Controller)
			{
				APawn* PlayerPawn = Controller->GetPawnOrSpectator();
				if (PlayerPawn && ActorInst->IsOwnedBy(PlayerPawn))
				{
					bIsOwned = true;
				}
				else if (USceneComponent* ActorRoot = ActorInst->GetRootComponent())
				{
					USceneComponent* AttachParent = ActorRoot->GetAttachParent();
					if (AttachParent)
					{
						bIsOwned = IsActorOwnedByPlayer(AttachParent->GetOwner(), Player);
					}
				}
			}
		}
	}
	return bIsOwned;
}

/* UMixedRealityUtilLibrary 
 *****************************************************************************/

UMixedRealityUtilLibrary::UMixedRealityUtilLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMixedRealityUtilLibrary::SetMaterialBillboardSize(UMaterialBillboardComponent* Target, float NewSizeX, float NewSizeY)
{
	bool bMarkRenderStateDirty = false;
	for (FMaterialSpriteElement& Sprite : Target->Elements)
	{
		if (Sprite.BaseSizeX != NewSizeX || Sprite.BaseSizeY != NewSizeY)
		{
			Sprite.BaseSizeX = NewSizeX;
			Sprite.BaseSizeY = NewSizeY;
			bMarkRenderStateDirty = true;
		}
	}

	if (bMarkRenderStateDirty)
	{
		Target->MarkRenderStateDirty();
	}
}

APawn* UMixedRealityUtilLibrary::FindAssociatedPlayerPawn(AActor* ActorInst)
{
	APawn* PlayerPawn = nullptr;

	if (UWorld* TargetWorld = ActorInst->GetWorld())
	{
		const TArray<ULocalPlayer*>& LocalPlayers = GEngine->GetGamePlayers(TargetWorld);
		for (ULocalPlayer* Player : LocalPlayers)
		{
			if (MixedRealityUtilLibrary_Impl::IsActorOwnedByPlayer(ActorInst, Player))
			{
				PlayerPawn = Player->GetPlayerController(TargetWorld)->GetPawnOrSpectator();
				break;
			}
		}
	}
	return PlayerPawn;
}

USceneComponent* UMixedRealityUtilLibrary::FindAssociatedHMDRoot(AActor* ActorInst)
{
	APawn* PlayerPawn = FindAssociatedPlayerPawn(ActorInst);
	return UMixedRealityUtilLibrary::GetHMDRootComponent(PlayerPawn);
}

USceneComponent* UMixedRealityUtilLibrary::GetHMDRootComponent(const UObject* WorldContextObject, int32 PlayerIndex)
{
	const APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(WorldContextObject, PlayerIndex);
	return UMixedRealityUtilLibrary::GetHMDRootComponent(PlayerPawn);
}

USceneComponent* UMixedRealityUtilLibrary::GetHMDRootComponent(const APawn* PlayerPawn)
{
	USceneComponent* VROrigin = nullptr;
	if (UCameraComponent* HMDCamera = UMixedRealityUtilLibrary::GetHMDCameraComponent(PlayerPawn))
	{
		VROrigin = HMDCamera->GetAttachParent();
	}
	return VROrigin;
}

UCameraComponent* UMixedRealityUtilLibrary::GetHMDCameraComponent(const APawn* PlayerPawn)
{
	UCameraComponent* HMDCam = nullptr;
	if (PlayerPawn)
	{
		TArray<UCameraComponent*> CameraComponents;
		PlayerPawn->GetComponents(CameraComponents);

		for (UCameraComponent* Camera : CameraComponents)
		{
			if (Camera->bLockToHmd)
			{
				HMDCam = Camera;
				break;
			}
		}

		if (HMDCam == nullptr && CameraComponents.Num() > 0)
		{
			HMDCam = CameraComponents[0];
		}
	}
	return HMDCam;
}
