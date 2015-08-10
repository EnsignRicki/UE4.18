// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SCompoundWidget.h"
#include "STreeView.h"
#include "SListView.h"
#include "STableViewBase.h"
#include "TreeItemID.h"
#include "SlateBasics.h"

class AHLODSelectionActor;
class UWorld;
class UDrawSphereComponent;
class IDetailsView;
class ALODActor;
class AActor;

namespace HLODOutliner
{
	struct ITreeItem;
	typedef TSharedPtr<ITreeItem> FTreeItemPtr;
	typedef TSharedRef<ITreeItem> FTreeItemRef;

	/**
	* Outliner action class used for making changing to the Outliner's treeview 
	*/	
	struct FOutlinerAction
	{
		enum ActionType
		{
			AddItem,
			RemoveItem,
			MoveItem
		};

		FOutlinerAction(ActionType InType, FTreeItemPtr InItem) : Type(InType), Item(InItem) {};
		FOutlinerAction(ActionType InType, FTreeItemPtr InItem, FTreeItemPtr InParentItem) : Type(InType), Item(InItem), ParentItem( InParentItem) {};

		ActionType Type;
		FTreeItemPtr Item;
		FTreeItemPtr ParentItem;
	};

	/**
	* Implements the profiler window.
	*/
	class SHLODOutliner : public SCompoundWidget, public FNotifyHook, public FEditorUndoClient
	{
	typedef STreeView<FTreeItemPtr> SHLODTree;
	friend struct FLODActorItem;
	friend struct FHLODTreeWidgetItem;
	friend struct FStaticMeshActorItem;
	friend class SHLODWidgetItem;

	public:
		/** Default constructor. */
		SHLODOutliner();

		/** Virtual destructor. */
		virtual ~SHLODOutliner();

		SLATE_BEGIN_ARGS(SHLODOutliner){}
		SLATE_END_ARGS()

		/**
		* Constructs this widget.
		*/
		void Construct(const FArguments& InArgs);
		/** Creates the panel's ButtonWidget rows */
		TSharedRef<SWidget> CreateButtonWidgets();

		/** Creates the panel's Tree view widget*/
		TSharedRef<SWidget> CreateTreeviewWidget();

		/** Initializes and creates the settings view */
		void CreateSettingsView();

		// Begin SCompoundWidget Interface
		virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
		virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
		virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
		virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
		virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
		virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)  override;
		// End of SCompoundWidget Interface

		// Begin FEditorUndoClient Interface
		virtual void PostUndo(bool bSuccess) override;
		virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
		// End of FEditorUndoClient	

		/** Button handlers */
		FReply HandleBuildHLODs();
		FReply HandleDeleteHLODs();
		FReply HandlePreviewHLODs();
		FReply HandleDeletePreviewHLODs();
		FReply RetrieveActors();
		FReply HandleBuildLODActors();
		FReply HandleForceRefresh();
		/** End button handlers */

	private:
		/** Registers all the callback delegates required for keeping the treeview sync */
		void RegisterDelegates();

		/** De-registers all the callback delegates required for keeping the treeview sync */
		void DeregisterDelegates();

	protected:
		/**
		* Forces viewing the mesh of a Cluster (LODActor)
		*
		* @param Item - Treeview Item representation for LODActor
		*/
		void ForceViewLODActor(TSharedRef<ITreeItem> Item);

		/**
		* UI callback for the state of Force HLOD View checkbox
		*
		* @param Item - Treeview Item representation for Hierarchical LOD level
		* @return ECheckBoxState
		*/
		ECheckBoxState IsHLODLevelChecked(TSharedRef<ITreeItem> Item) const;

		/**
		* UI callback for changes in Force HLOD view checkbox
		*
		* @param NewState - State of the check Box
		* @param Item - Treeview Item representation for Hierarchical LOD level
		*/
		void HandleCheckBoxCheckedStateChanged(ECheckBoxState NewState, TSharedRef<ITreeItem> Item);

		/**
		* Returns whether or not a HLOD level can be forced, this depends on whether or not all of the Clusters with LODLevel are non-dirty (have their meshes build)
		*
		* @param LODLevel - LOD level to check for
		* @return bool
		*/
		bool CanHLODLevelBeForced(const uint32 LODLevel) const;

		/**
		* Restores the forced viewing state for the given LOD levle
		*
		* @param LODLevel - LOD level to force
		*/
		void RestoreForcedLODLevel(const uint32 LODLevel);

		/**
		* Forces LODActors within the given LODLevel to show their meshes (other levels hide theirs)
		*
		* @param LODLevel - LOD level to force
		*/
		void SetForcedLODLevel(const uint32 LODLevel);

