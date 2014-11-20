// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.


#include "PersonaPrivatePCH.h"

#include "SAnimTrackCurvePanel.h"
#include "ScopedTransaction.h"
#include "SAnimCurveEd.h"
#include "Editor/KismetWidgets/Public/SScrubWidget.h"
#include "AssetRegistryModule.h"
#include "Kismet2NameValidators.h"
#include "SExpandableArea.h"
#include "STextEntryPopup.h"

#define LOCTEXT_NAMESPACE "AnimTrackCurvePanel"

//////////////////////////////////////////////////////////////////////////
//  FAnimTrackCurveInterface interface 

namespace ETransformCurve
{
	enum Type
	{
		Translation = 0,
		Rotation,
		Scale,
		Max
	};
}

/** Interface you implement if you want the CurveEditors to be able to edit curves on you */
class FAnimTrackCurveBaseInterface : public FCurveOwnerInterface
{
public:
	TWeakObjectPtr<UAnimSequence>	BaseSequence;
	FAnimCurveBase*	CurveData;
	ETransformCurve::Type	CurveType;

public:
	FAnimTrackCurveBaseInterface(UAnimSequence * BaseSeq, FAnimCurveBase*	InData, ETransformCurve::Type InCurveType)
		: BaseSequence(BaseSeq)
		, CurveData(InData)
		, CurveType(InCurveType)
	{
		// they should be valid
		check (BaseSequence.IsValid());
		check (CurveData);
	}

	FName GetDisplayCurveName(int32 Index) const
	{
		// rotation curve
		if(CurveType == ETransformCurve::Rotation )
		{
			switch(Index)
			{
			case 1:
			return FName(TEXT("Pitch"));
			case 2:
			return FName(TEXT("Yaw"));
			default:
			return FName(TEXT("Roll"));
			}
		}
		else
		{
			switch(Index)
			{
			case 1:
			return FName(TEXT("Y"));
			case 2:
			return FName(TEXT("Z"));
			default:
			return FName(TEXT("X"));
			}
		}
	}

	/** Returns set of curves to edit. Must not release the curves while being edited. */
	virtual TArray<FRichCurveEditInfoConst> GetCurves() const
	{
		TArray<FRichCurveEditInfoConst> Curves;
		FVectorCurve * VectorCurveData = (FVectorCurve*)(CurveData);
		Curves.Add(FRichCurveEditInfoConst(&VectorCurveData->FloatCurves[0], GetDisplayCurveName(0)));
		Curves.Add(FRichCurveEditInfoConst(&VectorCurveData->FloatCurves[1], GetDisplayCurveName(1)));
		Curves.Add(FRichCurveEditInfoConst(&VectorCurveData->FloatCurves[2], GetDisplayCurveName(2)));
		return Curves;
	}
	/** Returns set of curves to query. Must not release the curves while being edited. */
	virtual TArray<FRichCurveEditInfo> GetCurves()
	{
		TArray<FRichCurveEditInfo> Curves;
		FVectorCurve * VectorCurveData = (FVectorCurve*)(CurveData);
		Curves.Add(FRichCurveEditInfo(&VectorCurveData->FloatCurves[0], GetDisplayCurveName(0)));
		Curves.Add(FRichCurveEditInfo(&VectorCurveData->FloatCurves[1], GetDisplayCurveName(1)));
		Curves.Add(FRichCurveEditInfo(&VectorCurveData->FloatCurves[2], GetDisplayCurveName(2)));
		return Curves;
	}

	virtual UObject* GetOwner()
	{
		if (BaseSequence.IsValid())
		{
			return BaseSequence.Get();
		}

		return NULL;
	}

	/** Called to modify the owner of the curve */
	virtual void ModifyOwner()
	{
		if (BaseSequence.IsValid())
		{
			// need to rebake
			BaseSequence.Get()->bNeedsRebake = true;
			BaseSequence.Get()->Modify(true);
		}
	}

	/** Called to make curve owner transactional */
	virtual void MakeTransactional()
	{
		if (BaseSequence.IsValid())
		{
			BaseSequence.Get()->SetFlags(BaseSequence.Get()->GetFlags() | RF_Transactional);
		}
	}

