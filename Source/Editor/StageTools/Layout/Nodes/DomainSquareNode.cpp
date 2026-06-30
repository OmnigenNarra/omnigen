#include "stdafx.h"
#include "DomainSquareNode.h"
#include "Scene/Generation/Stages/Layout/DomainDrawable.h"
#include "Scene/Generation/OmnigenGenerationData.h"

void omniSave(const DomainSquareNode& object, OmniBin<std::ios::out>& omniBin)
{
	omniBin << object.square;
	omniBin << object.createdOnCurrentStage;
	omniBin << object.shorelineNodes;
	omniBin << object.landmassNodes;
	omniBin << object.ridgeNodes;
	omniBin << object.isohypseNodes;
}

void omniLoad(DomainSquareNode& object, OmniBin<std::ios::in>& omniBin)
{
	omniBin >> object.square;
	omniBin >> object.createdOnCurrentStage;
	omniBin >> object.shorelineNodes;
	omniBin >> object.landmassNodes;
	omniBin >> object.ridgeNodes;
	omniBin >> object.isohypseNodes;
}