	protected:
		/**
		* Builds the HLOD mesh for the given ALODActor (cluster)
		*
		* @param Item - (LODActor)TreeItem containing the ALODActor ptr
		*/
		void BuildLODActor(TSharedRef<ITreeItem> Item);	

		/**
		* Select the LODActor in the Editor Viewport
		*
		* @param Item - (LODActor)TreeItem containing the ALODActor ptr
		*/
		void SelectLODActor(TSharedRef<ITreeItem> Item);

		/**
		* Deletes a cluster (LODActor)
		*
		* @param Item - Treeview Item representation for LODActor
		*/
		void DeleteCluster(TSharedRef<ITreeItem> Item);

		/**
		* Selects the contained actors (SubActors) for a specific LODActor
		*
		* @param Item - Treeview Item representation for LODActor
		*/
		void SelectContainedActors(TSharedRef<ITreeItem> Item);

		/**
		* Removes the given StaticMeshActor from its parent's (ALODActor) sub-actors array
		*
		* @param Item - (StaticMeshActor)TreeItem containing the Actor ptr
		*/
		void RemoveStaticMeshActorFromCluster(TSharedRef<ITreeItem> Item);

		/**
		* Removes the given LODActor from its parent's (ALODActor) sub-actors array
		*
		* @param Item - Treeview Item representation for LODActor
		*/
		void RemoveLODActorFromCluster(TSharedRef<ITreeItem> Item);
		
		/**
		* Destroys an LODActor instance
		*
		* @param InActor - ALODActor to destroy
		*/
		void DestroyLODActor(ALODActor* InActor);

		/**
		* Updates the DrawDistance value for all the LODActors with the given LODLevelIndex
		*
		* @param LODLevelIndex -
		*/
		void UpdateDrawDistancesForLODLevel(const uint32 LODLevelIndex);

	protected:
		/** Tree view callbacks */

		/**
		* Generates a Tree view row for the given Tree view Node
		*
		* @param InReflectorNode - Node to generate a row for
		* @param OwnerTable - Owning table of the InReflectorNode
		* @return TSharedRef<ITableRow>
		*/
		TSharedRef<ITableRow> OnOutlinerGenerateRow(FTreeItemPtr InReflectorNode, const TSharedRef<STableViewBase>& OwnerTable);
	
		/**
		* Treeview callback for retrieving the children of a specific TreeItem
		*
		* @param InParent - Parent item to return the children from
		* @param OutChildren - InOut array for the children
		*/
		void OnOutlinerGetChildren(FTreeItemPtr InParent, TArray<FTreeItemPtr>& OutChildren);

		/**
		* Handles the event fired when selection with the HLOD Tree view changes
		*
		* @param TreeItem - Selected node(s)
		* @param SelectionInfo - Type of selection change
		*/
		void OnOutlinerSelectionChanged(FTreeItemPtr TreeItem, ESelectInfo::Type /*SelectionInfo*/);
	
		/**
		* Handles double click events from the HLOD Tree view
		*
		* @param TreeItem - Node which was double-clicked
		*/
		void OnOutlinerDoubleClick(FTreeItemPtr TreeItem);

		/** Open a context menu for this scene outliner */
		TSharedPtr<SWidget> OnOpenContextMenu();

		/**
		* Handles item expansion events from the HLOD tree view
		*
		* @param TreeItem - Item which expansion state was changed
		* @param bIsExpanded - New expansion state
		*/
		void OnItemExpansionChanged(FTreeItemPtr TreeItem, bool bIsExpanded);
		
		/** End of Tree view callbacks */

	private:
		/** Starts the Editor selection batch */
		void StartSelection();

		/** Empties the current Editor selection */
		void EmptySelection();

		/** Destroys the created selection actors */
		void DestroySelectionActors();		

		/**
		* Selects an Actor in the Editor viewport
		*
		* @param Actor - AActor to select inside the viewport
		* @param SelectionDepth - (recursive)
		*/
		void SelectActorInViewport(AActor* Actor, const uint32 SelectionDepth = 0);

		/**
		* Selects actors and sub-actors for the given LODActor
		*
		* @param LODActor - Actor to select + subactors
		* @param SelectionDepth - (recursive)
		*/
		void SelectLODActorAndContainedActorsInViewport(ALODActor* LODActor, const uint32 SelectionDepth = 0);

		/**
		* Creates a ASelectionActor for the given actor, "procedurally" drawing its bounds
		*
		* @param Actor - Actor to create the SelectionActor for
		* @return UDrawSphereComponent*
		*/
		UDrawSphereComponent* CreateBoundingSphereForActor(AActor* Actor);

		/**
		* Extracts all the Static Mesh Actors from the given LODActor's SubActors array
		*
		* @param LODActor - LODActors to check the SubActors array for
		* @param InOutActors - Array to fill with Static Mesh Actors
		*/
		void ExtractStaticMeshActorsFromLODActor(ALODActor* LODActor, TArray<AActor*> &InOutActors);
	