	/** Called to get the name of a curve back from the animation skeleton */
	virtual FText GetCurveName(USkeleton::AnimCurveUID Uid) const
	{
		if(BaseSequence.IsValid())
		{
			FSmartNameMapping* NameMapping = BaseSequence.Get()->GetSkeleton()->SmartNames.GetContainer(USkeleton::AnimTrackCurveMappingName);
			if(NameMapping)
			{
				FName CurveName;
				if(NameMapping->GetName(Uid, CurveName))
				{
					FName DisplayName = CurveName;

					return FText::FromString(FString::Printf(TEXT("%s(%c)"), *DisplayName.ToString(), GetCurveTypeCharacter()));
				}
			}
		}

		return FText::GetEmpty();
	}

	TCHAR GetCurveTypeCharacter() const
	{
		switch (CurveType)
		{
		case ETransformCurve::Translation:
			return TCHAR('T');
		case ETransformCurve::Rotation:
			return TCHAR('R');
		}

		return TCHAR('S');
	}

	virtual void OnCurveChanged() override
	{
	}
};

//////////////////////////////////////////////////////////////////////////
//  SCurveEd Track : each track for curve editing 

/** Widget for editing a single track of animation curve - this includes CurveEditors */
class STransformCurveEdTrack : public SCompoundWidget
{
private:
	/** Pointer to notify panel for drawing*/
	TSharedPtr<class SCurveEditor>			CurveEditors[ETransformCurve::Max];

	/** Name of curve it's editing - CurveName should be unique within this tracks**/
	FAnimTrackCurveBaseInterface	*		CurveInterfaces[ETransformCurve::Max];

	/** Curve Panel Ptr **/
	TWeakPtr<SAnimTrackCurvePanel>			PanelPtr;

private:
	USkeleton::AnimCurveUID	CurveUid;

public:
	SLATE_BEGIN_ARGS( STransformCurveEdTrack )
		: _AnimTrackCurvePanel()
		, _Sequence()
		, _CurveUid()
		, _WidgetWidth()
		, _ViewInputMin()
		, _ViewInputMax()
		, _OnSetInputViewRange()
		, _OnGetScrubValue()
	{}
	SLATE_ARGUMENT( TSharedPtr<SAnimTrackCurvePanel>, AnimTrackCurvePanel)
	// editing related variables
	SLATE_ARGUMENT( class UAnimSequence*, Sequence )
	SLATE_ARGUMENT( USkeleton::AnimCurveUID, CurveUid )
	// widget viewing related variables
	SLATE_ARGUMENT( float, WidgetWidth ) // @todo do I need this?
	SLATE_ATTRIBUTE( float, ViewInputMin )
	SLATE_ATTRIBUTE( float, ViewInputMax )
	SLATE_EVENT( FOnSetInputViewRange, OnSetInputViewRange )
	SLATE_EVENT( FOnGetScrubValue, OnGetScrubValue )
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~STransformCurveEdTrack();


	/**
	 * Build and display curve track context menu.
	 *
	 */
	FReply OnContextMenu();

	// expand editor mode 
	ESlateCheckBoxState::Type IsEditorExpanded() const;
	void ToggleExpandEditor(ESlateCheckBoxState::Type NewType);
	const FSlateBrush* GetExpandContent() const;
	FVector2D GetDesiredSize() const;

	// Bound to attribute for curve name, uses curve interface to request from skeleton
	FText GetCurveName(USkeleton::AnimCurveUID Uid, ETransformCurve::Type Type) const;
};

//////////////////////////////////////////////////////////////////////////
// STransformCurveEdTrack

