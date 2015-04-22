// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	World.cpp: UWorld implementation
=============================================================================*/

#include "UnrealEd.h"
#include "HierarchicalLOD.h"

#if WITH_EDITOR
#include "Editor/UnrealEd/Classes/Editor/EditorEngine.h"
#include "Editor/UnrealEd/Public/Editor.h"
#include "Engine/LODActor.h"
#include "Editor/UnrealEd/Classes/Factories/Factory.h"
#include "MeshUtilities.h"
#include "ObjectTools.h"

#endif // WITH_EDITOR
#include "GameFramework/WorldSettings.h"
#include "Components/InstancedStaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogLODGenerator, Warning, All);

#define LOCTEXT_NAMESPACE "HierarchicalLOD"

/*-----------------------------------------------------------------------------
	HierarchicalLOD implementation.
-----------------------------------------------------------------------------*/

////////////////////////////////////////////////////////////////////////////////////////////
// Hierarchical LOD System implementation 
////////////////////////////////////////////////////////////////////////////////////////////

// utility function to calculate overlap of two spheres
float CalculateOverlap(const FSphere& ASphere, const float AFillingFactor, const FSphere& BSphere, const float BFillingFactor)
{
	// if it doesn't intersect, return zero 
	if (!ASphere.Intersects(BSphere))
	{
		return 0.f;
	}

	if (ASphere.IsInside(BSphere, 1.f))
	{
		return ASphere.GetVolume();
	}

	if(BSphere.IsInside(ASphere, 1.f))
	{
		return BSphere.GetVolume();
	}

	if (ASphere.Equals(BSphere, 1.f))
	{
		return ASphere.GetVolume();
	}

	float Distance = (ASphere.Center-BSphere.Center).Size();
	check (Distance > 0.f);

	float  ARadius = ASphere.W;
	float  BRadius = BSphere.W;

	float  ACapHeight = (BRadius*BRadius - (ARadius - Distance)*(ARadius - Distance)) / (2*Distance);
	float  BCapHeight = (ARadius*ARadius - (BRadius - Distance)*(BRadius - Distance)) / (2*Distance);

	check (ACapHeight>0.f);
	check (BCapHeight>0.f);

	float  OverlapRadius1 = ((ARadius+BRadius)*(ARadius+BRadius) - Distance*Distance) * (Distance*Distance - (ARadius-BRadius)*(ARadius-BRadius));
	float  OverlapRadius2 = 2*Distance;
	float  OverlapRedius = FMath::Sqrt(OverlapRadius1) / OverlapRadius2;
	float  OverlapRediusSq = OverlapRedius*OverlapRedius;

	check (OverlapRadius1 >= 0.f);

	float ConstPI = PI/6.0f;
	float AVolume = ConstPI*(3*OverlapRediusSq + ACapHeight*ACapHeight) * ACapHeight;
	float BVolume = ConstPI*(3*OverlapRediusSq + BCapHeight*BCapHeight) * BCapHeight;

	check (AVolume > 0.f &&  BVolume > 0.f);

	float TotalVolume = AFillingFactor*AVolume + BFillingFactor*BVolume;
	return TotalVolume;
}

// utility functions that calculates filling factor
float CalculateFillingFactor(const FSphere& ASphere, const float AFillingFactor, const FSphere& BSphere, const float BFillingFactor)
{
	float OverlapVolume = CalculateOverlap( ASphere, AFillingFactor, BSphere, BFillingFactor);
	FSphere UnionSphere = ASphere + BSphere;
	// it shouldn't be zero or it should be checked outside
	ensure(UnionSphere.W != 0.f);

	// http://deim.urv.cat/~rivi/pub/3d/icra04b.pdf
	// cost is calculated based on r^3 / filling factor
	// since it subtract by AFillingFactor * 1/2 overlap volume + BfillingFactor * 1/2 overlap volume
	return FMath::Max(0.f, (AFillingFactor * ASphere.GetVolume() + BFillingFactor * BSphere.GetVolume() - OverlapVolume) / UnionSphere.GetVolume());
}

