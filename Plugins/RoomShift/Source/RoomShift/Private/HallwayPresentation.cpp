// Face identity, physical attachment guides and visibility ownership.
#include "HallwayGame.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
int32 PresentationTagNumber(const AActor* Actor,const TCHAR* Prefix)
{
    for(FName Tag:Actor->Tags)
    {
        const FString Value=Tag.ToString();
        if(Value.StartsWith(Prefix))return FCString::Atoi(*Value.RightChop(FCString::Strlen(Prefix)));
    }
    return -1;
}
}

FLinearColor AHallwayPuzzle::FaceColor(int32 Face)
{
    // Clay, sage, dusty blue, mauve, sand. Match authored plaster instances.
    static const FLinearColor Colors[]={
        FLinearColor(.67f,.43f,.32f),FLinearColor(.40f,.61f,.47f),
        FLinearColor(.40f,.55f,.72f),FLinearColor(.62f,.45f,.65f),
        FLinearColor(.75f,.65f,.42f)};
    return Face>=0&&Face<5?Colors[Face]:FLinearColor::White;
}

FTransform AHallwayPuzzle::CurrentFaceTransform(int32 Stage,int32 Face) const
{
    FTransform Result=FaceTransform(Stage,Face,State.Turns[Stage][Face]);
    if(Stage==State.Stage&&Face==Turning)
    {
        const float T=FMath::Clamp(TurnTime/.85f,0.f,1.f);
        Result.SetRotation(FQuat::Slerp(TurnStart,TurnEnd,T*T*(3.f-2.f*T)));
    }
    return Result;
}

void AHallwayPuzzle::CreateTethers()
{
    UStaticMesh* Cylinder=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UMaterialInterface* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/RoomShift/M_Surface.M_Surface"));
    if(!Cylinder||!Base)return;
    for(FHallPart& Part:Parts)
    {
        auto* Line=NewObject<UStaticMeshComponent>(this);
        AddInstanceComponent(Line);
        Line->SetupAttachment(GetRootComponent());
        Line->SetMobility(EComponentMobility::Movable);
        Line->SetStaticMesh(Cylinder);
        Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Line->SetCastShadow(false);
        auto* Material=UMaterialInstanceDynamic::Create(Base,this);
        Material->SetVectorParameterValue(TEXT("Tint"),FaceColor(Part.Face));
        Material->SetVectorParameterValue(TEXT("Color"),FaceColor(Part.Face));
        Material->SetScalarParameterValue(TEXT("Roughness"),.8f);
        Material->SetScalarParameterValue(TEXT("Highlight"),.08f);
        Line->SetMaterial(0,Material);
        Line->RegisterComponent();
        Part.Tether=Line;
    }
}

void AHallwayPuzzle::UpdateTethers()
{
    for(FHallPart& Part:Parts)
    {
        if(!Part.Tether.IsValid()||!Part.Mesh.IsValid())continue;
        const bool Visible=Part.Stage==State.Stage&&!State.Joined[Part.Item]&&Part.Item!=Landing;
        Part.Tether->SetVisibility(Visible);
        if(!Visible)continue;
        const FTransform Face=CurrentFaceTransform(Part.Stage,Part.Face);
        const FVector PieceCenter=Part.Mesh->Bounds.Origin;
        FVector Anchor=Face.InverseTransformPosition(PieceCenter);
        const FVector Center=Part.Stage==1?BedroomCenter:EntranceCenter;
        // Project onto the correct wall plane, using the same rotating frame as the part.
        if(Part.Face==0)Anchor.Y=400-Center.Y;
        if(Part.Face==1)Anchor.X=(Part.Stage==1?160:610)-Center.X;
        if(Part.Face==2)Anchor.X=(Part.Stage==1?1470:1240)-Center.X;
        if(Part.Face==3)Anchor.Y=(Part.Stage==1?1740:380)-Center.Y;
        if(Part.Face==4)Anchor.Z=(Part.Stage==1?780:535)-Center.Z;
        const FVector WallPoint=Face.TransformPosition(Anchor);
        const FVector Delta=WallPoint-PieceCenter;
        const float Thickness=Part.Face==Selected?.014f:.007f;
        Part.Tether->SetWorldLocation((PieceCenter+WallPoint)*.5f);
        Part.Tether->SetWorldRotation(FRotationMatrix::MakeFromZ(Delta).Rotator());
        Part.Tether->SetWorldScale3D(FVector(Thickness,Thickness,Delta.Size()/100.f));
    }
}

void AHallwayPuzzle::RestoreCompletedActors()
{
    for(int32 Item=0;Item<ItemCount;++Item)if(AActor* Actor=Sources[Item].Get())
    {
        Actor->SetActorTransform(SourceTransforms[Item]);
        Actor->SetActorHiddenInGame(!State.Joined[Item]||(bStartInBedroom&&ItemStage(Item)==0));
        if(auto* Mesh=Actor->FindComponentByClass<UStaticMeshComponent>())
            Mesh->SetOverlayMaterial(Item==FlashItem?SuccessMaterial.Get():nullptr);
    }
    for(const auto& Entry:PropTransforms)if(AActor* Actor=Entry.Key.Get())
    {
        const int32 Item=PresentationTagNumber(Actor,TEXT("RH_PROP_"));
        if(Item<0||Item>=ItemCount)continue;
        if(!Actor->GetActorTransform().Equals(Entry.Value))Actor->SetActorTransform(Entry.Value);
        Actor->SetActorHiddenInGame(!State.Joined[Item]||(bStartInBedroom&&ItemStage(Item)==0));
    }
}

void AHallwayPuzzle::ApplyRoomIsolation()
{
    if(!bStartInBedroom)return;
    for(auto Actor:EntranceActors)if(Actor.IsValid())Actor->SetActorHiddenInGame(true);
    if(Curtain.IsValid())Curtain->SetActorHiddenInGame(true);
    // The authored fixed enclosure remains visible and never inherits face rotations.
}