void STransformCurveEdTrack::Construct(const FArguments& InArgs)
{
	TSharedRef<SAnimTrackCurvePanel> PanelRef = InArgs._AnimTrackCurvePanel.ToSharedRef();
	PanelPtr = InArgs._AnimTrackCurvePanel;
	
	// now create CurveInterfaces, 
	// find which curve this belongs to
	UAnimSequence * Sequence = InArgs._Sequence;
	check (Sequence);

	// get the curve data
	FTransformCurve* Curve = static_cast<FTransformCurve*> (Sequence->RawCurveData.GetCurveData(InArgs._CurveUid, FRawCurveTracks::TransformType));
	check (Curve);

	CurveUid = InArgs._CurveUid;

	CurveInterfaces[ETransformCurve::Translation] = new FAnimTrackCurveBaseInterface(Sequence, &Curve->TranslationCurve, ETransformCurve::Translation);
	CurveInterfaces[ETransformCurve::Rotation] = new FAnimTrackCurveBaseInterface(Sequence, &Curve->RotationCurve, ETransformCurve::Rotation);
	CurveInterfaces[ETransformCurve::Scale] = new FAnimTrackCurveBaseInterface(Sequence, &Curve->ScaleCurve, ETransformCurve::Scale);
	int32 NumberOfKeys = Sequence->GetNumberOfFrames();
	//////////////////////////////
	
	this->ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(2.0f, 2.0f))
		[
			SNew(SVerticalBox)

			+SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.FillWidth(1)
				[
					// Notification editor panel
					SAssignNew(CurveEditors[ETransformCurve::Translation], SAnimCurveEd)
					.ViewMinInput(InArgs._ViewInputMin)
					.ViewMaxInput(InArgs._ViewInputMax)
					.DataMinInput(0.f)
					.DataMaxInput(Sequence->SequenceLength)
					// @fixme fix this to delegate
					.TimelineLength(Sequence->SequenceLength)
					.NumberOfKeys(NumberOfKeys)
					.DesiredSize(this, &STransformCurveEdTrack::GetDesiredSize)
					.OnSetInputViewRange(InArgs._OnSetInputViewRange)
					.OnGetScrubValue(InArgs._OnGetScrubValue)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SBox)
					.WidthOverride(InArgs._WidgetWidth)
					[
						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.FillWidth(1)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						.Padding(5, 0, 0, 0)
						[
							// Name of track
							SNew(SEditableText)
							.MinDesiredWidth(64.0f)
							.IsEnabled(true)
							.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
							.SelectAllTextWhenFocused(true)
							.IsReadOnly(true)
							.Text(this, &STransformCurveEdTrack::GetCurveName, Curve->CurveUid, ETransformCurve::Translation)
						]

						+SHorizontalBox::Slot()
						.Padding(FMargin(0.0f, 5.0f, 0.0f, 5.0f))
						.AutoWidth()
						.VAlign(VAlign_Top)
						[
							SNew(SButton)
							.ToolTipText(LOCTEXT("DisplayTrackOptionsMenuTooltip", "Display track options menu"))
							.OnClicked(this, &STransformCurveEdTrack::OnContextMenu)
							.Content()
							[
								SNew(SImage)
								.Image(FEditorStyle::GetBrush("ComboButton.Arrow"))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]
				]
			]

			+SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.FillWidth(1)
				[
					// Notification editor panel
					SAssignNew(CurveEditors[ETransformCurve::Rotation], SAnimCurveEd)
					.ViewMinInput(InArgs._ViewInputMin)
					.ViewMaxInput(InArgs._ViewInputMax)
					.DataMinInput(0.f)
					.DataMaxInput(Sequence->SequenceLength)
					// @fixme fix this to delegate
					.TimelineLength(Sequence->SequenceLength)
					.NumberOfKeys(NumberOfKeys)
					.DesiredSize(this, &STransformCurveEdTrack::GetDesiredSize)
					.OnSetInputViewRange(InArgs._OnSetInputViewRange)
					.OnGetScrubValue(InArgs._OnGetScrubValue)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SBox)
					.WidthOverride(InArgs._WidgetWidth)
					[

						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.FillWidth(1)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						.Padding(5, 0, 0, 0)
						[
							// Name of track
							SNew(SEditableText)
							.MinDesiredWidth(64.0f)
							.IsEnabled(true)
							.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
							.SelectAllTextWhenFocused(true)
							.IsReadOnly(true)
							.Text(this, &STransformCurveEdTrack::GetCurveName, Curve->CurveUid, ETransformCurve::Rotation)
						]
					]
				]
			]

			+SVerticalBox::Slot()
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.FillWidth(1)
				[
					// Notification editor panel
					SAssignNew(CurveEditors[ETransformCurve::Scale], SAnimCurveEd)
					.ViewMinInput(InArgs._ViewInputMin)
					.ViewMaxInput(InArgs._ViewInputMax)
					.DataMinInput(0.f)
					.DataMaxInput(Sequence->SequenceLength)
					// @fixme fix this to delegate
					.TimelineLength(Sequence->SequenceLength)
					.NumberOfKeys(NumberOfKeys)
					.DesiredSize(this, &STransformCurveEdTrack::GetDesiredSize)
					.OnSetInputViewRange(InArgs._OnSetInputViewRange)
					.OnGetScrubValue(InArgs._OnGetScrubValue)
				]

				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.f)
				[
					SNew(SBox)
					.WidthOverride(InArgs._WidgetWidth)
					[
						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.FillWidth(1)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						.Padding(5, 0, 0, 0)
						[
							// Name of track
							SNew(SEditableText)
							.MinDesiredWidth(64.0f)
							.IsEnabled(true)
							.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
							.SelectAllTextWhenFocused(true)
							.IsReadOnly(true)
							.Text(this, &STransformCurveEdTrack::GetCurveName, Curve->CurveUid, ETransformCurve::Scale)
						]
					]
				]
			]


		]
	];
	
	//Inform track widget about the curve and whether it is editable or not.
	CurveEditors[ETransformCurve::Translation]->SetCurveOwner(CurveInterfaces[ETransformCurve::Translation], true);
	CurveEditors[ETransformCurve::Rotation]->SetCurveOwner(CurveInterfaces[ETransformCurve::Rotation], true);
	CurveEditors[ETransformCurve::Scale]->SetCurveOwner(CurveInterfaces[ETransformCurve::Scale], true);
}