////////////////////////////////////////////////////////////////////////////////////////////
// Hierarchical LOD Builder System implementation 
////////////////////////////////////////////////////////////////////////////////////////////

FHierarchicalLODBuilder::FHierarchicalLODBuilder(class UWorld* InWorld)
:	World(InWorld)
{}

#if WITH_HOT_RELOAD_CTORS
FHierarchicalLODBuilder::FHierarchicalLODBuilder()
	: World(nullptr)
{
	EnsureRetrievingVTablePtr();
}
#endif // WITH_HOT_RELOAD_CTORS

// build hierarchical cluster
void FHierarchicalLODBuilder::Build()
{
	check (World);

	const TArray<class ULevel*>& Levels = World->GetLevels();

	for (const auto& LevelIter : Levels)
	{
		BuildClusters(LevelIter);
	}
}

// for now it only builds per level, it turns to one actor at the end
void FHierarchicalLODBuilder::BuildClusters(class ULevel* InLevel)
{
	// I'm using stack mem within this scope of the function
	// so we need this
	FMemMark Mark(FMemStack::Get());

	// you still have to delete all objects just in case they had it and didn't want it anymore
	TArray<UObject*> AssetsToDelete;
	for (int32 ActorId=InLevel->Actors.Num()-1; ActorId >= 0; --ActorId)
	{
		ALODActor* LodActor = Cast<ALODActor>(InLevel->Actors[ActorId]);
		if (LodActor)
		{
			for (auto& Asset: LodActor->SubObjects)
			{
				// @TOOD: This is not permanent fix
				if (Asset)
				{
					AssetsToDelete.Add(Asset);
				}
			}
			World->DestroyActor(LodActor);
		}
	}

	for (auto& Asset : AssetsToDelete)
	{
		ObjectTools::DeleteSingleObject(Asset, false);
	}

	// only build if it's enabled
	if(InLevel->GetWorld()->GetWorldSettings()->bEnableHierarchicalLODSystem)
	{
		const int32 TotalNumLOD = InLevel->GetWorld()->GetWorldSettings()->HierarchicalLODSetup.Num();

		for(int32 LODId=0; LODId<TotalNumLOD; ++LODId)
		{
			AWorldSettings* WorldSetting = InLevel->GetWorld()->GetWorldSettings();
			check(WorldSetting->HierarchicalLODSetup.IsValidIndex(LODId));
			float DesiredBoundBoxHalfSize = WorldSetting->HierarchicalLODSetup[LODId].DesiredBoundSize * 0.5f;
			float DesiredFillingRatio = WorldSetting->HierarchicalLODSetup[LODId].DesiredFillingPercentage * 0.01f;
			ensure(DesiredFillingRatio!=0.f);
			float HighestCost = FMath::Pow(DesiredBoundBoxHalfSize, 3)/(DesiredFillingRatio);
			// test parameter I was playing with to cull adding to the array
			// intialization can have too many elements, decided to cull
			// the problem can be that we can create disconnected tree
			// my assumption is that if the merge cost is too high, then it's not worth merge anyway
			static int32 CullMultiplier=1;

			// since to show progress of initialization, I'm scoping it
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("LODIndex"), FText::AsNumber(LODId));

				FScopedSlowTask SlowTask(100, FText::Format(LOCTEXT("HierarchicalLOD_InitializeCluster", "Initializing Clusters for LOD {LODIndex}..."), Arguments));
				SlowTask.MakeDialog();

				// initialize Clusters
				InitializeClusters(InLevel, LODId, HighestCost*CullMultiplier);

				// move a half way - I know we can do this better but as of now this is small progress
				SlowTask.EnterProgressFrame(50);

				// now we have all pair of nodes
				FindMST();
			}

			// now we have to calculate merge clusters and build actors
			MergeClustersAndBuildActors(InLevel, LODId, HighestCost);
		}
	}

	// Clear Clusters. It is using stack mem, so it won't be good after this
	Clusters.Empty();
}