		/** Ends the Editor selection batch */
		void EndSelection();

	protected:
		/** Broadcast event delegates */

		/** Called by USelection::SelectionChangedEvent delegate when the level's selection changes */
		void OnLevelSelectionChanged(UObject* Obj);

		/** Called by the engine when a level is added to the world. */
		void OnLevelAdded(ULevel* InLevel, UWorld* InWorld);

		/** Called by the engine when a level is removed from the world. */
		void OnLevelRemoved(ULevel* InLevel, UWorld* InWorld);

		/** Called by the engine when an actor is added to the world. */
		void OnLevelActorsAdded(AActor* InActor);

		/** Called by the engine when an actor is remove from the world. */
		void OnLevelActorsRemoved(AActor* InActor);

		/** Handler for when a property changes on any object */
		void OnActorLabelChanged(AActor* ChangedActor);

		/** Called when the map has changed*/
		void OnMapChange(uint32 MapFlags);

		/** Called when the current level has changed */
		void OnNewCurrentLevel();

		/** Called when a HLODActor is moved between clusters */
		void OnHLODActorMovedEvent(const AActor* InActor, const AActor* ParentActor);

		/** Called when an Actor is moved inside of the level */
		void OnActorMovedEvent(AActor* InActor);

		/** Called when and HLODActor is added to the level */
		void OnHLODActorAddedEvent(const AActor* InActor, const AActor* ParentActor);

		/** Called when and HLODActor is added to the level */
		void OnHLODActorMarkedDirtyEvent(ALODActor* InActor);	

		/** Called when a DrawDistance value within WorldSettings changed */
		void OnHLODDrawDistanceChangedEvent();

		/** End of Broadcast event delegates */

	private:
		/** Tells the scene outliner that it should do a full refresh, which will clear the entire tree and rebuild it from scratch. */
		void FullRefresh();

		/** Populates the HLODTreeRoot array and consequently the Treeview */
		void Populate();
	
		/** Structure containing information relating to the expansion state of parent items in the tree */
		typedef TMap<FTreeItemID, bool> FParentsExpansionState;

		/** Gets the current expansion state of parent items */
		TMap<FTreeItemID, bool> GetParentsExpansionState() const;

		/** Updates the expansion state of parent items after a repopulate, according to the previous state */
		void SetParentsExpansionState(const FParentsExpansionState& ExpansionStateInfo) const;

		/**
		* Adds a new Treeview item
		*
		* @param InItem - Item to add
		* @param InParentItem - Optional parent item to add it to
		* @return const bool
		*/
		const bool AddItemToTree(FTreeItemPtr InItem, FTreeItemPtr InParentItem);

		/**
		* Moves a TreeView item around
		*
		* @param InItem - Item to move
		* @param InParentItem - New parent for InItem to move to
		*/
		void MoveItemInTree(FTreeItemPtr InItem, FTreeItemPtr InParentItem);

		/**
		* Removes a TreeView item
		*
		* @param InItem - Item to remove
		*/
		void RemoveItemFromTree(FTreeItemPtr InItem);

		/**
		* Selects a Treeview item
		*
		* @param InItem - Item to select
		*/
		void SelectItemInTree(FTreeItemPtr InItem);
	private:		
		/** Whether or not we need to do a refresh of the Tree view*/
		bool bNeedsRefresh;

		/** World instance we are currently representing/mirroring in the panel */
		UWorld* CurrentWorld;

		/** Tree view nodes */
		TArray<FTreeItemPtr> HLODTreeRoot;
		/** Currently selected Tree view nodes*/
		TArray<FTreeItemPtr> SelectedNodes;
		/** HLOD Treeview widget*/
		TSharedPtr<SHLODTree> TreeView;
		/** Property viewing widget */
		TSharedPtr<IDetailsView> SettingsView;
	
		/** Map containing all the nodes with their corresponding keys */
		TMap<FTreeItemID, FTreeItemPtr> TreeItemsMap;

		/** Array of pending OutlinerActions */
		TArray<FOutlinerAction> PendingActions;

		/** Array containing all the nodes */
		TArray<FTreeItemPtr> AllNodes;
	
		/** Array of SelectionActors created for current HLOD selection*/
		TArray<AHLODSelectionActor*> SelectionActors;

		/** Currently forced LOD level*/
		int32 ForcedLODLevel;
		/** Array with flags for each LOD level (whether or not all their Clusters/LODActors have their meshes built) */
		TArray<bool> LODLevelBuildFlags;
		/** Array of LODActors/Cluster per LOD level*/
		TArray<TArray<ALODActor*>> LODLevelActors;
		/** Array of DrawDistances for each LOD Level*/
		TArray<float> LODLevelDrawDistances;
	};
};