/** return a widget */
const FSlateBrush* STransformCurveEdTrack::GetExpandContent() const
{
	return FEditorStyle::GetBrush("Kismet.VariableList.HideForInstance");
}

STransformCurveEdTrack::~STransformCurveEdTrack()
{
	delete CurveInterfaces[ETransformCurve::Translation];
	delete CurveInterfaces[ETransformCurve::Rotation];
	delete CurveInterfaces[ETransformCurve::Scale];
}

FReply STransformCurveEdTrack::OnContextMenu()
{
	TSharedPtr<SAnimTrackCurvePanel> PanelShared = PanelPtr.Pin();

	if(PanelShared.IsValid())
	{
		FSlateApplication::Get().PushMenu(SharedThis(this),
										  PanelShared->CreateCurveContextMenu(CurveUid),
										  FSlateApplication::Get().GetCursorPos(),
										  FPopupTransitionEffect(FPopupTransitionEffect::TypeInPopup));
	}

	return FReply::Handled();
}

FVector2D STransformCurveEdTrack::GetDesiredSize() const
{
	return FVector2D(128.f, 128.f);
}

FText STransformCurveEdTrack::GetCurveName(USkeleton::AnimCurveUID Uid, ETransformCurve::Type Type) const
{
	return CurveInterfaces[Type]->GetCurveName(Uid);
}

//////////////////////////////////////////////////////////////////////////
// SAnimTrackCurvePanel