void FHierarchicalLODBuilder::FindMST() 
{
	if (Clusters.Num() > 0)
	{
		// now sort edge in the order of weight
		struct FCompareCluster
		{
			FORCEINLINE bool operator()(const FLODCluster& A, const FLODCluster& B) const
			{
				return (A.GetCost() < B.GetCost());
			}
		};

		Clusters.Sort(FCompareCluster());
	}
}

void FHierarchicalLODBuilder::MergeClustersAndBuildActors(class ULevel* InLevel, const int32 LODIdx, float HighestCost)
{
	if (Clusters.Num() > 0)
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("LODIndex"), FText::AsNumber(LODIdx));

		// merge clusters first
		{
			static int32 TotalIteration=3;
			const int32 TotalCluster = Clusters.Num();

			FScopedSlowTask SlowTask(TotalIteration*TotalCluster, FText::Format( LOCTEXT("HierarchicalLOD_BuildClusters", "Building Clusters for LOD {LODIndex}..."), Arguments) );
			SlowTask.MakeDialog();

			for(int32 Iteration=0; Iteration<TotalIteration; ++Iteration)
			{
				// now we have minimum Clusters
				for(int32 ClusterId=0; ClusterId < TotalCluster; ++ClusterId)
				{
					auto& Cluster = Clusters[ClusterId];
					UE_LOG(LogLODGenerator, Verbose, TEXT("%d. %0.2f {%s}"), ClusterId+1, Cluster.GetCost(), *Cluster.ToString());

					// progress bar update
					SlowTask.EnterProgressFrame();

					if(Cluster.IsValid())
					{
						for(int32 MergedClusterId=0; MergedClusterId < ClusterId; ++MergedClusterId)
						{
							// compare with previous clusters
							auto& MergedCluster = Clusters[MergedClusterId];
							// see if it's valid, if it contains, check the cost
							if(MergedCluster.IsValid())
							{
								if(MergedCluster.Contains(Cluster))
								{
									// if valid, see if it contains any of this actors
									// merge whole clusters
									FLODCluster NewCluster = Cluster + MergedCluster;
									float MergeCost = NewCluster.GetCost();

									// merge two clusters
									if(MergeCost <= HighestCost)
									{
										UE_LOG(LogLODGenerator, Log, TEXT("Merging of Cluster (%d) and (%d) with merge cost (%0.2f) "), ClusterId+1, MergedClusterId+1, MergeCost);

										MergedCluster = NewCluster;
										// now this cluster is invalid
										Cluster.Invalidate();
										break;
									}
									else
									{
										Cluster -= MergedCluster;
									}
								}
							}
						}

						UE_LOG(LogLODGenerator, Verbose, TEXT("Processed(%s): %0.2f {%s}"), Cluster.IsValid()? TEXT("Valid"):TEXT("Invalid"), Cluster.GetCost(), *Cluster.ToString());
					}
				}
			}
		}

		static bool bBuildActor=true;

		if (bBuildActor)
		{
			// print data
			int32 TotalValidCluster=0;
			for(auto& Cluster: Clusters)
			{
				if(Cluster.IsValid())
				{
					++TotalValidCluster;
				}
			}

			FScopedSlowTask SlowTask(TotalValidCluster, FText::Format( LOCTEXT("HierarchicalLOD_MergeActors", "Merging Actors for LOD {LODIndex}..."), Arguments) );
			SlowTask.MakeDialog();

			for(auto& Cluster: Clusters)
			{
				if(Cluster.ShouldBuild())
				{
					SlowTask.EnterProgressFrame();
					//Sleep(0.001f);
					Cluster.BuildActor(InLevel->GetWorld(), InLevel, LODIdx);
				}
			}
		}
	}
}

