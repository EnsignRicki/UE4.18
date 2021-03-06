// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

//~=============================================================================
// MaterialFunctionFactoryNew
//~=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "MaterialFunctionMaterialLayerBlendFactory.generated.h"

UCLASS(MinimalAPI, hidecategories=Object, collapsecategories)
class UMaterialFunctionMaterialLayerBlendFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual bool CanCreateNew() const override;
	virtual UObject* FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface	
};