void SAnimTrackCurvePanel::Construct(const FArguments& InArgs)
{
	SAnimTrackPanel::Construct( SAnimTrackPanel::FArguments()
		.WidgetWidth(InArgs._WidgetWidth)
		.ViewInputMin(InArgs._ViewInputMin)
		.ViewInputMax(InArgs._ViewInputMax)
		.InputMin(InArgs._InputMin)
		.InputMax(InArgs._InputMax)
		.OnSetInputViewRange(InArgs._OnSetInputViewRange));

	WeakPersona = InArgs._Persona;
	Sequence = InArgs._Sequence;
	WidgetWidth = InArgs._WidgetWidth;
	OnGetScrubValue = InArgs._OnGetScrubValue;

	this->ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1)
		[
			SNew( SExpandableArea )
			.AreaTitle( LOCTEXT("TransformCurve_Title", "Tracks") )
			.BodyContent()
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(5)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AdditiveLayerTrackList_Title", "Additive Layer Tracks"))
						.Font( FEditorStyle::GetFontStyle( "PropertyWindow.BoldFont" ) )
						.ShadowOffset( FVector2D(1.0f, 1.0f) )
					]

					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SComboButton)
						.ContentPadding(FMargin(2.0f))
						.OnGetMenuContent(this, &SAnimTrackCurvePanel::GenerateCurveList)
					]
				]

				+SVerticalBox::Slot()
				.Padding(FMargin(0.0f, 5.0f, 0.0f, 0.0f))
				.AutoHeight()
				[
					SAssignNew(PanelSlot, SSplitter)
					.Orientation(Orient_Vertical)
				]
			]
		]
	];

	UpdatePanel();
}

void SAnimTrackCurvePanel::DeleteTrack(USkeleton::AnimCurveUID Uid)
{
	if(Sequence->RawCurveData.GetCurveData(Uid, FRawCurveTracks::TransformType))
	{
		const FScopedTransaction Transaction( LOCTEXT("AnimCurve_DeleteTrack", "Delete Curve") );
		Sequence->Modify(true);
		Sequence->bNeedsRebake = true;
		Sequence->RawCurveData.DeleteCurveData(Uid, FRawCurveTracks::TransformType);
		UpdatePanel();
		WeakPersona.Pin()->RefreshPreviewInstanceTrackCurves();
	}
}

void SAnimTrackCurvePanel::UpdatePanel()
{
	if(Sequence != NULL)
	{
		USkeleton* CurrentSkeleton = Sequence->GetSkeleton();
		FSmartNameMapping* MetadataNameMap = CurrentSkeleton->SmartNames.GetContainer(USkeleton::AnimTrackCurveMappingName);
		// Sort the raw curves before setting up display
		Sequence->RawCurveData.TransformCurves.Sort([MetadataNameMap](const FTransformCurve& A, const FTransformCurve& B)
		{
			bool bAMeta = A.GetCurveTypeFlag(ACF_Metadata);
			bool bBMeta = B.GetCurveTypeFlag(ACF_Metadata);
			
			if(bAMeta != bBMeta)
			{
				return !bAMeta;
			}

			FName AName;
			FName BName;
			MetadataNameMap->GetName(A.CurveUid, AName);
			MetadataNameMap->GetName(B.CurveUid, BName);

			return AName < BName;
		});

		// see if we need to clear or not
		FChildren * VariableChildren = PanelSlot->GetChildren();
		for (int32 Id=VariableChildren->Num()-1; Id>=0; --Id)
		{
			PanelSlot->RemoveAt(Id);
		}

		// Clear all tracks as we're re-adding them all anyway.
		Tracks.Empty();

		// Updating new tracks
		FSmartNameMapping* NameMapping = CurrentSkeleton->SmartNames.GetContainer(USkeleton::AnimTrackCurveMappingName);

		const int32 TotalCurve = Sequence->RawCurveData.TransformCurves.Num();
		for(int32 CurrentIt = 0 ; CurrentIt < TotalCurve ; ++CurrentIt)
		{
			FTransformCurve&  Curve = Sequence->RawCurveData.TransformCurves[CurrentIt];

			const bool bEditable = Curve.GetCurveTypeFlag(ACF_Editable);
			FName CurveName;

			// if editable, add to the list
			if(bEditable && NameMapping->GetName(Curve.CurveUid, CurveName))
			{
				TSharedPtr<STransformCurveEdTrack> CurrentTrack;
				PanelSlot->AddSlot()
				.SizeRule(SSplitter::SizeToContent)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.VAlign(VAlign_Center)
					[
						SAssignNew(CurrentTrack, STransformCurveEdTrack)
						.Sequence(Sequence)
						.CurveUid(Curve.CurveUid)
						.AnimTrackCurvePanel(SharedThis(this))
						.WidgetWidth(WidgetWidth)
						.ViewInputMin(ViewInputMin)
						.ViewInputMax(ViewInputMax)
						.OnGetScrubValue(OnGetScrubValue)
						.OnSetInputViewRange(OnSetInputViewRange)
					]
				];
				Tracks.Add(CurrentTrack);
			}
		}

		TSharedPtr<FPersona> SharedPersona = WeakPersona.Pin();
		if(SharedPersona.IsValid())
		{
			SharedPersona->OnCurvesChanged.Broadcast();
		}
	}
}

