// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#include "K2Node_EnumLiteral.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "KismetCompiledFunctionContext.h"
#include "KismetCompilerMisc.h"
#include "BlueprintFieldNodeSpawner.h"
#include "EditorCategoryUtils.h"
#include "BlueprintActionDatabaseRegistrar.h"

const FName UK2Node_EnumLiteral::GetEnumInputPinName()
{
	static const FName Name(TEXT("Enum"));
	return Name;
}

UK2Node_EnumLiteral::UK2Node_EnumLiteral(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UK2Node_EnumLiteral::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
	if (!Enum)
	{
		MessageLog.Error(*NSLOCTEXT("K2Node", "EnumLiteral_NullEnumError", "Undefined Enum in @@").ToString(), this);
	}
}

void UK2Node_EnumLiteral::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* InputPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Byte, Enum, GetEnumInputPinName());
	Schema->SetPinAutogeneratedDefaultValueBasedOnType(InputPin);

	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Byte, Enum, UEdGraphSchema_K2::PN_ReturnValue);

	Super::AllocateDefaultPins();
}

FSlateIcon UK2Node_EnumLiteral::GetIconAndTint(FLinearColor& OutColor) const
{
	static FSlateIcon Icon("EditorStyle", "GraphEditor.Enum_16x");
	return Icon;
}

FText UK2Node_EnumLiteral::GetTooltipText() const
{
	if (Enum == nullptr)
	{
		return NSLOCTEXT("K2Node", "BadEnumLiteral_Tooltip", "Literal enum (bad enum)");
	}
	else if (CachedTooltip.IsOutOfDate(this))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("EnumName"), FText::FromName(Enum->GetFName()));
		CachedTooltip.SetCachedText(FText::Format(NSLOCTEXT("K2Node", "EnumLiteral_Tooltip", "Literal enum {EnumName}"), Args), this);
	}
	return CachedTooltip;	
}

FText UK2Node_EnumLiteral::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedTooltip.IsOutOfDate(this))
	{
		return GetTooltipText();
	}
	return CachedTooltip;
}

class FKCHandler_EnumLiteral : public FNodeHandlingFunctor
{
public:
	FKCHandler_EnumLiteral(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
	}

	virtual void RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		check(Context.Schema && Cast<UK2Node_EnumLiteral>(Node));
		FNodeHandlingFunctor::RegisterNets(Context, Node);

		UEdGraphPin* InPin = Node->FindPinChecked(UK2Node_EnumLiteral::GetEnumInputPinName());
		UEdGraphPin* Net = FEdGraphUtilities::GetNetFromPin(InPin);
		if (Context.NetMap.Find(Net) == NULL)
		{
			FBPTerminal* Term = Context.CreateLocalTerminalFromPinAutoChooseScope(Net, Context.NetNameMap->MakeValidName(Net));
			Context.NetMap.Add(Net, Term);
		}

		FBPTerminal** ValueSource = Context.NetMap.Find(Net);
		check(ValueSource && *ValueSource);
		UEdGraphPin* OutPin = Node->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
		if (ensure(Context.NetMap.Find(OutPin) == NULL))
		{
			FBPTerminal* TerminalPtr = *ValueSource; //necessary because of CheckAddress in Map::Add
			Context.NetMap.Add(OutPin, TerminalPtr);
		}
	}
};

FNodeHandlingFunctor* UK2Node_EnumLiteral::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_EnumLiteral(CompilerContext);
}

void UK2Node_EnumLiteral::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	struct GetMenuActions_Utils
	{
		static void SetNodeEnum(UEdGraphNode* NewNode, UField const* /*EnumField*/, TWeakObjectPtr<UEnum> NonConstEnumPtr)
		{
			UK2Node_EnumLiteral* EnumNode = CastChecked<UK2Node_EnumLiteral>(NewNode);
			EnumNode->Enum = NonConstEnumPtr.Get();
		}
	};

	UClass* NodeClass = GetClass();
	ActionRegistrar.RegisterEnumActions( FBlueprintActionDatabaseRegistrar::FMakeEnumSpawnerDelegate::CreateLambda([NodeClass](const UEnum* InEnum)->UBlueprintNodeSpawner*
	{
		UBlueprintFieldNodeSpawner* NodeSpawner = UBlueprintFieldNodeSpawner::Create(NodeClass, InEnum);
		check(NodeSpawner != nullptr);
		TWeakObjectPtr<UEnum> NonConstEnumPtr = MakeWeakObjectPtr(const_cast<UEnum*>(InEnum));
		NodeSpawner->SetNodeFieldDelegate = UBlueprintFieldNodeSpawner::FSetNodeFieldDelegate::CreateStatic(GetMenuActions_Utils::SetNodeEnum, NonConstEnumPtr);

		return NodeSpawner;
	}) );
}

FText UK2Node_EnumLiteral::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::Enum);
}