bool ShouldGenerateCluster(class AActor* Actor)
{
	if (!Actor)
	{
		return false;
	}

	if (!Actor->bEnableAutoLODGeneration)
	{
		return false;
	}

	// make sure we don't generate for LODActor for Level 0
	if (Actor->IsA(ALODActor::StaticClass()))
	{
		return false;
	}

	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	if(Extent.SizeSquared() <= 0.1)
	{
		return false;
	}

	// for now only consider staticmesh - I don't think skel mesh would work with simplygon merge right now @fixme
	TArray<UStaticMeshComponent*> Components;
	Actor->GetComponents<UStaticMeshComponent>(Components);
	// TODO: support instanced static meshes
	Components.RemoveAll([](UStaticMeshComponent* Val){ return Val->IsA(UInstancedStaticMeshComponent::StaticClass()); });

	return (Components.Num() > 0);
}

void FHierarchicalLODBuilder::InitializeClusters(class ULevel* InLevel, const int32 LODIdx, float CullCost)
{
	if (InLevel->Actors.Num() > 0)
	{
		if (LODIdx == 0)
		{
			Clusters.Empty();

			// first we generate graph with 2 pair nodes
			// this is very expensive when we have so many actors
			// so we'll need to optimize later @todo
			for(int32 ActorId=0; ActorId<InLevel->Actors.Num(); ++ActorId)
			{
				AActor* Actor1 = InLevel->Actors[ActorId];
				// make sure Actor has bound
				if(ShouldGenerateCluster(Actor1))
				{
					for(int32 SubActorId=ActorId+1; SubActorId<InLevel->Actors.Num(); ++SubActorId)
					{
						AActor* Actor2 = InLevel->Actors[SubActorId];
						if(ShouldGenerateCluster(Actor2))
						{
							FLODCluster NewClusterCandidate = FLODCluster(Actor1, Actor2);
							// @todo optimization : filter first - this might create disconnected tree - 
							//@Todo debug but it's too slow to have full tree 
							if (NewClusterCandidate.GetCost() <= CullCost)
							{
								Clusters.Add(NewClusterCandidate);
							}
						}
					}
				}
			}
		}
		else // at this point we only care for LODActors
		{
			Clusters.Empty();
			TArray<ALODActor*> LODActors;
			for(int32 ActorId=0; ActorId<InLevel->Actors.Num(); ++ActorId)
			{
				ALODActor* Actor = Cast<ALODActor>(InLevel->Actors[ActorId]);
				if (Actor)
				{
					LODActors.Add(Actor);
				}
			}
			// first we generate graph with 2 pair nodes
			// this is very expensive when we have so many actors
			// so we'll need to optimize later @todo
			for(int32 ActorId=0; ActorId<LODActors.Num(); ++ActorId)
			{
				ALODActor* Actor1 = (LODActors[ActorId]);
				if (Actor1->LODLevel == LODIdx)
				{
					for(int32 SubActorId=ActorId+1; SubActorId<LODActors.Num(); ++SubActorId)
					{
						ALODActor* Actor2 = LODActors[SubActorId];

						if (Actor2->LODLevel == LODIdx)
						{
							// create new cluster
							FLODCluster NewClusterCandidate = FLODCluster(Actor1, Actor2);
							Clusters.Add(NewClusterCandidate);
						}
					}
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////
// LODCluster
////////////////////////////////////////////////////////////////////////////////////////////


FLODCluster::FLODCluster(const FLODCluster& Other)
: Actors(Other.Actors)
, Bound(Other.Bound)
, FillingFactor(Other.FillingFactor)
, bValid(Other.bValid)
{
}

FLODCluster::FLODCluster(class AActor* Actor1)
: Bound(ForceInit)
, bValid(true)
{
	AddActor(Actor1);
	// calculate new filling factor
	FillingFactor = 1.f;
}

FLODCluster::FLODCluster(class AActor* Actor1, class AActor* Actor2)
: Bound(ForceInit)
, bValid(true)
{
	FSphere Actor1Bound = AddActor(Actor1);
	FSphere Actor2Bound = AddActor(Actor2);

	// calculate new filling factor
	FillingFactor = CalculateFillingFactor(Actor1Bound, 1.f, Actor2Bound, 1.f);
}

FSphere FLODCluster::AddActor(class AActor* NewActor)
{
	ensure (Actors.Contains(NewActor) == false);
	Actors.Add(NewActor);
	FVector Origin, Extent;
	NewActor->GetActorBounds(false, Origin, Extent);
	
	FSphere NewBound = FSphere(Origin, Extent.Size());
	Bound += NewBound;

	return NewBound;
}

FLODCluster FLODCluster::operator+(const FLODCluster& Other) const
{
	FLODCluster UnionCluster(*this);
	UnionCluster.MergeClusters(Other);
	return UnionCluster;
}

FLODCluster FLODCluster::operator+=(const FLODCluster& Other)
{
	MergeClusters(Other);
	return *this;
}

FLODCluster FLODCluster::operator-(const FLODCluster& Other) const
{
	FLODCluster Cluster(*this);
	Cluster.SubtractCluster(Other);
	return *this;
}

FLODCluster FLODCluster::operator-=(const FLODCluster& Other)
{
	SubtractCluster(Other);
	return *this;
}

FLODCluster& FLODCluster::operator=(const FLODCluster& Other)
{
	this->bValid		= Other.bValid;
	this->Actors		= Other.Actors;
	this->Bound			= Other.Bound;
	this->FillingFactor = Other.FillingFactor;

	return *this;
}
// Merge two clusters
void FLODCluster::MergeClusters(const FLODCluster& Other)
{
	// please note that when merge, we merge two boxes from each cluster, not exactly all actors' bound
	// have to recalculate filling factor and bound based on cluster data
	FillingFactor = CalculateFillingFactor(Bound, FillingFactor, Other.Bound, Other.FillingFactor);
	Bound += Other.Bound;

	for (auto& Actor: Other.Actors)
	{
		Actors.AddUnique(Actor);
	}
}

void FLODCluster::SubtractCluster(const FLODCluster& Other)
{
	for(int32 ActorId=0; ActorId<Actors.Num(); ++ActorId)
	{
		if (Other.Actors.Contains(Actors[ActorId]))
		{
			Actors.RemoveAt(ActorId);
			--ActorId;
		}
	}

	TArray<AActor*> NewActors = Actors;
	Actors.Empty();
	// need to recalculate parameter
	if (NewActors.Num() == 0)
	{
		Invalidate();
	}
	else if (NewActors.Num() == 1)
	{
		Bound = FSphere(ForceInitToZero);
		AddActor(NewActors[0]);
		FillingFactor = 1.f;
	}
	else if (NewActors.Num() >= 2)
	{
		Bound = FSphere(ForceInit);

		FSphere Actor1Bound = AddActor(NewActors[0]);
		FSphere Actor2Bound = AddActor(NewActors[1]);

		// calculate new filling factor
		FillingFactor = CalculateFillingFactor(Actor1Bound, 1.f, Actor2Bound, 1.f);

		// if more actors, we add them manually
		for (int32 ActorId=2; ActorId<NewActors.Num(); ++ActorId)
		{
			// if not contained, it shouldn't be
			check (!Actors.Contains(NewActors[ActorId]));

			FSphere NewBound = AddActor(NewActors[ActorId]);
			FillingFactor = CalculateFillingFactor(NewBound, 1.f, Bound, FillingFactor);
			Bound += NewBound;
		}
	}
}

// if criteria matches, build new LODActor and replace current Actors with that. We don't need 
// this clears previous actors and sets to this new actor
// this is required when new LOD is created from these actors, this will be replaced
// to save memory and to reduce memory increase during this process, we discard previous actors and replace with this actor
void FLODCluster::BuildActor(class UWorld* InWorld, class ULevel* InLevel, const int32 LODIdx)
{
	// do big size
	if (InWorld && InLevel)
	{
		////////////////////////////////////////////////////////////////////////////////////
		// create asset using Actors
		const FHierarchicalSimplification& LODSetup = InWorld->GetWorldSettings()->HierarchicalLODSetup[LODIdx];
		const FMeshProxySettings& ProxySetting = LODSetup.Setting;
		// Where generated assets will be stored
		UPackage* AssetsOuter = InLevel->GetOutermost(); // this asset is going to save with map, this means, I'll have to delete with it
		if (AssetsOuter)
		{
			TArray<UStaticMeshComponent*> AllComponents;

			for (auto& Actor: Actors)
			{
				TArray<UStaticMeshComponent*> Components;
				Actor->GetComponents<UStaticMeshComponent>(Components);
				// TODO: support instanced static meshes
				Components.RemoveAll([](UStaticMeshComponent* Val){ return Val->IsA(UInstancedStaticMeshComponent::StaticClass()); });
				AllComponents.Append(Components);
			}

			// it shouldn't even have come here if it didn't have any staticmesh
			if (ensure(AllComponents.Num() > 0))
			{
				// In case we don't have outer generated assets should have same path as LOD level
				const FString AssetsPath = FPackageName::GetLongPackagePath(AssetsOuter->GetName()) + TEXT("/");
				class AActor* FirstActor = Actors[0];

				TArray<UObject*> OutAssets;
				FVector OutProxyLocation = FVector::ZeroVector;
				// Generate proxy mesh and proxy material assets
				IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
				// should give unique name, so use level + actor name
				const FString PackageName = FString::Printf(TEXT("LOD_%s"), *FirstActor->GetName());
				if (MeshUtilities.GetMeshMergingInterface() && LODSetup.bSimplifyMesh)
				{
					MeshUtilities.CreateProxyMesh(Actors, ProxySetting, AssetsOuter, PackageName, OutAssets, OutProxyLocation);
				}
				else
				{
					FMeshMergingSettings MergeSetting;
					MergeSetting.bMergeMaterials = true;
					MeshUtilities.MergeActors(Actors, MergeSetting, AssetsPath + PackageName, OutAssets, OutProxyLocation, true );
				}

				UStaticMesh* MainMesh=NULL;
				// set staticmesh
				for(auto& Asset: OutAssets)
				{
					class UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset);

					if(StaticMesh)
					{
						MainMesh = StaticMesh;
					}
				}

				if (MainMesh)
				{
					////////////////////////////////////////////////////////////////////////////////////
					// create LODActors using the current Actors
					class ALODActor* NewActor = Cast<ALODActor>(InWorld->SpawnActor(ALODActor::StaticClass(), &OutProxyLocation, &FRotator::ZeroRotator));

					NewActor->SubObjects = OutAssets;
					NewActor->SubActors = Actors;
					NewActor->LODLevel = LODIdx+1; 
					float DrawDistance = LODSetup.DrawDistance;

					NewActor->GetStaticMeshComponent()->StaticMesh = MainMesh;

					// now set as parent
					for(auto& Actor: Actors)
					{
						Actor->SetLODParent(NewActor->GetStaticMeshComponent(), DrawDistance);
					}
				}
			}
		}
	}
}

bool FLODCluster::Contains(FLODCluster& Other) const
{
	if (IsValid() && Other.IsValid())
	{
		for(auto& Actor: Other.Actors)
		{
			if(Actors.Contains(Actor))
			{
				return true;
			}
		}
	}

	return false;
}

FString FLODCluster::ToString() const
{
	FString ActorList;
	for (auto& Actor: Actors)
	{
		ActorList += Actor->GetActorLabel();
		ActorList += ", ";
	}

	return FString::Printf(TEXT("ActorNum(%d), Actor List (%s)"), Actors.Num(), *ActorList);
}

#undef LOCTEXT_NAMESPACE 