TSharedRef<SWidget> SAnimTrackCurvePanel::GenerateCurveList()
{
	TSharedPtr<SVerticalBox> MainBox, ListBox;
	TSharedRef<SWidget> NewWidget = SAssignNew(MainBox, SVerticalBox);

	if ( Sequence && Sequence->RawCurveData.TransformCurves.Num() > 0)
	{
		MainBox->AddSlot()
			.AutoHeight()
			.MaxHeight(300)
			[
				SNew( SScrollBox )
				+SScrollBox::Slot() 
				[
					SAssignNew(ListBox, SVerticalBox)
				]
			];

		// Mapping to retrieve curve names
		FSmartNameMapping* NameMapping = Sequence->GetSkeleton()->SmartNames.GetContainer(USkeleton::AnimTrackCurveMappingName);
		check(NameMapping);

		for (auto Iter=Sequence->RawCurveData.TransformCurves.CreateConstIterator(); Iter; ++Iter)
		{
			const FTransformCurve& Curve= *Iter;

			FName CurveName;
			NameMapping->GetName(Curve.CurveUid, CurveName);

			ListBox->AddSlot()
				.AutoHeight()
				.VAlign(VAlign_Center)
				.Padding( 2.0f, 2.0f )
				[
					SNew( SCheckBox )
					.IsChecked(this, &SAnimTrackCurvePanel::IsCurveEditable, Curve.CurveUid)
					.OnCheckStateChanged(this, &SAnimTrackCurvePanel::ToggleEditability, Curve.CurveUid)
					.ToolTipText( LOCTEXT("Show Curves", "Show or Hide Curves") )
					.IsEnabled( true )
					[
						SNew( STextBlock )
						.Text(FText::FromName(CurveName))
					]
				];
		}

		MainBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding( 2.0f, 2.0f )
			[
				SNew( SButton )
				.VAlign( VAlign_Center )
				.HAlign( HAlign_Center )
				.OnClicked( this, &SAnimTrackCurvePanel::RefreshPanel )
				[
					SNew( STextBlock )
					.Text( LOCTEXT("RefreshCurve", "Refresh") )
				]
			];

		MainBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding( 2.0f, 2.0f )
			[
				SNew( SButton )
				.VAlign( VAlign_Center )
				.HAlign( HAlign_Center )
				.OnClicked( this, &SAnimTrackCurvePanel::ShowAll, true )
				[
					SNew( STextBlock )
					.Text( LOCTEXT("ShowAll", "Show All") )
				]
			];

		MainBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding( 2.0f, 2.0f )
			[
				SNew( SButton )
				.VAlign( VAlign_Center )
				.HAlign( HAlign_Center )
				.OnClicked( this, &SAnimTrackCurvePanel::ShowAll, false )
				[
					SNew( STextBlock )
					.Text( LOCTEXT("HideAll", "Hide All") )
				]
			];
	}
	else
	{
		MainBox->AddSlot()
			.AutoHeight()
			.VAlign(VAlign_Center)
			.Padding( 2.0f, 2.0f )
			[
				SNew( STextBlock )
				.Text(LOCTEXT("Not Available", "No curve exists"))
			];
	}

	return NewWidget;
}

ESlateCheckBoxState::Type SAnimTrackCurvePanel::IsCurveEditable(USkeleton::AnimCurveUID Uid) const
{
	if ( Sequence )
	{
		const FTransformCurve* Curve = static_cast<const FTransformCurve *>(Sequence->RawCurveData.GetCurveData(Uid, FRawCurveTracks::TransformType));
		if ( Curve )
		{
			return Curve->GetCurveTypeFlag(ACF_Editable)? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
		}
	}

	return ESlateCheckBoxState::Undetermined;
}

void SAnimTrackCurvePanel::ToggleEditability(ESlateCheckBoxState::Type NewType, USkeleton::AnimCurveUID Uid)
{
	bool bEdit = (NewType == ESlateCheckBoxState::Checked);

	if ( Sequence )
	{
		FTransformCurve * Curve = static_cast<FTransformCurve *>(Sequence->RawCurveData.GetCurveData(Uid, FRawCurveTracks::TransformType));
		if ( Curve )
		{
			Curve->SetCurveTypeFlag(ACF_Editable, bEdit);
		}
	}
}

FReply		SAnimTrackCurvePanel::RefreshPanel()
{
	UpdatePanel();
	return FReply::Handled();
}

FReply		SAnimTrackCurvePanel::ShowAll(bool bShow)
{
	if ( Sequence )
	{
		for (auto Iter = Sequence->RawCurveData.TransformCurves.CreateIterator(); Iter; ++Iter)
		{
			FTransformCurve & Curve = *Iter;
			Curve.SetCurveTypeFlag(ACF_Editable, bShow);
		}

		UpdatePanel();
	}

	return FReply::Handled();
}

ESlateCheckBoxState::Type SAnimTrackCurvePanel::GetCurveFlagAsCheckboxState(USkeleton::AnimCurveUID CurveUid, EAnimCurveFlags InFlag) const
{
	FAnimCurveBase* Curve = Sequence->RawCurveData.GetCurveData(CurveUid, FRawCurveTracks::TransformType);
	return Curve && Curve->GetCurveTypeFlag(InFlag) ? ESlateCheckBoxState::Checked : ESlateCheckBoxState::Unchecked;
}

void SAnimTrackCurvePanel::SetCurveFlagFromCheckboxState(ESlateCheckBoxState::Type CheckState, USkeleton::AnimCurveUID CurveUid, EAnimCurveFlags InFlag)
{
	bool Enabled = CheckState == ESlateCheckBoxState::Checked;
	FAnimCurveBase* Curve = Sequence->RawCurveData.GetCurveData(CurveUid, FRawCurveTracks::TransformType);
	if (Curve)
	{
		Curve->SetCurveTypeFlag(InFlag, Enabled);

		if (InFlag == ACF_Disabled)
		{
			// needs to rebake
			Sequence->bNeedsRebake = true;
			// need to update curves, otherwise they're not disabled
			WeakPersona.Pin()->RefreshPreviewInstanceTrackCurves();		
		}
	}
}

TSharedRef<SWidget> SAnimTrackCurvePanel::CreateCurveContextMenu(USkeleton::AnimCurveUID CurveUid) const
{
	FMenuBuilder MenuBuilder(true, NULL);

	// get the curve data
	FAnimCurveBase* Curve = Sequence->RawCurveData.GetCurveData(CurveUid, FRawCurveTracks::TransformType);
	if (Curve)
	{
		MenuBuilder.BeginSection("AnimTrackCurvePanelCurveTypes", LOCTEXT("CurveTypesHeading", "Curve Types"));
		{
			MenuBuilder.AddWidget(
				SNew(SCheckBox)
				.IsChecked(this, &SAnimTrackCurvePanel::GetCurveFlagAsCheckboxState, CurveUid, ACF_Disabled)
				.OnCheckStateChanged(this, &SAnimTrackCurvePanel::SetCurveFlagFromCheckboxState, CurveUid, ACF_Disabled)
				.ToolTipText(LOCTEXT("DisableCurveTooltip", "Disable Track"))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("DisableCurveTextLabel", "Disable Curve"))
				],
				FText()
				);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection("AnimTrackCurvePanelTrackOptions", LOCTEXT("TrackOptionsHeading", "Track Options"));
		{
			FUIAction NewAction;

			NewAction.ExecuteAction.BindSP(this, &SAnimTrackCurvePanel::DeleteTrack, CurveUid);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("RemoveTrack", "Remove Track"),
				LOCTEXT("RemoveTrackTooltip", "Remove this track"),
				FSlateIcon(),
				NewAction);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}
#undef LOCTEXT_NAMESPACE
